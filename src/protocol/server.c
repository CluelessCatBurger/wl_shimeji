#include "server.h"
#include "environment.h"
#include "messages.h"
#include "protocol/connector.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <archive.h>
#include <archive_entry.h>
#include <io.h>

static struct protocol_server_state* server_state = NULL;

typedef bool (*protocol_packet_handler)(struct protocol_client* client, ipc_packet_t* packet);

protocol_packet_handler common_requests      [255] = {0};
protocol_packet_handler environment_requests [255] = {0};
protocol_packet_handler plugin_requests      [255] = {0};
protocol_packet_handler mascot_requests      [255] = {0};
protocol_packet_handler prototype_requests   [255] = {0};
protocol_packet_handler selection_requests   [255] = {0};
protocol_packet_handler import_requests      [255] = {0};
protocol_packet_handler export_requests      [255] = {0};
protocol_packet_handler window_requests      [255] = {0};
protocol_packet_handler shm_pool_requests    [255] = {0};
protocol_packet_handler buffer_requests      [255] = {0};
protocol_packet_handler popup_requests       [255] = {0};
protocol_packet_handler click_requests       [255] = {0};

protocol_packet_handler *object_handlers[PROTOCOL_OBJECT_TYPES_MAX] = {0};

struct protocol_selection {
    uint32_t id;
    struct protocol_client* creator;
};

struct protocol_click_event {
    uint32_t id;
    struct mascot* associated_mascot;
    bool accepted;
    struct protocol_client* accepted_by;
    environment_popup_t* created_popup;
    uint32_t x;
    uint32_t y;
};

struct protocol_import {
    uint32_t id;
    struct protocol_client* creator;
    int32_t target_descriptor;
    pthread_t thread;
    uint8_t flags;
};

struct protocol_export {
    uint32_t id;
    struct protocol_client* creator;
    int32_t target_descriptor;
};

void protocol_set_server_state(struct protocol_server_state* state)
{
    server_state = state;
}

struct protocol_server_state* protocol_get_server_state()
{
    return server_state;
}

void protocol_init()
{
    object_handlers[PROTOCOL_OBJECT_NONE] = common_requests;
    object_handlers[PROTOCOL_OBJECT_ENVIRONMENT] = environment_requests;
    object_handlers[PROTOCOL_OBJECT_PLUGIN] = plugin_requests;
    object_handlers[PROTOCOL_OBJECT_MASCOT] = mascot_requests;
    object_handlers[PROTOCOL_OBJECT_PROTOTYPE] = prototype_requests;
    object_handlers[PROTOCOL_OBJECT_SELECTION] = selection_requests;
    object_handlers[PROTOCOL_OBJECT_IMPORT] = import_requests;
    object_handlers[PROTOCOL_OBJECT_EXPORT] = export_requests;
    object_handlers[PROTOCOL_OBJECT_SHM_POOL] = shm_pool_requests;
    object_handlers[PROTOCOL_OBJECT_BUFFER] = buffer_requests;
    object_handlers[PROTOCOL_OBJECT_POPUP] = popup_requests;
    object_handlers[PROTOCOL_OBJECT_CLICK] = click_requests;

    common_requests[0x00] = protocol_handler_client_hello;
    mascot_requests[0x16] = protocol_handler_mascot_get_info;
    common_requests[0x1E] = protocol_handler_select ;
    common_requests[0x21] = protocol_handler_reload_prototype;
    common_requests[0x2B] = protocol_handler_spawn;
    mascot_requests[0x2C] = protocol_handler_dispose;
    mascot_requests[0x50] = protocol_handler_apply_behavior;
    environment_requests[0x2E] = protocol_handler_environment_close;
    selection_requests[0x3D] = protocol_handler_selection_cancel;
    common_requests[0x22] = protocol_handler_import;
}

struct protocol_client* protocol_accept_connection(ipc_connector_t *connector)
{
    if (!connector) return NULL;

    struct protocol_client *client = calloc(1, sizeof(struct protocol_client));
    if (!client) return NULL;

    client->connector = connector;
    client->objects = list_init(64);

    return client;
}

