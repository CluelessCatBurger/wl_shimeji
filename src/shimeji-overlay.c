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
#include <bits/types/sigset_t.h>
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

            environment_store.entries[i] = environment;
            environment_store.entry_states[i] = 1;
            environment_store.used_count++;

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

            pthread_mutex_unlock(&environment_store.mutex);

            struct env_data* data = environment_get_user_data(environment);
            if (data) {
                data->stop = true;
                pthread_join(data->interpolation_thread, NULL);
                free(data);
            }

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
    INFO("Mascot %s:%u is orphaned", mascot->prototype->name, mascot->id);
    pthread_mutex_lock(&mascot_store.mutex);
    bool found = false;
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (mascot_store.entries[i] == mascot) {
            mascot_store.entries[i] = NULL;
            mascot_store.entry_states[i] = 0;
            mascot_store.used_count--;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&mascot_store.mutex);
    if (!found) {
        ERROR("Orphaned mascot not found in store");
    }

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
        pthread_mutex_unlock(&environment_store.mutex);
        mascot_unlink(mascot);
        return;
    }

    mascot_environment_changed(mascot, env);
    pthread_mutex_unlock(&environment_store.mutex);

    // Re-add mascot to store
    pthread_mutex_lock(&mascot_store.mutex);
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (!mascot_store.entry_states[i]) {
            mascot_store.entries[i] = mascot;
            mascot_store.entry_states[i] = 1;
            mascot_store.used_count++;
            pthread_mutex_unlock(&mascot_store.mutex);
            return;
        }
    }
    pthread_mutex_unlock(&mascot_store.mutex);

}

static void mascot_dropped_oob(struct mascot* mascot, int32_t x, int32_t y)
{
    if (!mascot) return;
    int32_t lx, ly;
    int32_t lw, lh;
    if (!environment_logical_position(mascot->environment, &lx, &ly)) {
        return;
    }
    if (!environment_logical_size(mascot->environment, &lw, &lh)) {
        return;
    }

    // Find output and exact position where mascot is dropped
    environment_t* env = NULL;

    int32_t ldropx = lx + x;
    int32_t ldropy = ly + y;

    int32_t elx, ely;
    int32_t elw, elh;

    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_t *target_env = environment_store.entries[i];

            if (!environment_logical_position(target_env, &elx, &ely)) {
                continue;
            }
            if (!environment_logical_size(target_env, &elw, &elh)) {
                continue;
            }

            // Compositor have logical space, where 0,0 is top left corner.
            // Each output have its own logical space, where 0,0 is top left corner of output.
            // lx, ly is offset of output in compositor space. lw, lh is size of output in compositor space
            // find if mascot within output rectangle
            if (ldropx >= elx && ldropx < elx + elw && ldropy >= ely && ldropy < ely + elh) {
                env = target_env;
                break;
            }
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);

    if (!env) {
        WARN("No environment found for mascot %s:%u", mascot->prototype->name, mascot->id);
        return;
    }


    // Find mascot coordinates in new environment rectangle
    int32_t new_x = ldropx - elx;
    int32_t new_y = ldropy - ely;
    INFO("Move to new environment %s, new coordinates %d, %d", environment_name(env), new_x, new_y);
    mascot->X->value.i = new_x;
    mascot->Y->value.i = mascot_screen_y_to_mascot_y(mascot, new_y);
    environment_subsurface_set_position(mascot->subsurface, new_x, new_y);
    environment_subsurface_reset_interpolation(mascot->subsurface);

    // Move mascot to new environment
    mascot_environment_changed(mascot, env);

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

