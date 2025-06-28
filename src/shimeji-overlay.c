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

#include <linux/limits.h>
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
#include "io.h"
#include <errno.h>

#include "protocol/server.h"

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
#define DEFAULT_PLUGINS_PATH "/usr/lib/wl_shimeji/plugins"
#endif

#ifndef DEFAULT_CONFIG_FILE_PATH
#define DEFAULT_CONFIG_FILE_PATH "%s/shimeji-overlayd.conf"
#endif

#ifndef DEFAULT_SOCK_PATH
#define DEFAULT_SOCK_PATH "%s/shimeji-overlayd.sock"
#endif

jmp_buf recovery_point;
bool should_exit = false;
int32_t signal_code = 0;

struct list* thread_storage = NULL;
pthread_mutex_t thread_mutex;

struct env_data {
    pthread_t interpolation_thread;
    bool stop;
};

static struct protocol_server_state server_state = {0};

static void* env_interpolation_thread(void*);

static void env_new(environment_t* environment)
{
    pthread_mutex_lock(&server_state.environment_mutex);
    for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
        environment_t* neighbor = list_get(server_state.environments, i);
        if (!neighbor) continue;
        c--;
        if (neighbor == environment) continue;
        environment_announce_neighbor(neighbor, environment);
        environment_announce_neighbor(environment, neighbor);
    }
    struct env_data* data = calloc(1, sizeof(struct env_data));
    if (!data) {
        ERROR("Failed to allocate memory for environment data");
    }

    list_add(server_state.environments, environment);
    protocol_server_announce_new_environment(environment, NULL);
    environment_set_user_data(environment, data);
    environment_set_affordance_manager(environment, &server_state.affordance_manager);

    // for (size_t i = 0; i < plugin_count; i++) environment_announce_plugin(environment, plugins[i]);

    pthread_create(&data->interpolation_thread, NULL, env_interpolation_thread, environment);
    pthread_mutex_unlock(&server_state.environment_mutex);
}

static void env_delete(environment_t* environment)
{
    pthread_mutex_lock(&server_state.environment_mutex);
    uint32_t env_index = list_find(server_state.environments, environment);
    if (env_index != UINT32_MAX) {
        list_remove(server_state.environments, env_index);
    }

    for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
        environment_t* neighbor = list_get(server_state.environments, i);
        if (!neighbor) continue;
        c--;
        if (neighbor == environment) continue;
        environment_widthdraw_neighbor(neighbor, environment);
        environment_widthdraw_neighbor(environment, neighbor);
    }

    pthread_mutex_unlock(&server_state.environment_mutex);

    struct env_data* data = environment_get_user_data(environment);
    if (data) {
        data->stop = true;
        pthread_join(data->interpolation_thread, NULL);
        free(data);
    }
    environment_unlink(environment);
}

static void set_cursor_pos(int32_t x, int32_t y)
{
    environment_set_public_cursor_position(NULL, x, y);
}

static void set_active_ie(bool is_active, int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct bounding_box bb = {
        .type = OUTER_COLLISION,
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };
    pthread_mutex_lock(&server_state.environment_mutex);
    for (uint32_t i = 0; i < list_size(server_state.environments); i++) {
        environment_t* environment = list_get(server_state.environments, i);
        if (!environment) continue;
        environment_recalculate_ie_attachement(environment, is_active, bb);
    }
    pthread_mutex_unlock(&server_state.environment_mutex);
    environment_set_active_ie(is_active, bb);
}

static environment_t* find_env_by_coords(int32_t x, int32_t y)
{
    for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
        environment_t* environment = list_get(server_state.environments, i);
        if (!environment) continue;
        c--;
        struct bounding_box* geometry = environment_global_geometry(environment);
        if (is_inside(geometry, x, y)) {
            return environment;
        }
    }
    return NULL;
}


void* env_interpolation_thread(void* data)
{
    TRACE("Starting environment interpolation thread");
    environment_t* env = (environment_t*)data;
    struct env_data* env_data = environment_get_user_data(env);
    uint64_t sleep_time;
    while (!env_data->stop) {
        sleep_time = environment_interpolate(env);
        environment_commit(env);

        if (!sleep_time) sleep_time = 40000;
        usleep(sleep_time);
    }
    return NULL;
}