void protocol_disconnect_client(struct protocol_client* client)
{
    if (!client) return;

    for (uint32_t i = 0; i < list_size(client->objects); i++) {
        struct protocol_object* object = list_get(client->objects, i);
        if (!object) continue;

        switch (object->type) {
            case PROTOCOL_OBJECT_SELECTION:
                TRACE("Cleared selection of deleted client");
                if (object->data == server_state->active_selection) {
                    server_state->active_selection = NULL;
                    pthread_mutex_lock(&server_state->environment_mutex);
                    for (uint32_t j = 0; j < list_size(server_state->environments); j++) {
                        environment_t* env = list_get(server_state->environments, j);
                        if (!env) continue;
                        environment_select_position(env, NULL, NULL);
                    }
                    pthread_mutex_unlock(&server_state->environment_mutex);
                }
                free(object->data);
                TRACE("Cleared selection of deleted client");
                break;
            default:
                break;
        }

        free(object);
    }

    list_free(client->objects);
    free(client);
}

void protocol_server_broadcast_packet(ipc_packet_t* packet) {
    if (!server_state) {
        WARN("[BUG] Broadcast called before server setup");
        return;
    }

    if (!packet) return;

    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;

        c--;

        ipc_connector_send(client->connector, packet);
    }
}


protocol_error protocol_client_handle_packet(struct protocol_client* client, ipc_packet_t* packet)
{
    if (!client || !packet) return PROTOCOL_INVALID_ARGUMENT;

    bool handled = PROTOCOL_CLIENT_UNHANDLED;
    uint8_t opcode = ipc_packet_get_type(packet);
    uint32_t object_id = ipc_packet_get_object(packet);

    protocol_object_type_t object_type = (object_id >> 24);

    if (object_type > PROTOCOL_OBJECT_TYPES_MAX) return PROTOCOL_CLIENT_UNHANDLED;

    if (!client->initialized && opcode != 0) {
        WARN("Client %p attempted to send a packet before initialization, opcode %d", client, opcode);
        return PROTOCOL_CLIENT_VIOLATION;
    }

    protocol_packet_handler handler = object_handlers[object_type][opcode];
    if (!handler) {
        WARN("Unhandled opcode %x for object type %d", opcode, object_type);
        return PROTOCOL_CLIENT_UNHANDLED;
    }

    handled = handler(client, packet);

    return handled ? PROTOCOL_CLIENT_HANDLED : PROTOCOL_CLIENT_VIOLATION;
}

void protocol_server_announce_new_environment(environment_t* environment, struct protocol_client* client)
{
    if (!environment) return;

    struct protocol_server_state* server_state = protocol_get_server_state();

    if (client) {
        pthread_mutex_t* mascots_mutex;
        struct list* mascots = environment_mascot_list(environment, &mascots_mutex);

        ipc_packet_t* announcement = protocol_builder_environment(environment);
        ipc_connector_send(client->connector, announcement);

        pthread_mutex_lock(mascots_mutex);
        for (uint32_t i = 0, c = list_count(mascots); i < list_size(mascots) && c; i++) {
            struct mascot* mascot = list_get(mascots, i);
            if (!mascot) continue;
            c--;
            ipc_packet_t* mascot_announcement = protocol_builder_environment_mascot(environment, mascot);
            ipc_connector_send(client->connector, mascot_announcement);
        }
        pthread_mutex_unlock(mascots_mutex);
    } else {
        pthread_mutex_lock(&server_state->clients_mutex);
        for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
            struct protocol_client* client = list_get(server_state->clients, i);
            if (!client) continue;
            c--;
            protocol_server_announce_new_environment(environment, client);
        }
        pthread_mutex_unlock(&server_state->clients_mutex);

    }
}