static struct mascot* lookup_mascot_by_coords(environment_t* at_env, int32_t x, int32_t y)
{
    pthread_mutex_lock(&mascot_store.mutex);
    struct mascot* mascot = NULL;
    uint32_t score = 0;
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (mascot_store.entry_states[i]) {
            struct mascot* m = mascot_store.entries[i];
            environment_subsurface_t* subsurface = m->subsurface;
            if (environment_subsurface_get_environment(subsurface) == at_env) {
                const struct mascot_pose* pose = environment_subsurface_get_pose(subsurface);
                if (!pose) continue;
                // Check if coordinates are within mascot's pose
                // Mascot's rectangle is defined by anchor point and size
                // We also respect scaling factor of environment
                int32_t surface_anchor_point_x = m->X->value.i;
                int32_t surface_anchor_point_y = mascot_screen_y_to_mascot_y(m, m->Y->value.i);
                int32_t surface_width = pose->sprite[m->LookingRight->value.i]->width / environment_screen_scale(at_env);
                int32_t surface_height = pose->sprite[m->LookingRight->value.i]->height / environment_screen_scale(at_env);

                surface_anchor_point_x += pose->anchor_x / environment_screen_scale(at_env);
                surface_anchor_point_y += pose->anchor_y / environment_screen_scale(at_env);

                if (x >= surface_anchor_point_x && x < surface_anchor_point_x + surface_width &&
                    y >= surface_anchor_point_y && y < surface_anchor_point_y + surface_height) {
                    if ((!m->dragged_tick || m->dragged_tick > score) && !m->dragged) {
                        mascot = m;
                        score = m->dragged_tick;
                    }
                }
            }
        }
    }
    pthread_mutex_unlock(&mascot_store.mutex);
    return mascot;
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

void summon_mascot(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data)
{

    UNUSED(subsurface);
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]){
            environment_select_position(environment_store.entries[i], NULL, NULL);
            environment_set_input_state(environment_store.entries[i], false);
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);

    struct mascot_spawn_data* spawn_data = data;
    struct mascot_prototype* prototype = mascot_prototype_store_get(prototype_store, spawn_data->name);
    if (!prototype) {
        uint8_t buff[2] = {3, 1}; // Command SPAWN_RESULT, result NOT_FOUND
        send(spawn_data->fd, buff, 2, 0);
        free(data);
        return;
    }
    if (!env) {
        uint8_t buff[2] = {3, 2}; // Command SPAWN_RESULT, result INVALID_ENVIRONMENT
        send(spawn_data->fd, buff, 2, 0);
        free(data);
        return;
    }

    environment_summon_mascot(env, prototype, x, y, NULL, NULL);

    uint8_t buff[2] = {3, 0}; // Command SPAWN_RESULT, result SUCCESS
    send(spawn_data->fd, buff, 2, 0);
    free(data);
}

void remove_mascot(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data)
{
    UNUSED(x);
    UNUSED(y);
    UNUSED(env);
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_select_position(environment_store.entries[i], NULL, NULL);
            environment_set_input_state(environment_store.entries[i], false);
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);

    int fd = (uintptr_t)data;
    if (!subsurface) {
        uint8_t buff[2] = {4, 2}; // Command REMOVE_RESULT, result INVALID_SUBSURFACE
        send(fd, buff, 2, 0);
        return;
    }
    struct mascot* mascot = environment_subsurface_get_mascot(subsurface);
    if (!mascot) {
        uint8_t buff[2] = {4, 3}; // Command REMOVE_RESULT, result NO_MASCOT
        send(fd, buff, 2, 0);
        return;
    }
    environment_remove_mascot(mascot->environment, mascot);

    char buff[2] = {4, 0}; // Command REMOVE_RESULT, result SUCCESS
    send(fd, buff, 2, 0);
}

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

void select_mascot(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* data)
{
    UNUSED(x);
    UNUSED(y);
    UNUSED(env);
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_select_position(environment_store.entries[i], NULL, NULL);
            environment_set_input_state(environment_store.entries[i], false);
        }
    }

    int fd = (int)(uintptr_t)data;
    struct mascot* mascot = environment_subsurface_get_mascot(subsurface);
    if (!mascot) {
        char buff[6] = {4, 1, 0, 0, 0, 0}; // Command SELECT_RESULT, result NO_MASCOT
        send(fd, buff, 2, 0);
        return;
    }

    // Write mascot id to buffer
    char buff[6] = {0}; // Command SELECT_RESULT, result SUCCESS
    buff[0] = 4;
    buff[1] = 0;
    // Write uint32_t
    uint32_t id = mascot->id;
    memcpy(buff + 2, &id, 4);
    send(fd, buff, 6, 0);
}

