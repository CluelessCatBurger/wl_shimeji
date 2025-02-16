/*
    shimeji-overlay.c - Shimeji overlay daemon

    Copyright (C) 2025  CluelessCatBurger <github.com/CluelessCatBurger>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE
#include <sys/mman.h>
#include "mascot_config_parser.h"
#include "mascot.h"
#include "environment.h"
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/un.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include "plugins.h"
#include "config.h"
#include "physics.h"
#include "list.h"
#include "packet_handler.h"

#define MASCOT_OVERLAYD_INSTANCE_EXISTS -1
#define MASCOT_OVERLAYD_INSTANCE_CREATE_ERROR -2
#define MASCOT_OVERLAYD_INSTANCE_BIND_ERROR -3
#define MASCOT_OVERLAYD_INSTANCE_LISTEN_ERROR -4
#define MASCOT_OVERLAYD_INSTANCE_ACCEPT_ERROR -5

#define MASCOT_OVERLAYD_INSTANCE_DEFAULT_ENV_COUNT 2
#define MASCOT_OVERLAYD_INSTANCE_DEFAULT_THREAD_COUNT 1
#define MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT 256

#ifndef DEFAULT_CONF_PATH
#define DEFAULT_CONF_PATH "%s/.local/share/wl_shimeji"
#endif

#ifndef DEFAULT_PROTOS_PATH
#define DEFAULT_PROTOS_PATH "%s/shimejis"
#endif

#ifndef DEFAULT_PLUGINS_PATH
#define DEFAULT_PLUGINS_PATH "%s/plugins"
#endif

#ifndef DEFAULT_SOCK_PATH
#define DEFAULT_SOCK_PATH "%s/shimeji-overlayd.sock"
#endif

static mascot_prototype_store* prototype_store = NULL;
static struct mascot_affordance_manager affordance_manager = {0};

char socket_path[256] = {0};
char config_path[256] = {0};
char plugins_path[256] = {0};

struct plugin* plugins[256] = {0};

struct {
    environment_t** entries;
    uint8_t* entry_states;
    size_t entry_count;
    size_t used_count;
    pthread_mutex_t mutex;
} environment_store = {0};

jmp_buf recovery_point;
bool should_exit = false;
int32_t signal_code = 0;

struct {
    pthread_t** entries;
    uint8_t* entry_states;
    size_t entry_count;
    size_t used_count;
} thread_store = {0};

struct {
    struct mascot** entries;
    uint8_t* entry_states;
    size_t entry_count;
    size_t used_count;
    pthread_mutex_t mutex;
} mascot_store = {0};

struct env_data {
    pthread_t interpolation_thread;
    bool stop;
};

struct daemon_data overlay_runtime_state = {0};

static void* env_interpolation_thread(void*);

static void env_new(environment_t* environment)
{
    pthread_mutex_lock(&environment_store.mutex);
    if (environment_store.entry_count == environment_store.used_count) {
        environment_store.entry_count *= 2;
        environment_store.entries = realloc(environment_store.entries, environment_store.entry_count * sizeof(environment_t*));
        environment_store.entry_states = realloc(environment_store.entry_states, environment_store.entry_count * sizeof(uint8_t));

        for (size_t i = environment_store.used_count; i < environment_store.entry_count; i++) {
            environment_store.entries[i] = NULL;
            environment_store.entry_states[i] = 0;
        }

        if (!environment_store.entries || !environment_store.entry_states) {
            ERROR("Failed to allocate memory for environment store");
        }
    }

    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (!environment_store.entry_states[i]) {

            for (size_t j = 0; j < environment_store.entry_count; j++) {
                if (environment_store.entries[j]) {
                    environment_announce_neighbor(environment_store.entries[j], environment);
                    environment_announce_neighbor(environment, environment_store.entries[j]);
                }
            }

            for (size_t j = 0; j < list_size(overlay_runtime_state.clients); j++) {
                struct client* client = list_get(overlay_runtime_state.clients, j);
                if (client) {
                    struct packet* packet = build_environment(environment, 0);
                    if (packet) {
                        overlay_runtime_state.send_packet(client, packet);
                    }
                }
            }

            environment_store.entries[i] = environment;
            environment_store.entry_states[i] = 1;
            environment_store.used_count++;

            list_add(overlay_runtime_state.environments, environment);

            struct env_data* data = calloc(1, sizeof(struct env_data));
            if (!data) {
                ERROR("Failed to allocate memory for environment data");
            }
            pthread_mutex_unlock(&environment_store.mutex);
            environment_set_user_data(environment, data);
            environment_set_affordance_manager(environment, &affordance_manager);

            pthread_create(&data->interpolation_thread, NULL, env_interpolation_thread, environment);
            return;
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);


}

static void env_delete(environment_t* environment)
{
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entries[i] == environment) {
            environment_store.entries[i] = NULL;
            environment_store.entry_states[i] = 0;
            environment_store.used_count--;

            for (size_t j = 0; j < environment_store.entry_count; j++) {
                if (environment_store.entries[j]) {
                    environment_widthdraw_neighbor(environment_store.entries[j], environment);
                    environment_widthdraw_neighbor(environment, environment_store.entries[j]);
                }
            }

            for (size_t j = 0; j < list_size(overlay_runtime_state.clients); j++) {
                struct client* client = list_get(overlay_runtime_state.clients, j);
                if (client) {
                    struct packet* packet = build_environment(environment, 1);
                    if (packet) {
                        overlay_runtime_state.send_packet(client, packet);
                    }
                }
            }

            pthread_mutex_unlock(&environment_store.mutex);

            struct env_data* data = environment_get_user_data(environment);
            if (data) {
                data->stop = true;
                pthread_join(data->interpolation_thread, NULL);
                free(data);
            }

            list_remove(overlay_runtime_state.environments, list_find(overlay_runtime_state.environments, environment));

            environment_unlink(environment);
            return;
        }
    }

    pthread_mutex_unlock(&environment_store.mutex);
}


static environment_t* find_env_by_coords(int32_t x, int32_t y)
{
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
           struct bounding_box* geometry = environment_global_geometry(environment_store.entries[i]);
           if (is_inside(geometry, x, y)) {
               pthread_mutex_unlock(&environment_store.mutex);
               return environment_store.entries[i];
           }
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);
    return NULL;
}


void* env_interpolation_thread(void* data)
{
    TRACE("Starting environment interpolation thread");
    environment_t* env = (environment_t*)data;
    struct env_data* env_data = environment_get_user_data(env);
    uint64_t sleep_time;
    while (!env_data->stop) {
        pthread_mutex_lock(&mascot_store.mutex);
        sleep_time = environment_interpolate(env);
        pthread_mutex_unlock(&mascot_store.mutex);
        environment_commit(env);

        if (!sleep_time) sleep_time = 40000;
        usleep(sleep_time);
    }
    return NULL;
}

static void orphaned_mascot(struct mascot* mascot) {
    // Find new environment for mascot
    pthread_mutex_lock(&environment_store.mutex);
    environment_t* env = NULL;
    float env_score = 0.0;
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            float r = drand48();
            if (r > env_score) {
                env = environment_store.entries[i];
                env_score = r;
            }
        }
    }
    if (!env) {
        WARN("No environment found for orphaned mascot");
        mascot_unlink(mascot);
        return;
    }
    mascot_environment_changed(mascot, env);
    pthread_mutex_unlock(&environment_store.mutex);
    pthread_mutex_unlock(&mascot_store.mutex);

}

static void broadcast_input_enabled_listener(bool enabled)
{
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_set_input_state(environment_store.entries[i], enabled);
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);
}

// Sigaction handler for SIGSEGV and SIGINT
void sigaction_handler(int signum, siginfo_t* info, void* context)
{
    UNUSED(context);
    if (signum == SIGSEGV) {
        WARN("Segmentation fault occurred at address %p", info->si_addr);
        longjmp(recovery_point, 1);
        signal_code = signum;
    } else if (signum == SIGINT) {
        INFO("Received SIGINT, shutting down...");
        should_exit = true;
    } else if (signum == SIGHUP) {
        INFO("Received SIGHUP, reloading configuration...");
        config_parse(config_path);
    }
    else {
        ERROR("Unexpected signal %d received!", signum);
    }
}

size_t get_default_config_path(char* pathbuf, size_t pathbuf_len)
{
    // Get home directory
    const char *home = secure_getenv("HOME");
    if (!home) return 0;

    return snprintf(pathbuf, pathbuf_len, DEFAULT_CONF_PATH, home);
}

size_t get_mascots_path(char* pathbuf, size_t pathbuf_len)
{
    return snprintf(pathbuf, pathbuf_len, DEFAULT_PROTOS_PATH, config_path);
}

size_t get_plugins_path(char* pathbuf, size_t pathbuf_len)
{
    return snprintf(pathbuf, pathbuf_len, DEFAULT_PLUGINS_PATH, config_path);
}

size_t get_default_socket_path(char* pathbuf, size_t pathbuf_len)
{
    const char *prefix = secure_getenv("XDG_RUNTIME_DIR");
    if (!prefix) prefix = "/tmp";

    return snprintf(pathbuf, pathbuf_len, DEFAULT_SOCK_PATH, prefix);
}

int32_t create_socket(const char* socket_path)
{
    struct stat st;
    if (!stat(socket_path, &st)) {
        // Try to connect to existing socket, if successful, return error
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0) return MASCOT_OVERLAYD_INSTANCE_CREATE_ERROR;

        struct sockaddr_un addr;
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        addr.sun_path[sizeof(addr.sun_path) - 1] = 0;

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            return MASCOT_OVERLAYD_INSTANCE_EXISTS;
        }
        close(fd);
        unlink(socket_path);
    }
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return MASCOT_OVERLAYD_INSTANCE_CREATE_ERROR;

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = 0;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return MASCOT_OVERLAYD_INSTANCE_BIND_ERROR;
    }

    if (listen(fd, 1) < 0) {
        close(fd);
        return MASCOT_OVERLAYD_INSTANCE_LISTEN_ERROR;
    }

    return fd;
}

struct mascot_spawn_data {
    int fd;
    char name[128];
};

// ----------------------------------------------------------------------------

enum mascot_prototype_load_result load_prototype(const char* path)
{
    struct mascot_prototype* prototype = mascot_prototype_new();
    if (!prototype) return PROTOTYPE_LOAD_OOM;

    enum mascot_prototype_load_result result = mascot_prototype_load(prototype, path);
    if (result != PROTOTYPE_LOAD_SUCCESS) {
        mascot_prototype_unlink(prototype);
        return result;
    }

    struct mascot_prototype* old_prototype = mascot_prototype_store_get(prototype_store, prototype->name);
    if (old_prototype) {
        mascot_prototype_store_remove(prototype_store, old_prototype);
    }
    mascot_prototype_store_add(prototype_store, prototype);
    return PROTOTYPE_LOAD_SUCCESS;
}

struct socket_description {
    int fd;
    enum {
        SOCKET_TYPE_LISTEN,
        SOCKET_TYPE_CLIENT,
        SOCKET_TYPE_ENVIRONMENT,
    } type;
    struct client* connector;
};

void* mascot_manager_thread(void* arg)
{
    pthread_mutex_lock(&environment_store.mutex);
    UNUSED(arg);
    for (uint32_t tick = 0; ; tick++) {
        for (size_t i = 0; i < environment_store.entry_count; i++) {
            // if (tick % 5 != 0) break;
            if (environment_store.entry_states[i]) {
                struct environment* environment = environment_store.entries[i];
                environment_pre_tick(environment, tick);
                environment_tick(environment, tick);
                environment_commit(environment);
            }
        }
        pthread_mutex_unlock(&environment_store.mutex);
        usleep(40000);
        if (should_exit) {
            break;
        }
    }
    pthread_mutex_lock(&environment_store.mutex);
    exit(0);
};

void stop_callback()
{
    should_exit = true;
}

void send_packet_cb(struct client* client, struct packet* packet)
{
    if (!client) return;
    if (!packet) return;
    uint32_t pos = packet->position;
    packet->position = 0;
    uint8_t type;
    uint8_t version;
    uint16_t length;
    uint32_t event_id;
    if (!read_header(packet, &type, &version, &length, &event_id)) {
        ERROR("Failed to read packet header");
        return;
    }
    packet->position = 0;
    length = pos+1;
    if (!write_header(packet, type, version, length, event_id)) {
        ERROR("Failed to write packet header");
        return;
    }

    int send_result = send(client->fd, packet->buffer, length, 0);\
    if (send_result < 0) {
        WARN("Failed to send packet to client");
    }

    destroy_packet(packet);
}

int main(int argc, const char** argv)
{
    int inhereted_fd = -1;
    int fds_count = 0;
    bool spawn_everything = false;
    int env_init_flags = 0;
    bool disable_plugins = false;

    get_default_config_path(config_path, 256);
    get_default_socket_path(socket_path, 128);

    if (get_plugins_path(plugins_path, 256) == 0) {
        fprintf(stderr, "Failed to get plugins path\n");
        return 1;
    }

    // Setup SIGINT handler early
    sigaction(SIGINT, &(struct sigaction) {
        .sa_sigaction = sigaction_handler,
    }, NULL);

    // Arguments:
    // -s, --socket-path <path> - path to the socket (optional)
    // -c, --config-dir <path> - path to the mascots folder (optional)
    // -cfd, --caller-fd <fd> - inhereted fd (optional)
    // -se, --spawn-everything - spawn all known mascots (optional)
    // -p, --plugins-path <path> - path to the plugins folder (optional)
    // -h, --help - show help
    // -v, --version - show version
    // --no-tablets - disable tablet_v2 protocol
    // --no-viewporter - disable viewporter protocol
    // --no-fractional-scale - disable fractional scale protocol
    // --no-cursor-shape - disable cursor shape protocol
    // --no-plugins - disable plugins

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--socket-path") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --socket-path\n");
                return 1;
            }
            strncpy(socket_path, argv[i + 1], 127);
            i++;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --config-dir\n");
                return 1;
            }
            strncpy(config_path, argv[i + 1], 255);
            i++;
        } else if (strcmp(argv[i], "-cfd") == 0 || strcmp(argv[i], "--caller-fd") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --caller-fd\n");
                return 1;
            }
            inhereted_fd = atoi(argv[i + 1]);
            // Ensure that the fd is valid
            if (fcntl(inhereted_fd, F_GETFD) == -1) {
                ERROR("Caller fd is invalid!");
                return 1;
            }
            fds_count++;
            i++;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--plugins-path") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --plugins-path\n");
                return 1;
            }
            strncpy(plugins_path, argv[i + 1], 255);
            i++;
        } else if (strcmp(argv[i], "--no-tablets") == 0) {
            env_init_flags |= ENV_DISABLE_TABLETS;
        } else if (strcmp(argv[i], "--no-viewporter") == 0) {
            env_init_flags |= ENV_DISABLE_VIEWPORTER | ENV_DISABLE_FRACTIONAL_SCALE;
        } else if (strcmp(argv[i], "--no-fractional-scale") == 0) {
            env_init_flags |= ENV_DISABLE_FRACTIONAL_SCALE;
        } else if (strcmp(argv[i], "--no-cursor-shape") == 0) {
            env_init_flags |= ENV_DISABLE_CURSOR_SHAPE;
        } else if (strcmp(argv[i], "--no-plugins") == 0) {
            disable_plugins = true;
        } else if (strcmp(argv[i], "-dwt") == 0 || strcmp(argv[i], "--disable-tablets-workarounds") == 0) {
            environment_disable_tablet_workarounds(true);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("%s\n", WL_SHIMEJI_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -s, --socket-path <path> - path to the socket (optional)\n");
            printf("  -c, --config-dir <path> - path to the mascots folder (optional)\n");
            printf("  -cfd, --caller-fd <fd> - inhereted fd (optional)\n");
            printf("  -p, --plugins-path <path> - path to the plugins folder (optional)\n");
            printf("  -h, --help - show help\n");
            printf("  -v, --version - show version\n");
            printf("  --no-tablets - disable tablet_v2 protocol\n");
            printf("  --no-viewporter - disable viewporter protocol\n");
            printf("  --no-fractional-scale - disable fractional scale protocol\n");
            printf("  --no-cursor-shape - disable cursor shape protocol\n");
            printf("  --no-plugins - disable plugins\n");
            return 0;
        } else if (strcmp(argv[i], "-se") == 0 || strcmp(argv[i], "--spawn-everything") == 0) {
            spawn_everything = true;
        } else {
            fprintf(stderr, "Error: unknown argument %s\n", argv[i]);
            return 1;
        }
    }

    char config_file_path[256] = {0};
    int slen = snprintf(config_file_path, 255, "%s/shimeji.conf", config_path);
    if (slen < 0 || slen >= 255) {
        ERROR("Provided config directory path is too long!");
        return 1;
    }

    if (!config_parse(config_file_path)) {
        INFO("Bad config! Creating new one.");
        // config_set_breeding(true);
        // config_set_dragging(true);
        // config_set_ie_interactions(true);
        // config_set_ie_throwing(false);
        // config_set_cursor_data(true);
        // config_set_mascot_limit(100);
        // config_set_ie_throw_policy(PLUGIN_IE_THROW_POLICY_LOOP);
        // config_set_allow_dismiss_animations(true);
        // config_set_per_mascot_interactions(true);
        // config_set_tick_delay(40000);
        // config_set_overlay_layer(LAYER_TYPE_OVERLAY);
        config_write(config_file_path);
    }

    char mascots_path_packages[256] = {0};
    slen = get_mascots_path(mascots_path_packages, 255);
    struct stat st = {0};
    if (slen < 0 || slen >= 255) {
        ERROR("Provided config directory path is too long!");
        return 1;
    }
    if (stat(mascots_path_packages, &st) == -1) {
        mkdir(mascots_path_packages, 0700);
    }
    if (stat(mascots_path_packages, &st) == -1) {
        ERROR("Failed to create mascots packages directory");
        return 1;
    }

    srand48(time(NULL));

    // Create socket
    int listenfd = create_socket(socket_path);
    if (listenfd == MASCOT_OVERLAYD_INSTANCE_EXISTS) {
        INFO("Another instance of mascot-overlayd is already running");
        return 0;
    } else if (listenfd == MASCOT_OVERLAYD_INSTANCE_CREATE_ERROR) {
        ERROR("Failed to create socket");
    } else if (listenfd == MASCOT_OVERLAYD_INSTANCE_BIND_ERROR) {
        ERROR("Failed to bind socket");
    } else if (listenfd == MASCOT_OVERLAYD_INSTANCE_LISTEN_ERROR) {
        ERROR("Failed to listen on socket");
    }

    // Initialize prototype store
    prototype_store = mascot_prototype_store_new();

    // Initialize mascot store
    mascot_store.entries = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(struct mascot*));
    mascot_store.entry_states = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(uint8_t));
    mascot_store.entry_count = MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT;
    mascot_store.used_count = 0;
    pthread_mutex_init(&mascot_store.mutex, NULL);

    // Initialize mascot manager thread store
    thread_store.entries = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(pthread_t));
    thread_store.entry_states = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(uint8_t));
    thread_store.entry_count = MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT;
    thread_store.used_count = 0;

    // Initialize environment store
    environment_store.entries = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(struct environment*));
    environment_store.entry_states = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(uint8_t));
    environment_store.entry_count = MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT;
    environment_store.used_count = 0;
    pthread_mutex_init(&environment_store.mutex, NULL);

    // Initialize affordance manager
    pthread_mutex_init(&affordance_manager.mutex, NULL);
    affordance_manager.slots = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(struct mascot*));
    affordance_manager.slot_count = MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT;
    affordance_manager.occupied_slots_count = 0;
    affordance_manager.slot_state = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(uint8_t));

    overlay_runtime_state.clients = list_init(1);
    overlay_runtime_state.initialized = true;
    overlay_runtime_state.config_path = config_path;
    overlay_runtime_state.environments = list_init(2);
    overlay_runtime_state.prototypes = prototype_store;
    pthread_mutex_init(&overlay_runtime_state.env_mutex, NULL);
    pthread_mutex_init(&overlay_runtime_state.proto_mutex, NULL);
    pthread_mutex_init(&overlay_runtime_state.client_mutex, NULL);
    overlay_runtime_state.send_packet = send_packet_cb;
    overlay_runtime_state.stop = stop_callback;

    struct client* inhereted_connector = connect_client(inhereted_fd, &overlay_runtime_state);

    // Initialize environment
    environment_set_global_coordinates_searcher(find_env_by_coords);
    enum environment_init_status env_init = environment_init(env_init_flags, env_new, env_delete, orphaned_mascot);
    if (env_init != ENV_INIT_OK) {
        char buf[256];
        snprintf(buf, 255, "Failed to initialize environment:\n%s", environment_get_error());
        struct packet* packet = build_initialization_status(1, buf);
        if (packet) {
            overlay_runtime_state.send_packet(inhereted_connector, packet);
        }
        ERROR("Failed to initialize environment: %s", environment_get_error());
    }

    environment_set_broadcast_input_enabled_listener(broadcast_input_enabled_listener);

    size_t plugin_count = 0;

    // Load plugins
    if (!disable_plugins){
        DIR *plugins_dir = opendir(plugins_path);
        if (plugins_dir) {
            struct dirent *ent;
            while ((ent = readdir(plugins_dir)) != NULL) {
                if (ent->d_type == DT_REG) {
                    char plugin_path[256];
                    int slen = snprintf(plugin_path, 255, "%s/%s", plugins_path, ent->d_name);
                    if (slen < 0 || slen >= 255) {
                        WARN("Plugin path is too long! Skipping...");
                        continue;
                    }
                    struct plugin* plugin = plugin_open(plugin_path);
                    if (!plugin) {
                        WARN("Failed to load plugin %s", plugin_path);
                        continue;
                    }
                    const char* error = NULL;
                    int init_flags = 0;
                    init_flags |= config_get_cursor_data() ? PLUGIN_PROVIDES_CURSOR_POSITION : 0;
                    init_flags |= config_get_ie_interactions() ? PLUGIN_PROVIDES_IE_POSITION : 0;
                    init_flags |= (config_get_ie_interactions() && config_get_ie_throwing() && config_get_ie_throw_policy()) ? PLUGIN_PROVIDES_IE_MOVE : 0;
                    enum plugin_initialization_result init_status = plugin_init(plugin, init_flags, &error);
                    if (init_status != PLUGIN_INIT_OK) {
                        WARN("Failed to initialize plugin %s: %s", plugin_path, error);
                        plugin_deinit(plugin);
                        plugin_close(plugin);
                        continue;
                    }
                    if (plugin->provides & PLUGIN_PROVIDES_IE_MOVE) plugin_execute_ie_throw_policy(plugin, config_get_ie_throw_policy());
                    plugins[plugin_count++] = plugin;
                }
            }
            closedir(plugins_dir);
        }

        // Assign plugins to environments
        for (size_t i = 0; i < environment_store.entry_count; i++) {
            if (environment_store.entry_states[i]) {
                environment_t* env = environment_store.entries[i];
                for (size_t j = 0; j < plugin_count; j++) {
                    struct plugin* plugin = plugins[j];
                    struct ie_object* ie = NULL;
                    enum plugin_execution_result exec_res = plugin_get_ie_for_environment(plugin, env, &ie);
                    if (exec_res == PLUGIN_EXEC_SEGFAULT) {
                        WARN("Plugin %s segfaulted while getting IE for environment %d", plugin->name, i);
                    } else if (exec_res == PLUGIN_EXEC_ERROR) {
                        WARN("Plugin %s returned an error while getting IE for environment %d", plugin->name, i);
                    } else if (exec_res == PLUGIN_EXEC_OK) {
                        environment_set_ie(env, ie);
                        break;
                    }
                }
            }
        }
    }

    // Load mascot protos
    DIR* dir = opendir(mascots_path_packages);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_DIR) {
                if (ent->d_name[0] == '.') continue;
                char proto_path[256];
                int slen = snprintf(proto_path, 255, "%s/%s", mascots_path_packages, ent->d_name);
                if (slen < 0 || slen >= 255) {
                    WARN("Mascot prototype path is too long! Skipping...");
                    continue;
                }
                load_prototype(proto_path);
            }
        }
        closedir(dir);
    } else {
        char buf[300];
        snprintf(buf, 299, "Failed to open mascots directory: %s", mascots_path_packages);
        struct packet* packet = build_initialization_status(1, buf);
        if (packet) {
            overlay_runtime_state.send_packet(inhereted_connector, packet);
        }
        ERROR("Failed to open mascots directory: %s", mascots_path_packages);
    }

    // Spawn mascots if requested
    if (spawn_everything) {
        for (int i = 0; i < mascot_prototype_store_count(prototype_store); i++) {
            struct mascot_prototype* proto = mascot_prototype_store_get_index(prototype_store, i);

            // Select random env
            environment_t* env = NULL;
            float env_weight = 0.0;
            for (size_t j = 0; j < environment_store.used_count; j++) {
                if (environment_store.entry_states[j] == 0) {
                    continue;
                }
                environment_t* e = environment_store.entries[j];
                float r = drand48();
                INFO("Random number for env %d: %f", j, r);
                if (r > env_weight) {
                    env = e;
                    env_weight = r;
                }
            }
            if (env == NULL) {
                continue;
            }
            int x = rand() % environment_workarea_width(env);
            int y = environment_workarea_height(env) - 256;
            environment_summon_mascot(env, proto, x, y, NULL, NULL);
        }
    }

    // Create epoll
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        char buf[256];
        snprintf(buf, 255, "Failed to create epoll");
        buf[0] = 0xFF;
        send(inhereted_fd, buf, 1025+sizeof(size_t), 0);
        ERROR("Failed to create epoll");
    }

    // Add listenfd to epoll
    struct epoll_event ev;
    struct socket_description listen_sd = { .fd = listenfd, .type = SOCKET_TYPE_LISTEN };
    ev.events = EPOLLIN;
    ev.data.ptr = &listen_sd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
        char buf[256];
        snprintf(buf, 255, "Failed to add listenfd to epoll");
        struct packet* packet = build_initialization_status(1, buf);
        if (packet) {
            overlay_runtime_state.send_packet(inhereted_connector, packet);
        }
        ERROR("Failed to add listenfd to epoll");
    }

    // Add inhereted fd to epoll (if any)
    if (inhereted_fd != -1) {
        struct socket_description* inhereted_sd = calloc(1, sizeof(struct socket_description));
        *inhereted_sd = (struct socket_description){
            .fd = inhereted_fd,
            .type = SOCKET_TYPE_CLIENT,
            .connector = inhereted_connector,
        };
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.ptr = inhereted_sd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, inhereted_fd, &ev) == -1) {
            char buf[256];
            snprintf(buf, 255, "Failed to add inhereted fd to epoll");
            struct packet* packet = build_initialization_status(1, buf);
            if (packet) {
                overlay_runtime_state.send_packet(inhereted_connector, packet);
            }
            ERROR("Failed to add inhereted fd to epoll");
        }
    }

    // Add wayland fd to epoll
    int wayland_fd = environment_get_display_fd();
    struct socket_description wayland_sd = { .fd = wayland_fd, .type = SOCKET_TYPE_ENVIRONMENT };
    ev.events = EPOLLIN;
    ev.data.ptr = &wayland_sd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, wayland_fd, &ev) == -1) {
        char buf[256];
        snprintf(buf, 255, "Failed to add wayland fd to epoll");
        struct packet* packet = build_initialization_status(1, buf);
        if (packet) {
            overlay_runtime_state.send_packet(inhereted_connector, packet);
        }
        ERROR("Failed to add wayland fd to epoll");
    }

    // Now create 1 thread
    pthread_t* thread = calloc(1, sizeof(pthread_t));
    pthread_create(thread, NULL, mascot_manager_thread, NULL);
    thread_store.entries[0] = thread;
    thread_store.entry_states[0] = 1;
    thread_store.used_count++;

    // Main loop
    while (true) {
        if (!mascot_total_count && !fds_count) {
            break;
        }

        // Process epoll
        struct epoll_event events[16];
        int nfds = epoll_wait(epfd, events, 16, -1);
        if (nfds == -1) {
            WARN("Failed to epoll_wait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            struct socket_description* sd = events[i].data.ptr;
            if (sd->type == SOCKET_TYPE_LISTEN) {
                // Accept new connection
                struct sockaddr_un client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int clientfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_addr_len);
                struct client* connector = connect_client(clientfd, &overlay_runtime_state);
                list_add(overlay_runtime_state.clients, connector);
                if (clientfd == -1 || !connector) {
                    ERROR("Failed to accept new connection");
                }

                // Add clientfd to epoll
                struct socket_description* client_sd = calloc(1, sizeof(struct socket_description));
                client_sd->fd = clientfd;
                client_sd->type = SOCKET_TYPE_CLIENT;
                client_sd->connector = connector;
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLRDHUP;
                ev.data.ptr = client_sd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev) == -1) {

                    ERROR("Failed to add clientfd to epoll");
                }
                fds_count++;
            } else if (sd->type == SOCKET_TYPE_CLIENT) {
                DEBUG("Got data from client");
                // Mode SEQPACKET
                struct msghdr msg = {0};
                struct iovec iov = {0};
                uint8_t buf[256];
                char control[CMSG_SPACE(sizeof(int))];
                iov.iov_base = buf;
                iov.iov_len = sizeof(buf);
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = control;
                msg.msg_controllen = sizeof(control);
                int n = recvmsg(sd->fd, &msg, MSG_CMSG_CLOEXEC);
                if (n == -1) {
                    // Client disconnected
                    epoll_ctl(epfd, EPOLL_CTL_DEL, sd->fd, NULL);
                    list_remove(overlay_runtime_state.clients, list_find(overlay_runtime_state.clients, sd->connector));
                    disconnect_client(sd->connector);
                    free(sd);
                    fds_count--;
                } else {
                    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                    int afd = -1;
                    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                        afd = *((int*)CMSG_DATA(cmsg));
                    }
                    struct packet* packet = new_packet(n, iov.iov_base, afd);
                    if (!packet) {
                        ERROR("Failed to allocate memory for packet");
                    }
                    bool handler_resp = handle_packet(&overlay_runtime_state, sd->connector, packet);
                    if (!handler_resp) {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, sd->fd, NULL);
                        list_remove(overlay_runtime_state.clients, list_find(overlay_runtime_state.clients, sd->connector));
                        disconnect_client(sd->connector);
                        free(sd);
                        fds_count--;
                    }
                    destroy_packet(packet);

                }
            } else if (sd->type == SOCKET_TYPE_ENVIRONMENT) {
                // Wayland event
                if (environment_dispatch() == -1) {
                    LOG("ERROR", RED, "Failed to dispatch wayland events!");
                    pthread_mutex_lock(&mascot_store.mutex);
                    for (size_t i = 0; i < mascot_store.entry_count; i++) {
                        if (mascot_store.entry_states[i]) {
                            struct mascot* mascot = mascot_store.entries[i];
                            mascot_announce_affordance(mascot, NULL);
                            mascot_detach_affordance_manager(mascot);
                            mascot_store.entry_states[i] = 0;
                            mascot_store.entries[i] = NULL;
                            mascot_store.used_count--;
                            mascot_unlink(mascot);
                        }
                    }
                    pthread_mutex_unlock(&mascot_store.mutex);
                    pthread_mutex_lock(&environment_store.mutex);
                    for (size_t i = 0; i < environment_store.entry_count; i++) {
                        if (environment_store.entry_states[i]) {
                            struct environment* env = environment_store.entries[i];
                            environment_commit(env);
                        }
                    }
                    pthread_mutex_unlock(&environment_store.mutex);
                    environment_dispatch();
                    return 1;
                }
            }
        }

    }
    close(listenfd);
    close(inhereted_fd);
    close(epfd);
    unlink(socket_path);
    return 0;
}