void protocol_server_environment_widthdraw(environment_t* environment)
{
    if (!environment) return;

    struct protocol_server_state* server_state = protocol_get_server_state();


    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_packet_t* announcement = protocol_builder_environment_withdrawn(environment);
        ipc_connector_send(client->connector, announcement);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

void protocol_server_environment_changed(environment_t* environment)
{
    if (!environment) return;

    struct protocol_server_state* server_state = protocol_get_server_state();


    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_packet_t* announcement = protocol_builder_environment_changed(environment);
        ipc_connector_send(client->connector, announcement);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

void protocol_server_environment_emit_mascot(environment_t* environment, struct mascot* mascot)
{
    if (!environment || !mascot) return;

    struct protocol_server_state* server_state = protocol_get_server_state();


    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_packet_t* announcement = protocol_builder_environment_mascot(environment, mascot);
        ipc_connector_send(client->connector, announcement);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

void protocol_server_announce_new_prototype(struct mascot_prototype* prototype, struct protocol_client* client)
{
    if (!prototype) return;

    struct protocol_server_state* server_state = protocol_get_server_state();

    if (client) {
        ipc_packet_t* prototype_packet = protocol_builder_prototype(prototype);
        ipc_packet_t* prototype_name_packet = protocol_builder_prototype_name(prototype);
        ipc_packet_t* prototype_display_name_packet = protocol_builder_prototype_display_name(prototype);
        ipc_packet_t* prototype_path_packet = protocol_builder_prototype_path(prototype);
        ipc_packet_t* prototype_fd_packet = protocol_builder_prototype_fd(prototype);
        ipc_connector_send(client->connector, prototype_packet);
        ipc_connector_send(client->connector, prototype_name_packet);
        ipc_connector_send(client->connector, prototype_display_name_packet);
        ipc_connector_send(client->connector, prototype_path_packet);
        ipc_connector_send(client->connector, prototype_fd_packet);
        for (uint16_t i = 0; i < prototype->actions_count; i++) {
            ipc_packet_t* action_packet = protocol_builder_prototype_action(prototype, prototype->action_definitions[i]);
            ipc_connector_send(client->connector, action_packet);
        }
        for (uint16_t i = 0; i < prototype->behavior_count; i++) {
            ipc_packet_t* behavior_packet = protocol_builder_prototype_behavior(prototype, prototype->behavior_definitions[i]);
            ipc_connector_send(client->connector, behavior_packet);
        }
    } else {
        pthread_mutex_lock(&server_state->clients_mutex);
        for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
            struct protocol_client* client = list_get(server_state->clients, i);
            if (!client) continue;
            c--;
            protocol_server_announce_new_prototype(prototype, client);
            ipc_packet_t* commit_prototypes = protocol_builder_commit_prototypes();
            ipc_connector_send(client->connector, commit_prototypes);
        }
        pthread_mutex_unlock(&server_state->clients_mutex);

    }
}

void protocol_server_mascot_migrated(struct mascot *mascot, environment_t *new_environment)
{
    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_packet_t* migration = protocol_builder_mascot_migrated(mascot, new_environment);
        ipc_connector_send(client->connector, migration);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

void protocol_server_mascot_destroyed(struct mascot *mascot)
{
    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_packet_t* dispose_event = protocol_builder_mascot_disposed(mascot);
        ipc_connector_send(client->connector, dispose_event);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

void protocol_server_prototype_withdraw(struct mascot_prototype* prototype) {
    ipc_packet_t* withdraw_event = protocol_builder_prototype_withdrawn(prototype);
    pthread_mutex_lock(&server_state->clients_mutex);
    for (uint32_t i = 0, c = list_count(server_state->clients); i < list_size(server_state->clients) && c; i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (!client) continue;
        c--;
        ipc_connector_send(client->connector, withdraw_event);
    }
    pthread_mutex_unlock(&server_state->clients_mutex);
}

uint32_t protocol_import_id(struct protocol_import *import)
{
    return import->id;
}

uint32_t protocol_export_id(struct protocol_export *export)
{
    return export->id;
}

uint32_t protocol_selection_id(protocol_selection_t *selection)
{
    return selection->id;
}

uint32_t protocol_click_event_id(protocol_click_event_t *click_event)
{
    return click_event->id;
}

static void protocol_selected_callback(environment_t* environment, int32_t x, int32_t y, environment_subsurface_t* subsurface, void* userdata)
{
    protocol_selection_t* selection = userdata;
    struct mascot* mascot = environment_subsurface_get_mascot(subsurface);
    ipc_packet_t* selected = protocol_builder_selection_done(selection, mascot, environment, x, y, x, y);
    ipc_connector_send(selection->creator->connector, selected);
    free(protocol_client_remove_object(selection->creator, selection->id));
    server_state->active_selection = NULL;

    pthread_mutex_lock(&server_state->environment_mutex);
    for (uint32_t i = 0; i < list_size(server_state->environments); i++) {
        environment_t* env = list_get(server_state->environments, i);
        if (!env) continue;
        environment_select_position(env, NULL, NULL);
    }
    pthread_mutex_unlock(&server_state->environment_mutex);

}

protocol_selection_t* protocol_server_start_selection(struct list* environments, struct protocol_client* author, uint32_t id)
{
    if (!environments || !author || !id) return NULL;
    if (!list_count(environments)) return NULL;
    if ((id & 0xFF000000) != (PROTOCOL_OBJECT_SELECTION << 24)) return NULL;

    if (server_state->active_selection) {
        if (id == server_state->active_selection->id) {
            ipc_packet_t* notice = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "protocol.core.error.duplicate.object", NULL, 0, false);
            ipc_connector_send(author->connector, notice);
            return NULL;
        }
        protocol_selection_t* old_selection = server_state->active_selection;
        struct protocol_client* old_author = old_selection->creator;
        ipc_packet_t* cancelled = protocol_builder_selection_canceled(old_selection);
        ipc_connector_send(old_author->connector, cancelled);
        protocol_client_remove_object(old_author, old_selection->id);
        free(old_selection);
    }

    protocol_selection_t* selection = calloc(1, sizeof(protocol_selection_t));
    if (!selection) return NULL;
    selection->id = id;
    selection->creator = author;

    if (protocol_client_push_object(author, id, PROTOCOL_OBJECT_SELECTION, selection)) {
        free(selection);
        return NULL;
    }

    for (uint32_t i = 0; i < list_size(environments); i++) {
        environment_t* env = list_get(environments, i);
        if (!env) continue;
        environment_select_position(env, protocol_selected_callback, selection);
    }

    server_state->active_selection = selection;
    return selection;
}

void protocol_selection_cancel(protocol_selection_t* selection)
{
    if (!selection) return;
    if (server_state->active_selection != selection) return;
    ipc_packet_t* cancelled = protocol_builder_selection_canceled(selection);
    ipc_connector_send(selection->creator->connector, cancelled);
    server_state->active_selection = NULL;
    pthread_mutex_lock(&server_state->environment_mutex);
    for (uint32_t i = 0; i < list_size(server_state->environments); i++) {
        environment_t* env = list_get(server_state->environments, i);
        if (!env) continue;
        environment_select_position(env, NULL, NULL);
    }
    pthread_mutex_unlock(&server_state->environment_mutex);
    free(selection);
    return;
}

int32_t protocol_client_push_object(struct protocol_client* client, uint32_t id, uint32_t type, void* object) {
    if (!client || !object) return -1;

    if (protocol_client_find_object(client, id)) {
        WARN("Object with ID %u already exists", id);
        return -1;
    }

    if ((id & 0xFF000000) != (type << 24)) {
        WARN("Invalid object ID for type %u", type);
        WARN("Expected type %u, got %u", type << 24, id & 0xFF000000);
        return -1;
    }

    struct protocol_object* protocol_object = calloc(1, sizeof(struct protocol_object));
    if (!protocol_object) return -1;

    protocol_object->id = id;
    protocol_object->type = type;
    protocol_object->data = object;

    list_add(client->objects, protocol_object);
    return 0;
}

void* protocol_client_find_object(struct protocol_client* client, uint32_t id) {
    if (!client) return NULL;

    for (uint32_t i = 0; i < list_size(client->objects); i++) {
        struct protocol_object* protocol_object = list_get(client->objects, i);
        if (protocol_object && protocol_object->id == id) {
            return protocol_object->data;
        }
    }
    return NULL;
}

void* protocol_client_remove_object(struct protocol_client* client, uint32_t id) {
    if (!client) return NULL;

    for (uint32_t i = 0; i < list_size(client->objects); i++) {
        struct protocol_object* protocol_object = list_get(client->objects, i);
        if (protocol_object) {
            TRACE("Checking object id %u", protocol_object->id);
            if (protocol_object->id != id) continue;
            void* data = protocol_object->data;
            list_remove(client->objects, i);
            free(protocol_object);
            return data;
        }
    }
    INFO("Id %u not found in client's object list", id);
    return NULL;
}

protocol_click_event_t* protocol_server_announce_new_click(struct mascot* mascot, uint32_t x, uint32_t y)
{
    static uint32_t click_id = 0;
    protocol_click_event_t* event = calloc(1, sizeof(protocol_click_event_t));
    if (!event) return NULL;

    event->id = click_id++;
    event->associated_mascot = mascot;
    event->accepted = false;
    event->accepted_by = NULL;
    event->created_popup = NULL;
    event->x = x;
    event->y = y;

    for (uint32_t i = 0; i < list_size(server_state->clients); i++) {
        struct protocol_client* client = list_get(server_state->clients, i);
        if (client) {
            ipc_packet_t* expire_event = NULL;
            ipc_packet_t* add_new = protocol_builder_mascot_clicked(mascot, event);
            if (server_state->last_click_event) {
                expire_event = protocol_builder_click_event_expired(server_state->last_click_event);
                if (client == server_state->last_click_event->accepted_by && server_state->last_click_event->accepted) {
                    ipc_free_packet(expire_event);
                    ipc_free_packet(add_new);
                    continue;
                }
            }
            if (expire_event) ipc_connector_send(client->connector, expire_event);
            ipc_connector_send(client->connector, add_new);
        }
    }

    server_state->last_click_event = event;

    return event;
}

void protocol_server_click_accept(struct protocol_client* client, uint32_t popup_width, uint32_t popup_height, uint32_t id)
{
    if (!client || !server_state->last_click_event) return;

    if (server_state->last_click_event->accepted) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "accept.click_event.expired", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return;
    }

    server_state->last_click_event->accepted = true;
    server_state->last_click_event->accepted_by = client;

    environment_popup_t* popup = environment_popup_create(server_state->last_click_event->associated_mascot->environment, server_state->last_click_event->associated_mascot, server_state->last_click_event->x, server_state->last_click_event->y, popup_width, popup_height);
    if (!popup) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "accept.click_event.failed", NULL, 0, false);
        ipc_connector_send(client->connector, error);
    }
    server_state->last_click_event->created_popup = popup;
    protocol_client_push_object(client, server_state->last_click_event->id | (PROTOCOL_OBJECT_CLICK << 24), PROTOCOL_OBJECT_CLICK, server_state->last_click_event);
    protocol_client_push_object(client, id | (PROTOCOL_OBJECT_POPUP << 24), PROTOCOL_OBJECT_CLICK, popup);


    for (uint32_t i = 0; i < list_size(server_state->clients); i++) {
        struct protocol_client* _client = list_get(server_state->clients, i);
        if (_client) {
            ipc_packet_t* expire_event = expire_event = protocol_builder_click_event_expired(server_state->last_click_event);
            if (client == _client) {
                continue;
            }
            if (expire_event) ipc_connector_send(client->connector, expire_event);
        }
    }
}