// ----------------------------------------------------------------------------

void write_str(char* buf, uint16_t *len, const char* string)
{
    if (!string) {
        buf[(*len)++] = 0;
        return;
    }
    uint8_t slen = strlen(string);
    buf[(*len)++] = slen;
    memcpy(buf + *len, string, slen);
    *len += slen;
}

void write_variable(char* buf, uint16_t* len, struct mascot_local_variable* var)
{
    memcpy(buf + *len, &var->value, 4);
    *len += 4;
    buf[(*len)++] = var->kind;
    buf[(*len)++] = var->used;
    buf[(*len)++] = var->expr.evaluated;
    if (var->expr.expression_prototype) {
        buf[(*len)++] = 1;
        memcpy(buf + *len, &var->expr.expression_prototype->body->id, 2);
        *len += 2;
    } else {
        buf[(*len)++] = 0;
    }
}

void write_byte(char* buf, uint16_t* len, uint8_t byte)
{
    buf[(*len)++] = byte;
}

void write_int32(char* buf, uint16_t* len, int32_t value)
{
    memcpy(buf + *len, &value, 4);
    *len += 4;
}

void write_float(char* buf, uint16_t* len, float value)
{
    memcpy(buf + *len, &value, 4);
    *len += 4;
}

void write_long(char* buf, uint16_t* len, uint64_t value)
{
    memcpy(buf + *len, &value, 8);
    *len += 8;
}

void write_double(char* buf, uint16_t* len, double value)
{
    memcpy(buf + *len, &value, 8);
    *len += 8;
}

bool read_string(const char* buf, uint16_t* pos, char* string, uint8_t string_len)
{
    uint8_t slen = buf[(*pos)++];
    if (slen > string_len) {
        return false;
    }
    memcpy(string, buf + *pos, slen);
    string[slen] = 0;
    *pos += slen;
    return true;
}

bool read_bytes(const uint8_t* buf, uint16_t* pos, uint8_t* bytes, uint8_t byte_count)
{
    memcpy(bytes, buf + *pos, byte_count);
    *pos += byte_count;
    return true;
}

// ----------------------------------------------------------------------------

bool process_add_mascot_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len < 2) return false;
    uint8_t namelen = buff[1];
    if (len < 2 + namelen) return false;
    struct mascot_spawn_data* data = calloc(1, sizeof(struct mascot_spawn_data));
    data->fd = fd;
    memcpy(data->name, buff + 2, namelen);
    data->name[namelen] = 0;
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_select_position(environment_store.entries[i], summon_mascot, data);
            environment_set_input_state(environment_store.entries[i], true);
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);
    return true;
}

bool process_summon_mascot_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len < 2) return false;
    uint8_t namelen = buff[1];
    if (len < 2 + namelen) return false;
    struct mascot_spawn_data* data = calloc(1, sizeof(struct mascot_spawn_data));
    data->fd = fd;
    memcpy(data->name, buff + 2, namelen);
    data->name[namelen] = 0;
    pthread_mutex_lock(&environment_store.mutex);
    // Select random environment
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
    pthread_mutex_unlock(&environment_store.mutex);
    if (!env) {
        uint8_t buff[2] = {3, 2}; // Command SPAWN_RESULT, result INVALID_ENVIRONMENT
        send(fd, buff, 2, 0);
        free(data);
        return false;
    }
    int32_t x = rand() % environment_screen_width(env);
    int32_t y = environment_screen_height(env) - 256;
    summon_mascot(env, x, y, NULL, data);
    return true;
}