static void orphaned_mascot(struct mascot* mascot) {
    // Find new environment for mascot
    pthread_mutex_lock(&server_state.environment_mutex);
    environment_t* env = NULL;
    float env_score = 0.0;
    for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
        environment_t* environment = list_get(server_state.environments, i);
        if (!environment) continue;
        c--;
        float r = drand48();
        if (r > env_score) {
            env = environment;
            env_score = r;
        }
    }
    if (!env) {
        WARN("No environment found for orphaned mascot");
        mascot_unlink(mascot);
        pthread_mutex_unlock(&server_state.environment_mutex);
        return;
    }
    mascot_environment_changed(mascot, env);
    pthread_mutex_unlock(&server_state.environment_mutex);

}

static void broadcast_input_enabled_listener(bool enabled)
{
    pthread_mutex_lock(&server_state.environment_mutex);
    for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
        environment_t* environment = list_get(server_state.environments, i);
        if (!environment) continue;
        c--;
        environment_set_input_state(environment, enabled);
    }
    pthread_mutex_unlock(&server_state.environment_mutex);
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
        server_state.stop = true;
    } else if (signum == SIGHUP) {
        INFO("Received SIGHUP, reloading configuration...");
        config_parse(server_state.configuration_file);
    }
    else {
        ERROR("Unexpected signal %d received!", signum);
    }
}

size_t default_configuration_root(char* pathbuf, size_t pathbuf_len)
{
    // Get home directory
    const char *home = secure_getenv("HOME");
    if (!home) return 0;

    return snprintf(pathbuf, pathbuf_len, DEFAULT_CONF_PATH, home);
}

size_t default_configuration_file(const char* configuration_root, char* pathbuf, size_t pathbuf_len)
{
    return snprintf(pathbuf, pathbuf_len, DEFAULT_CONFIG_FILE_PATH, configuration_root);
}

size_t default_prototypes_location(const char* configuration_root, char* pathbuf, size_t pathbuf_len)
{
    return snprintf(pathbuf, pathbuf_len, DEFAULT_PROTOS_PATH, configuration_root);
}

size_t default_socket_path(char* pathbuf, size_t pathbuf_len)
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

// ----------------------------------------------------------------------------

// enum mascot_prototype_load_result load_prototype(const char* path)
// {
//     struct mascot_prototype* prototype = mascot_prototype_new();
//     if (!prototype) return PROTOTYPE_LOAD_OOM;

//     enum mascot_prototype_load_result result = mascot_prototype_load(prototype, path);
//     if (result != PROTOTYPE_LOAD_SUCCESS) {
//         mascot_prototype_unlink(prototype);
//         return result;
//     }

//     struct mascot_prototype* old_prototype = mascot_prototype_store_get(prototype_store, prototype->name);
//     if (old_prototype) {
//         mascot_prototype_store_remove(prototype_store, old_prototype);
//     }
//     mascot_prototype_store_add(prototype_store, prototype);
//     return PROTOTYPE_LOAD_SUCCESS;
// }

struct socket_description {
    int fd;
    enum {
        SOCKET_TYPE_LISTEN,
        SOCKET_TYPE_CLIENT,
        SOCKET_TYPE_ENVIRONMENT,
    } type;
    struct protocol_client* client;
    ipc_connector_t* ipc_connector;
};

void* mascot_manager_thread(void* arg)
{
    UNUSED(arg);
    for (uint32_t tick = 0; ; tick++) {
        pthread_mutex_lock(&server_state.environment_mutex);
        for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
            environment_t* environment = list_get(server_state.environments, i);
            if (!environment) continue;
            c--;
            // environment_pre_tick(environment, tick);
            environment_tick(environment, tick);
            environment_commit(environment);
        }
        pthread_mutex_unlock(&server_state.environment_mutex);
        plugins_tick();
        // for (int32_t i = 0; i < 32; i++) {
        //     if (!plugins[i]) break;
        //     plugin_tick(plugins[i]);
        // }
        usleep(40000);
        if (should_exit) {
            break;
        }
    }
    return NULL;
};

void stop_callback()
{
    server_state.stop = true;
}