void* protocol_import_thread(void* arg);

protocol_import_t* protocol_server_import(struct protocol_client* client, int32_t fd, uint32_t id, uint8_t flags)
{
    TRACE("IMPORT -1");

    if (!client || !id || fd < 0) return NULL;

    TRACE("IMPORT 0");

    protocol_import_t* import = calloc(1,sizeof(protocol_import_t));
    if (!import) ERROR("Failed to allocate memory for import");

    int dupfd = dup(fd);
    if (dupfd < 0) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "import.error.dup_failed", NULL, 0, true);
        ipc_connector_send(client->connector, error);
        ipc_packet_t* failed = protocol_builder_import_failed(import, 0);
        ipc_connector_send(client->connector, failed);
        free(import);
        return NULL;
    }

    TRACE("IMPORT 1");

    int32_t fd_flags = fcntl(fd, F_GETFL);
    if (fd_flags == -1) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 0);
        ipc_connector_send(client->connector, error);
        free(import);
        close(dupfd);
        return NULL;
    }

    TRACE("IMPORT 2");

    if (!((fd_flags & O_ACCMODE) == O_RDWR ) && !((fd_flags & O_ACCMODE) == O_RDONLY)) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 7);
        ipc_connector_send(client->connector, error);
        free(import);
        close(dupfd);
        return NULL;
    }


    TRACE("IMPORT 3");

    struct stat loc;
    if (fstat(fd, &loc) == -1) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 0);
        ipc_connector_send(client->connector, error);
        close(dupfd);
        free(import);
        return NULL;
    }
    if (!(loc.st_mode & S_IFREG)) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 6);
        ipc_connector_send(client->connector, error);
        free(import);
        close(dupfd);
        return NULL;
    }

    import->target_descriptor = dupfd;
    import->creator = client;
    import->id = id;
    import->flags = flags;

    if (protocol_client_push_object(client, id, PROTOCOL_OBJECT_IMPORT, import)) {
        free(import);
        close(dupfd);
        return NULL;
    }

    pthread_create(&import->thread, NULL, protocol_import_thread, (void*)import);
    pthread_detach(import->thread);

    return import;
}