bool process_load_prototype_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len < 2) return false;
    uint8_t pathlen = buff[1];
    if (len < 2 + pathlen) return false;
    char path[pathlen + 1];
    memcpy(path, buff + 2, pathlen);
    path[pathlen] = '\0';

    // Check if folder exists
    struct stat st;
    if (stat(path, &st) == -1) {
        uint8_t buff[3] = {5, 1, 0}; // Command LOAD_PROTOTYPE_RESULT, result INVALID_PATH
        send(fd, buff, 2, 0);
        return true;
    }

    // Check if folder is a directory
    if (!S_ISDIR(st.st_mode)) {
        uint8_t buff[3] = {5, 2, 0}; // Command LOAD_PROTOTYPE_RESULT, result NOT_A_DIRECTORY
        send(fd, buff, 2, 0);
        return true;
    }

    enum mascot_prototype_load_result result = load_prototype(path);

    if (result == PROTOTYPE_LOAD_OOM) {
        uint8_t buff[3] = {5, 3, 0}; // Command LOAD_PROTOTYPE_RESULT, result OUT_OF_MEMORY
        send(fd, buff, 2, 0);
        return true;
    }

    if (result != PROTOTYPE_LOAD_SUCCESS) {
        uint8_t buff[3] = {5, 4, result}; // Command LOAD_PROTOTYPE_RESULT, result LOAD_FAILED, reason result
        send(fd, buff, 2, 0);
        return true;
    }

    char rbuff[3] = {5, 0, 0}; // Command LOAD_PROTOTYPE_RESULT, result SUCCESS
    send(fd, rbuff, 3, 0);
    return true;
}

bool process_config_packet(uint8_t* buff, uint16_t len, int fd)
{
    INFO("Processing config packet, len %d", len);
    if (len < 2 || len > 6) return false;
    uint8_t param = buff[1] & 0x7f; // Mask out the high bit
    uint8_t get_value = buff[1] & 0x80; // Check the high bit
    int32_t large_param;

    if (get_value) {
        char rbuff[6] = {0};
        rbuff[0] = 0xf2; // Command CONFIG_RESULT
        switch (param) {
            case CONFIG_PARAM_BREEDING_ID:
                large_param = config_get_breeding();
                break;
            case CONFIG_PARAM_DRAGGING_ID:
                large_param = config_get_dragging();
                break;
            case CONFIG_PARAM_IE_INTERACTIONS_ID:
                large_param = config_get_ie_interactions();
                break;
            case CONFIG_PARAM_IE_THROWING_ID:
                large_param = config_get_ie_throwing();
                break;
            case CONFIG_PARAM_CURSOR_DATA_ID:
                large_param = config_get_cursor_data();
                break;
            case CONFIG_PARAM_MASCOT_LIMIT_ID:
                large_param = config_get_mascot_limit();
                break;
            case CONFIG_PARAM_IE_THROW_POLICY_ID:
                large_param = config_get_ie_throw_policy();
                break;
            case CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS_ID:
                large_param = config_get_allow_dismiss_animations();
                break;
            case CONFIG_PARAM_PER_MASCOT_INTERACTIONS_ID:
                large_param = config_get_per_mascot_interactions();
                break;
            case CONFIG_PARAM_TICK_DELAY_ID:
                large_param = config_get_framerate();
                break;
            default:
                rbuff[1] = 0x81; // Status CONFIG_GET_UNKNOWN
                send(fd, rbuff, 2, 0);
                return false;
        }
        rbuff[1] = 0x80; // Status CONFIG_GET_OK
        memcpy(rbuff + 2, &large_param, 4);
        send(fd, rbuff, 6, 0);
    } else {
        char rbuff[6] = {0};
        rbuff[0] = 0xf2; // Command CONFIG_RESULT
        switch (param) {
            case CONFIG_PARAM_BREEDING_ID:
                config_set_breeding(buff[2]);
                break;
            case CONFIG_PARAM_DRAGGING_ID:
                config_set_dragging(buff[2]);
                break;
            case CONFIG_PARAM_IE_INTERACTIONS_ID:
                config_set_ie_interactions(buff[2]);
                break;
            case CONFIG_PARAM_IE_THROWING_ID:
                config_set_ie_throwing(buff[2]);
                break;
            case CONFIG_PARAM_CURSOR_DATA_ID:
                config_set_cursor_data(buff[2]);
                break;
            case CONFIG_PARAM_MASCOT_LIMIT_ID:
                memcpy(&large_param, buff + 2, 4);
                config_set_mascot_limit(large_param);
                break;
            case CONFIG_PARAM_IE_THROW_POLICY_ID:
                memcpy(&large_param, buff + 2, 4);
                config_set_ie_throw_policy(large_param);
                for (size_t i = 0; i < 256; i++) {
                    if (plugins[i]) {
                        plugin_execute_ie_throw_policy(plugins[i], large_param);
                    }
                }
                break;
            case CONFIG_PARAM_ALLOW_DISMISS_ANIMATIONS_ID:
                config_set_allow_dismiss_animations(buff[2]);
                break;
            case CONFIG_PARAM_PER_MASCOT_INTERACTIONS_ID:
                config_set_per_mascot_interactions(buff[2]);
                break;
            case CONFIG_PARAM_TICK_DELAY_ID:
                memcpy(&large_param, buff + 2, 4);
                config_set_framerate(large_param);
                break;
            default:
                rbuff[1] = 0x01; // Status CONFIG_SET_UNKNOWN
                return false;
        }
        char config_file_path[256] = {0};
        size_t printed = snprintf(config_file_path, sizeof(config_file_path), "%s/shimeji.conf", config_path);
        if (printed >= sizeof(config_file_path)) {
            ERROR("Config file path too long");
            return false;
        }
        config_write(config_file_path);
        rbuff[1] = 0x00; // Command CONFIG_RESULT, result SUCCESS
        send(fd, rbuff, 2, 0);
    }
    return true;
}