int main(int argc, const char** argv)
{
    int inhereted_fd = -1;
    int listen_fd = -1;
    int fds_count = 0;
    bool spawn_everything = false;
    int env_init_flags = 0;
    bool disable_plugins = false;

    char socket_path[PATH_MAX] = {0};

    protocol_init();
    protocol_set_server_state(&server_state);

    char configuration_root  [PATH_MAX];
    char prototypes_location [PATH_MAX] = {0};
    char plugins_location    [PATH_MAX] = {0};
    char configuration_file  [PATH_MAX] = {0};


    default_configuration_root(configuration_root, PATH_MAX-1);
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
        } else if (strcmp(argv[i], "-cd") == 0 || strcmp(argv[i], "--configuration-root") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --configuration-root\n");
                return 1;
            }
            strncpy(configuration_root, argv[i + 1], PATH_MAX-1);
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
        } else if (strcmp(argv[i], "-pl") == 0 || strcmp(argv[i], "--plugins-location") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --plugins-location\n");
                return 1;
            }
            strncpy(plugins_location, argv[i + 1], PATH_MAX-1);
            i++;
        } else if (strcmp(argv[i], "-pr") == 0 || strcmp(argv[i], "--prototypes-location") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --prototypes-location\n");
                return 1;
            }
            strncpy(prototypes_location, argv[i + 1], PATH_MAX-1);
            i++;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config-file") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: missing argument for --config-file\n");
                return 1;
            }
            strncpy(configuration_file, argv[i + 1], PATH_MAX-1);
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
            printf("  -s, --socket-path <path> - path to the socket\n");
            printf("  -cd, --configuration-root <path> - path to the configuration root of the wl_shimeji (%s by default)\n", configuration_root);
            printf("  -cfd, --caller-fd <fd> - inhereted fd (optional)\n");
            printf("  -c, --config-file <path> - path to the config file\n");
            printf("  -pl, --plugins-location <path> - path to the plugins directory\n");
            printf("  -pr, --prototypes-location <path> - path to the prototypes directory\n");
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

    struct stat st = {0};
    if (stat(configuration_root, &st) == -1) {
        mkdir(configuration_root, 0700);
    }
    if (stat(configuration_root, &st) == -1) {
        ERROR("Failed to create configuration root at %s", configuration_root);
    }

    if (!configuration_file[0]) default_configuration_file(configuration_root, configuration_file, PATH_MAX-1);
    if (!config_parse(configuration_file)) {
        struct stat stat_loc;
        if (stat(configuration_file, &stat_loc)) {
            if (errno == ENOENT) {
                INFO("No configuration file found, creating new one at %s", configuration_file);
                config_write(configuration_file);
            } else ERROR("Failed to read configuration file %s: %s", configuration_file, strerrordesc_np(errno));
        } else ERROR("Malformed configuration file at %s", configuration_file);
    }

    if (!prototypes_location[0]) {
        if (!config_get_prototypes_location()) {
            default_prototypes_location(configuration_root, prototypes_location, PATH_MAX-1);
        } else {
            strncpy(prototypes_location, config_get_prototypes_location(), PATH_MAX-1);
        }
    }

    if (!plugins_location[0]) {
        if (!config_get_plugins_location()) {
            strncpy(plugins_location, DEFAULT_PLUGINS_PATH, PATH_MAX-1);
        } else {
            strncpy(plugins_location, config_get_plugins_location(), PATH_MAX-1);
        }
    }

    if (!socket_path[0]) {
        if (!config_get_socket_location()) {
            default_socket_path(socket_path, PATH_MAX-1);
        } else {
            strncpy(socket_path, config_get_socket_location(), PATH_MAX-1);
        }
    }

    // Initialize state
    {

        pthread_mutexattr_t init_attrs = {0};
        pthread_mutexattr_init(&init_attrs);
        pthread_mutexattr_settype(&init_attrs, PTHREAD_MUTEX_RECURSIVE);

        server_state.clients = list_init(1);
        server_state.environments = list_init(1);
        server_state.plugins = list_init(1);
        server_state.imports = list_init(1);
        server_state.exports = list_init(1);
        server_state.prototypes = mascot_prototype_store_new();
        strncpy(server_state.configuration_root, configuration_root, PATH_MAX);
        strncpy(server_state.plugins_location, plugins_location, PATH_MAX);
        strncpy(server_state.prototypes_location, prototypes_location, PATH_MAX);
        strncpy(server_state.configuration_file, plugins_location, PATH_MAX);
        pthread_mutex_init(&server_state.environment_mutex, &init_attrs);
        pthread_mutex_init(&server_state.prototypes_mutex, &init_attrs);
        pthread_mutex_init(&server_state.clients_mutex, &init_attrs);
        server_state.initialization_errors = calloc(16, sizeof(char*));
    }

    if (stat(server_state.prototypes_location, &st) == -1) {
        mkdir(server_state.prototypes_location, 0700);
    }
    if (stat(server_state.prototypes_location, &st) == -1) {
        ERROR("Failed to create prototypes storage at %s", server_state.prototypes_location);
    }
    if (stat(server_state.plugins_location, &st) == -1) {
        mkdir(server_state.plugins_location, 0700);
    }
    if (stat(server_state.plugins_location, &st) == -1) {
        WARN("Failed to create plugins storage at %s, disabling plugins", server_state.plugins_location);
        disable_plugins = true;
    }

    srand48(time(NULL));

    bool own_socket = false;
    const char* listen_fds_env = getenv("LISTEN_FDS");
    if (listen_fds_env) {
        int listen_fds = atoi(listen_fds_env);
        if (listen_fds > 1) {
            ERROR("wl_shimeji doesn't support multiple listen sockets");
        }
        listen_fd = 3;
    }

    if (listen_fd == -1) {
        // Create socket
        listen_fd = create_socket(socket_path);
        if (listen_fd == MASCOT_OVERLAYD_INSTANCE_EXISTS) {
            INFO("Another instance of mascot-overlayd is already running");
            return 0;
        } else if (listen_fd == MASCOT_OVERLAYD_INSTANCE_CREATE_ERROR) {
            ERROR("Failed to create socket");
        } else if (listen_fd == MASCOT_OVERLAYD_INSTANCE_BIND_ERROR) {
            ERROR("Failed to bind socket");
        } else if (listen_fd == MASCOT_OVERLAYD_INSTANCE_LISTEN_ERROR) {
            ERROR("Failed to listen on socket");
        }
        own_socket = true;
    }

    // Initialize affordance manager
    pthread_mutex_init(&server_state.affordance_manager.mutex, NULL);
    server_state.affordance_manager.slots = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(struct mascot*));
    server_state.affordance_manager.slot_count = MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT;
    server_state.affordance_manager.occupied_slots_count = 0;
    server_state.affordance_manager.slot_state = calloc(MASCOT_OVERLAYD_INSTANCE_DEFAULT_MASCOT_COUNT, sizeof(uint8_t));

    // Initialize environment
    environment_set_global_coordinates_searcher(find_env_by_coords);
    enum environment_init_status env_init = environment_init(env_init_flags, env_new, env_delete, orphaned_mascot);
    if (env_init != ENV_INIT_OK) {
        char buf[256];
        snprintf(buf, 255, "Failed to initialize environment:\n%s", environment_get_error());
        server_state.initialization_errors[server_state.initialization_errors_count++] = strdup(buf);
        server_state.initialization_result = true;
    }

    environment_set_broadcast_input_enabled_listener(broadcast_input_enabled_listener);

    // UNUSED(disable_plugins);
    if (!disable_plugins) {
        plugins_init(plugins_location, set_cursor_pos, set_active_ie);
    }
    else INFO("Plugins are disabled");

    // Load mascot prototypes
    mascot_prototype_store_set_location(server_state.prototypes, server_state.prototypes_location);
    uint32_t num_prototypes = mascot_prototype_store_reload(server_state.prototypes);

    UNUSED(num_prototypes); // TODO: Log prototype loading times

    // Spawn mascots if requested
    if (spawn_everything) {
        for (int i = 0; i < mascot_prototype_store_count(server_state.prototypes); i++) {
            struct mascot_prototype* proto = mascot_prototype_store_get_index(server_state.prototypes, i);
            if (!proto) {
                continue;
            }

            environment_t* env = NULL;
            float env_weight = 0.0;


            // Select random environment to spawn mascot in
            pthread_mutex_lock(&server_state.environment_mutex);
            for (uint32_t i = 0, c = list_count(server_state.environments); i < list_size(server_state.environments) && c; i++) {
                environment_t* environment = list_get(server_state.environments, i);
                if (!environment) continue;
                c--;
                float r = drand48();
                if (r > env_weight) {
                    env = environment;
                    env_weight = r;
                }
            }
            pthread_mutex_unlock(&server_state.environment_mutex);
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
        snprintf(buf, 255, "epoll_create1 failed: %s", strerrordesc_np(errno));
        server_state.initialization_errors[server_state.initialization_errors_count++] = strdup(buf);
        server_state.initialization_result = true;
    }

    // Add listenfd to epoll
    struct epoll_event ev;
    struct socket_description listen_sd = { .fd = listen_fd, .type = SOCKET_TYPE_LISTEN };
    ev.events = EPOLLIN;
    ev.data.ptr = &listen_sd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
        char buf[256];
        snprintf(buf, 255, "Failed to add IPC socket to epoll: %s", strerrordesc_np(errno));
        server_state.initialization_errors[server_state.initialization_errors_count++] = strdup(buf);
        server_state.initialization_result = true;
    }

    // Add inhereted fd to epoll (if any)
    if (inhereted_fd != -1) {
        struct socket_description* inhereted_sd = calloc(1, sizeof(struct socket_description));
        ipc_connector_t* parent_ipc_connector = ipc_initialize_connector(inhereted_fd, epfd, inhereted_sd);
        struct protocol_client* parent_client = protocol_accept_connection(parent_ipc_connector);
        *inhereted_sd = (struct socket_description){
            .fd = inhereted_fd,
            .type = SOCKET_TYPE_CLIENT,
            .ipc_connector = parent_ipc_connector,
            .client = parent_client
        };
    }

    // Add wayland fd to epoll
    int wayland_fd = environment_get_display_fd();
    struct socket_description wayland_sd = { .fd = wayland_fd, .type = SOCKET_TYPE_ENVIRONMENT };
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.ptr = &wayland_sd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, wayland_fd, &ev) == -1) {
        char buf[256];
        snprintf(buf, 255, "Failed to add wayland display fd to epoll: %s", strerror(errno));
        server_state.initialization_errors[server_state.initialization_errors_count++] = strdup(buf);
        server_state.initialization_result = true;
    }

    // Now create 1 thread
    pthread_t* thread = calloc(1, sizeof(pthread_t));
    pthread_create(thread, NULL, mascot_manager_thread, NULL);

    // Main loop
    while (true) {
        if (server_state.stop) {
            break;
        }

        // Process epoll
        struct epoll_event events[16];
        int nfds = epoll_wait(epfd, events, 16, 1000);
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
                int clientfd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
                if (clientfd == -1) {
                    WARN("Cannot accept new connection from client: %s", strerror(errno));
                    continue;
                }


                // Add clientfd to epoll
                struct socket_description* client_sd = calloc(1, sizeof(struct socket_description));
                ipc_connector_t* ipc_connector = ipc_initialize_connector(clientfd, epfd, client_sd);
                struct protocol_client* client = protocol_accept_connection(ipc_connector);
                client_sd->fd = clientfd;
                client_sd->type = SOCKET_TYPE_CLIENT;
                client_sd->client = client;
                client_sd->ipc_connector = ipc_connector;
                list_add(server_state.clients, client);
                fds_count++;
            } else if (sd->type == SOCKET_TYPE_CLIENT) {
                if (events[i].events & EPOLLHUP) {
                    list_remove(server_state.clients, list_find(server_state.clients, sd->client));
                    ipc_destroy_connector(sd->ipc_connector);
                    protocol_disconnect_client(sd->client);
                    close(sd->fd);
                    free(sd);
                    fds_count--;
                    continue;
                }

                if (events[i].events & EPOLLOUT) {
                    ipc_connector_flush(sd->ipc_connector);
                    continue;
                }

                ipc_packet_t* packet = NULL;
                int receive_status = ipc_connector_receive(sd->ipc_connector, &packet);
                if (receive_status && receive_status != -EAGAIN) {
                    perror("Receive failed");
                    ERROR("EXIT ON RECEIVE STATUS");
                }

                protocol_error handler_status = protocol_client_handle_packet(sd->client, packet);
                if (handler_status == PROTOCOL_CLIENT_VIOLATION) {
                    list_remove(server_state.clients, list_find(server_state.clients, sd->client));
                    ipc_destroy_connector(sd->ipc_connector);
                    protocol_disconnect_client(sd->client);
                    close(sd->fd);
                    free(sd);
                    fds_count--;
                    continue;
                }
                ipc_free_packet(packet);
            } else if (sd->type == SOCKET_TYPE_ENVIRONMENT) {
                // Wayland event
                if (events[i].events & EPOLLHUP) {
                    LOG("ERROR", RED, "Wayland connection closed");
                    break;
                }
                if (environment_dispatch() == -1) {
                    LOG("ERROR", RED, "Can't dispatch wayland events");
                    break;
                }
            }
        }

        if (!fds_count && !mascot_total_count) {
            break;
        }
    }

    plugins_deinit();
    close(listen_fd);
    close(inhereted_fd);
    close(epfd);
    if (own_socket) unlink(socket_path);
    return 0;
}