void* protocol_import_thread(void* arg) {
    protocol_import_t* import = (protocol_import_t*)arg;
    struct protocol_client* client = import->creator;

    ipc_packet_t* started = protocol_builder_import_started(import);
    ipc_connector_send(client->connector, started);

    // wlshm header is 512-byte header
    uint8_t wlshm_header[512];
    int32_t count = read(import->target_descriptor, wlshm_header, sizeof(wlshm_header));
    if (count != 512) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 1);
        ipc_connector_send(client->connector, error);
        protocol_client_remove_object(client, import->id);
        close(import->target_descriptor);
        free(import);
        return NULL;
    }

    if (wlshm_header[0] != 'W' || wlshm_header[1] != 'L' || wlshm_header[2] != 'P' || wlshm_header[3] != 'K') {
        ipc_packet_t* error = protocol_builder_import_failed(import, 1);
        ipc_connector_send(client->connector, error);
        protocol_client_remove_object(client, import->id);
        close(import->target_descriptor);
        free(import);
        return NULL;
    }

    char mascot_name[256] = {0};
    char dirname[PATH_MAX] = {0};
    char tmpdirname[] = ".unpackingMascotXXXXXX";
    uint8_t namesize = wlshm_header[4];
    mkstemp(tmpdirname);
    memcpy(mascot_name, wlshm_header + 5, namesize);
    memcpy(dirname, wlshm_header + 5, namesize);

    struct archive* archive = archive_read_new();
    archive_read_support_format_tar(archive);
    archive_read_support_filter_zstd(archive);

    int32_t r = archive_read_open_fd(archive, import->target_descriptor, 10240);
    if (r != ARCHIVE_OK) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 4);
        ipc_connector_send(client->connector, error);
        protocol_client_remove_object(client, import->id);
        close(import->target_descriptor);
        free(import);
        archive_read_free(archive);
        return NULL;
    }

    const struct mascot_prototype* old_prototype = mascot_prototype_store_get(server_state->prototypes, mascot_name);
    if (old_prototype) {
        if (!(import->flags & 1)) { // Replace flag
            ipc_packet_t* error = protocol_builder_import_failed(import, 3);
            ipc_connector_send(client->connector, error);
            protocol_client_remove_object(client, import->id);
            close(import->target_descriptor);
            free(import);
            return NULL;
        }
        snprintf(dirname, PATH_MAX, "%s", old_prototype->path);
    } else {
        struct stat st;
        int16_t retries_count = 1;
        while (!fstatat(mascot_prototype_store_get_fd(server_state->prototypes), dirname, &st, AT_SYMLINK_NOFOLLOW)) {
            if (retries_count > 4096) {
                ipc_packet_t* error = protocol_builder_import_failed(import, 3);
                ipc_connector_send(client->connector, error);
                protocol_client_remove_object(client, import->id);
                close(import->target_descriptor);
                free(import);
                return NULL;
            }
            snprintf(dirname, PATH_MAX, "%s (%d)", mascot_name, retries_count++);
        }
    }

    if (mkdirat(mascot_prototype_store_get_fd(server_state->prototypes), tmpdirname, 0755) == -1) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 0);
        ipc_connector_send(client->connector, error);
        protocol_client_remove_object(client, import->id);
        close(import->target_descriptor);
        free(import);
        archive_read_free(archive);
        return NULL;
    }

    int32_t newfd = openat(mascot_prototype_store_get_fd(server_state->prototypes), tmpdirname, O_DIRECTORY);
    if (newfd == -1) {
        ipc_packet_t* error = protocol_builder_import_failed(import, 0);
        ipc_connector_send(client->connector, error);
        protocol_client_remove_object(client, import->id);
        close(import->target_descriptor);
        free(import);
        archive_read_free(archive);
        return NULL;
    }

    struct archive_entry* entry = NULL;

    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
        if ((archive_entry_pathname(entry)[strlen(archive_entry_pathname(entry))-1]) != '/') {
            int32_t writefd = openat(newfd, archive_entry_pathname(entry), O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            archive_read_data_into_fd(archive, writefd);
            close(writefd);
        } else {
            mkdirat(newfd, archive_entry_pathname(entry), 0755);
        }
    }

    if (old_prototype) {
        io_swapat(mascot_prototype_store_get_fd(server_state->prototypes), tmpdirname, mascot_prototype_store_get_fd(server_state->prototypes), old_prototype->path);
        io_unlinkat(mascot_prototype_store_get_fd(server_state->prototypes), tmpdirname, IO_RECURSIVE);
    } else {
        io_renameat(mascot_prototype_store_get_fd(server_state->prototypes), tmpdirname, mascot_prototype_store_get_fd(server_state->prototypes), dirname, 0);
    }

    close(newfd);
    archive_read_free(archive);

    ipc_packet_t* success = protocol_builder_import_finished(import, mascot_name);
    ipc_connector_send(client->connector, success);
    protocol_client_remove_object(client, import->id);
    close(import->target_descriptor);
    free(import);

    return NULL;
}