bool process_select_mascot_packet(uint8_t* buff, uint16_t len, int fd)
{
    UNUSED(buff);
    if (len < 2) return false;
    pthread_mutex_lock(&environment_store.mutex);
    for (size_t i = 0; i < environment_store.entry_count; i++) {
        if (environment_store.entry_states[i]) {
            environment_select_position(environment_store.entries[i], select_mascot, (void*)(uintptr_t)fd);
            environment_set_input_state(environment_store.entries[i], true);
        }
    }
    pthread_mutex_unlock(&environment_store.mutex);
    return true;
}

bool process_set_behavior_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len < 6) return false;
    uint32_t id;
    uint16_t bufpos = 1;
    char behavior_name[256] = {0};
    read_bytes(buff, &bufpos, (uint8_t*)&id, 4);
    bool res = read_string((const char*)buff, &bufpos, behavior_name, 255);
    if (!res) {
        uint8_t rbuff[2] = {0xc2, 1}; // Command SET_BEHAVIOR_RESULT, result INVALID_BEHAVIOR_NAME
        send(fd, rbuff, 2, 0);
        return false;
    }

    pthread_mutex_lock(&mascot_store.mutex);
    struct mascot* mascot = NULL;
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (mascot_store.entry_states[i]) {
            if (mascot_store.entries[i]->id == id) {
                mascot = mascot_store.entries[i];
                break;
            }
        }
    }

    if (!mascot) {
        pthread_mutex_unlock(&mascot_store.mutex);
        uint8_t rbuff[2] = {0xc2, 2}; // Command SET_BEHAVIOR_RESULT, result NO_MASCOT
        send(fd, rbuff, 2, 0);
        return false;
    }

    const struct mascot_behavior* behavior = mascot_prototype_behavior_by_name(mascot->prototype, behavior_name);
    if (!behavior) {
        pthread_mutex_unlock(&mascot_store.mutex);
        uint8_t rbuff[2] = {0xc2, 3}; // Command SET_BEHAVIOR_RESULT, result INVALID_BEHAVIOR_NAME
        send(fd, rbuff, 2, 0);
        return false;
    }

    mascot_set_behavior(mascot, behavior);

    pthread_mutex_unlock(&mascot_store.mutex);
    uint8_t rbuff[2] = {0xc2, 0}; // Command SET_BEHAVIOR_RESULT, result SUCCESS
    send(fd, rbuff, 2, 0);
    return true;
}

bool process_get_mascot_info_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len < 6) return false;
    uint32_t id;
    char outbuf[4096] = {0};
    uint16_t outlen = 2;
    outbuf[0] = 6;
    memcpy(&id, buff + 2, 4);
    pthread_mutex_lock(&mascot_store.mutex);
    struct mascot* mascot = NULL;
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (!mascot_store.entry_states[i]) continue;
        if (mascot_store.entries[i]->id == id) {
            mascot = mascot_store.entries[i];
            mascot_link(mascot);
            break;
        }
    }
    if (!mascot) {
         // Command GET_MASCOT_INFO_RESULT, result NO_MASCOT
        outbuf[1] = 1;
        send(fd, buff, 2, 0);
        pthread_mutex_unlock(&mascot_store.mutex);
        return true;
    }
    // Serialize mascot info and send it
    outbuf[1] = 0;
    // Write display name

    write_str(outbuf, &outlen, mascot->prototype->display_name);
    write_str(outbuf, &outlen, mascot->prototype->name);
    write_str(outbuf, &outlen, mascot->prototype->path);

    if (mascot->target_mascot) {
        write_int32(outbuf, &outlen, mascot->target_mascot->id);
    } else {
        write_int32(outbuf, &outlen, -1);
    }

    write_int32(outbuf, &outlen, mascot->action_index);
    write_int32(outbuf, &outlen, mascot->action_duration);
    write_int32(outbuf, &outlen, mascot->action_tick);
    write_int32(outbuf, &outlen, mascot->refcounter);

    // Current action name
    if (mascot->current_action.action) write_str(outbuf, &outlen, mascot->current_action.action->name);
    else write_byte(outbuf, &outlen, 0);

    // Current behavior name
    if (mascot->current_behavior) write_str(outbuf, &outlen, mascot->current_behavior->name);
    else write_byte(outbuf, &outlen, 0);

    // Current affordance
    write_str(outbuf, &outlen, mascot->current_affordance);

    // Current state
    int state = mascot->state;
    write_int32(outbuf, &outlen, state);

    write_byte(outbuf, &outlen, mascot->as_p);
    for (uint8_t i = 0; i < mascot->as_p; i++) {
        write_str(outbuf, &outlen, mascot->action_stack[i].action->name);
        write_int32(outbuf, &outlen, mascot->action_index_stack[i]);
    }

    // Dump behavior pool (behavior name + frequency)
    write_byte(outbuf, &outlen, mascot->behavior_pool_len);
    for (uint8_t i = 0; i < mascot->behavior_pool_len; i++) {
        write_str(outbuf, &outlen, mascot->behavior_pool[i].behavior->name);
        write_long(outbuf, &outlen, mascot->behavior_pool[i].frequency);
    }

    // Dump all variables of mascot (variable value, used, evaluated, type, program_id)
    write_byte(outbuf, &outlen, MASCOT_LOCAL_VARIABLE_COUNT);
    for (uint8_t i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT; i++) {
        write_variable(outbuf, &outlen, &mascot->local_variables[i]);
    }
    // Send the buffer
    send(fd, outbuf, outlen, 0);
    mascot_unlink(mascot);
    pthread_mutex_unlock(&mascot_store.mutex);
    return true;
}

#define DISMISS_ONE 0
#define DISMISS_ALL 1
#define DISMISS_ALL_OTHERS 2
#define DISMISS_ALL_OTHER_SAME_TYPE 3
#define DISMISS_ALL_OF_SAME_TYPE 4
bool process_dismiss_packet(uint8_t* buff, uint16_t len, int fd)
{
    if (len != 6) return false;
    uint8_t act = buff[1];
    uint32_t id;
    memcpy(&id, buff + 2, 4);
    pthread_mutex_lock(&mascot_store.mutex);
    // Get mascot from store if exists, else return error
    struct mascot* mascot = NULL;
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (mascot_store.entries[i]->id == id) {
            mascot = mascot_store.entries[i];
            break;
        }
    }
    if (act != DISMISS_ALL) {
        if (!mascot) {
            pthread_mutex_unlock(&mascot_store.mutex);
            char buff[2] = {7, 1};
            send(fd, buff, 2, 0);
            return false;
        }
    }

    // Iterate over all mascots and dismiss them according to act
    for (size_t i = 0; i < mascot_store.entry_count; i++) {
        if (mascot_store.entry_states[i]) {
            if (act == DISMISS_ONE) {
                if (mascot_store.entries[i]->id == id) {
                    remove_mascot(NULL, 0, 0, mascot_store.entries[i]->subsurface, (void*)(uintptr_t)-1);
                    break;
                }
            } else if (act == DISMISS_ALL) {
                remove_mascot(NULL, 0, 0, mascot_store.entries[i]->subsurface, (void*)(uintptr_t)-1);
            } else if (act == DISMISS_ALL_OTHERS) {
               if (mascot->id != mascot_store.entries[i]->id) {
                   remove_mascot(NULL, 0, 0, mascot_store.entries[i]->subsurface, (void*)(uintptr_t)-1);
               }
            } else if (act == DISMISS_ALL_OTHER_SAME_TYPE) {
                if (mascot_store.entries[i]->id != id && mascot_store.entries[i]->prototype == mascot->prototype) {
                    remove_mascot(NULL, 0, 0, mascot_store.entries[i]->subsurface, (void*)(uintptr_t)-1);
                }
            } else if (act == DISMISS_ALL_OF_SAME_TYPE) {
                if (mascot_store.entries[i]->prototype == mascot->prototype) {
                    remove_mascot(NULL, 0, 0, mascot_store.entries[i]->subsurface, (void*)(uintptr_t)-1);
                }
            }
        }
    }

    pthread_mutex_unlock(&mascot_store.mutex);
    char outbuf[2] = {7, 0};
    send(fd, outbuf, 2, 0);
    return true;

}

struct socket_description {
    int fd;
    enum {
        SOCKET_TYPE_LISTEN,
        SOCKET_TYPE_CLIENT,
        SOCKET_TYPE_ENVIRONMENT,
    } type;
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

typedef bool (packet_processor_t) (uint8_t*,uint16_t,int) ;

packet_processor_t* packet_processors[256] = {0};

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

    // Initialize environment
    environment_set_global_coordinates_searcher(find_env_by_coords);
    enum environment_init_status env_init = environment_init(env_init_flags, env_new, env_delete, orphaned_mascot, mascot_dropped_oob);
    if (env_init != ENV_INIT_OK) {
        char buf[1025+sizeof(size_t)];
        size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to initialize environment:\n%s", environment_get_error());
        memcpy(buf+1, &strlen, sizeof(size_t));
        buf[0] = 0xFF;
        send(inhereted_fd, buf, 1025+sizeof(size_t), 0);
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
        char buf[1025+sizeof(size_t)];
        size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to open mascots directory: %s", mascots_path_packages);
        memcpy(buf+1, &strlen, sizeof(size_t));
        buf[0] = 0xFF;
        send(inhereted_fd, buf, 1025+sizeof(size_t), 0);
        ERROR("Failed to open mascots directory: %s", mascots_path_packages);
    }

    // Spawn mascots if requested
    if (spawn_everything) {
        for (int i = 0; i < mascot_prototype_store_count(prototype_store); i++) {
            struct mascot_prototype* proto = mascot_prototype_store_get_index(prototype_store, i);
            struct mascot_spawn_data* data = calloc(1, sizeof(struct mascot_spawn_data));
            data->fd = -1;
            strncpy(data->name, proto->name, 127);

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
            summon_mascot(env, x, y, NULL, data);
        }
    }

    // Create epoll
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        char buf[1025+sizeof(size_t)];
        size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to create epoll");
        memcpy(buf+1, &strlen, sizeof(size_t));
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
        char buf[1025+sizeof(size_t)];
        size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to add listenfd to epoll");
        memcpy(buf+1, &strlen, sizeof(size_t));
        send(inhereted_fd, buf, 1025+sizeof(size_t), 0);
        ERROR("Failed to add listenfd to epoll");
    }

    // Add inhereted fd to epoll (if any)
    if (inhereted_fd != -1) {
        struct socket_description* inhereted_sd = calloc(1, sizeof(struct socket_description));
        *inhereted_sd= (struct socket_description){ .fd = inhereted_fd, .type = SOCKET_TYPE_CLIENT };
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.ptr = inhereted_sd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, inhereted_fd, &ev) == -1) {

            char buf[1025+sizeof(size_t)];
            size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to add inhereted fd to epoll");
            memcpy(buf+1, &strlen, sizeof(size_t));
            buf[0] = 0xFF;
            send(inhereted_fd, buf, 1025+sizeof(size_t), 0);

            ERROR("Failed to add inhereted fd to epoll");
        }
    }

    // Add wayland fd to epoll
    int wayland_fd = environment_get_display_fd();
    struct socket_description wayland_sd = { .fd = wayland_fd, .type = SOCKET_TYPE_ENVIRONMENT };
    ev.events = EPOLLIN;
    ev.data.ptr = &wayland_sd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, wayland_fd, &ev) == -1) {
        char buf[1025+sizeof(size_t)];
        size_t strlen = snprintf(buf+1+sizeof(size_t), 1024, "Failed to add wayland fd to epoll");
        memcpy(buf+1, &strlen, sizeof(size_t));
        buf[0] = 0xFF;
        send(inhereted_fd, buf, 1025+sizeof(size_t), 0);
        ERROR("Failed to add wayland fd to epoll");
    }

    // Now create 1 thread
    pthread_t* thread = calloc(1, sizeof(pthread_t));
    pthread_create(thread, NULL, mascot_manager_thread, NULL);
    thread_store.entries[0] = thread;
    thread_store.entry_states[0] = 1;
    thread_store.used_count++;

    // Finalize initialization by sending the ready message
    char readymsg[2] = {0x7f};
    ssize_t funused = write(inhereted_fd, readymsg, 1);

    packet_processors[0x1] = process_add_mascot_packet;
    packet_processors[0x3] = process_load_prototype_packet;
    // packet_processors[0x6]
    packet_processors[0x7] = process_get_mascot_info_packet;
    packet_processors[0xd] = process_dismiss_packet;
    packet_processors[0xe] = process_summon_mascot_packet;
    packet_processors[0xf] = process_select_mascot_packet;
    packet_processors[0xf1] = process_config_packet;
    packet_processors[0xc1] = process_set_behavior_packet;

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
                if (clientfd == -1) {
                    ERROR("Failed to accept new connection");
                }

                // Add clientfd to epoll
                struct socket_description* client_sd = calloc(1, sizeof(struct socket_description));
                client_sd->fd = clientfd;
                client_sd->type = SOCKET_TYPE_CLIENT;
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
                uint8_t buf[256];
                int n = recv(sd->fd, buf, 256, 0);
                if (n == -1 || n == 0) {
                    // Client disconnected
                    epoll_ctl(epfd, EPOLL_CTL_DEL, sd->fd, NULL);
                    close(sd->fd);
                    free(sd);
                    fds_count--;
                } else {
                    // Process message
                    if (buf[0] == 0x4) {
                        // stop message
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
                        return 0;
                    }
                    packet_processor_t *processor = packet_processors[buf[0]];
                    if (processor) {
                        INFO("Processing packet %d", buf[0]);
                        processor(buf, n, sd->fd);
                    } else {
                        WARN("Unknown packet %d", buf[0]);
                    }
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
    UNUSED(funused);
    return 0;
}
