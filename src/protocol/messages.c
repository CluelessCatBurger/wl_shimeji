#include "messages.h"
#include "environment.h"
#include "list.h"
#include "mascot.h"
#include "mascot_config_parser.h"
#include "physics.h"
#include "protocol/connector.h"
#include "protocol/server.h"
#include <master_header.h>
#include <string.h>
#include <sys/mman.h>

#define ENSURE_MARSHALLER(x) if (x) return false

ipc_packet_t* protocol_builder_server_hello()
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    ipc_packet_set_type(packet, 0x01);
    return packet;
}

ipc_packet_t* protocol_builder_disconnect()
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    ipc_packet_set_type(packet, 0x02);
    return packet;
}

ipc_packet_t* protocol_builder_notice(uint8_t severity, const char* message, const char** format_parts, uint8_t format_parts_count, bool alert)
{
    ipc_packet_t* packet = ipc_allocate_packet(4096);
    ipc_packet_set_type(packet, 0x03);
    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, severity));
    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, alert));
    ENSURE_MARSHALLER(ipc_packet_write_string(packet, message));
    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, format_parts_count));
    for (uint8_t i = 0; i < format_parts_count; i++) {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, format_parts[i]));
    }
    return packet;
}

ipc_packet_t* protocol_builder_start()
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    ipc_packet_set_type(packet, 0x04);
    return packet;
}

ipc_packet_t* protocol_builder_environment(environment_t* environment)
{
    ipc_packet_t* packet = ipc_allocate_packet(2048);
    ipc_packet_set_type(packet, 0x05);
    uint32_t object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);
    struct bounding_box *local_geo, *global_geo;
    local_geo = environment_local_geometry(environment);
    global_geo = environment_global_geometry(environment);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, object_id));
    ENSURE_MARSHALLER(ipc_packet_write_string(packet, environment_name(environment)));
    ENSURE_MARSHALLER(ipc_packet_write_string(packet, environment_desc(environment)));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, global_geo->x));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, global_geo->y));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, local_geo->width));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, local_geo->height));
    ENSURE_MARSHALLER(ipc_packet_write_float(packet, environment_screen_scale(environment)));

    return packet;
}

ipc_packet_t* protocol_builder_environment_changed(environment_t* environment)
{
    ipc_packet_t* packet = ipc_allocate_packet(2048);
    ipc_packet_set_type(packet, 0x06);
    uint32_t object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);
    struct bounding_box *local_geo, *global_geo;
    local_geo = environment_local_geometry(environment);
    global_geo = environment_global_geometry(environment);

    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, environment_name(environment)));
    ENSURE_MARSHALLER(ipc_packet_write_string(packet, environment_desc(environment)));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, global_geo->x));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, global_geo->y));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, local_geo->width));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, local_geo->height));
    ENSURE_MARSHALLER(ipc_packet_write_float(packet, environment_screen_scale(environment)));

    return packet;
}

ipc_packet_t* protocol_builder_environment_mascot(environment_t* environment, struct mascot* mascot)
{
    ipc_packet_t* packet = ipc_allocate_packet(16);
    uint32_t object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);
    uint32_t mascot_object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);
    uint32_t prototype_object_id = (mascot->prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x07);
    ipc_packet_set_object(packet, object_id);
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, mascot_object_id));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, prototype_object_id));

    return packet;
}

ipc_packet_t* protocol_builder_environment_withdrawn(environment_t* environment)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);

    ipc_packet_set_type(packet, 0x08);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_prototype(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(8);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x09);
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, object_id));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_name(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(256);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0A);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, prototype->name));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_display_name(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(256);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0B);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, prototype->display_name));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_path(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(256);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0C);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, prototype->path));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_fd(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0D);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_put_fd(packet, prototype->path_fd));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_action(struct mascot_prototype* prototype, const struct mascot_action* action)
{
    ipc_packet_t* packet = ipc_allocate_packet(268);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0E);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, action->name));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, action->type));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, action->embedded_type));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, action->border_type));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_behavior(struct mascot_prototype* prototype, const struct mascot_behavior* behavior)
{
    ipc_packet_t* packet = ipc_allocate_packet(266);
    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x0F);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, behavior->name));
    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, behavior->is_condition));
    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, 0));
    ENSURE_MARSHALLER(ipc_packet_write_uint64(packet, behavior->frequency));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_icon(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(256);

    uint32_t object_id = (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);

    ipc_packet_set_type(packet, 0x10);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, NULL));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_author(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_version(struct mascot_prototype* prototype);

ipc_packet_t* protocol_builder_commit_prototypes()
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    ipc_packet_set_type(packet, 0x13);

    return packet;
}


ipc_packet_t* protocol_builder_mascot_migrated(struct mascot* mascot, environment_t* environment)
{
    ipc_packet_t* packet = ipc_allocate_packet(8);
    uint32_t object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);
    uint32_t environment_object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);

    ipc_packet_set_type(packet, 0x14);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, environment_object_id));

    return packet;
}

ipc_packet_t* protocol_builder_mascot_disposed(struct mascot* mascot)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);

    ipc_packet_set_type(packet, 0x15);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_mascot_info(struct mascot* mascot)
{
    ipc_packet_t* packet = ipc_allocate_packet(4096);

    uint32_t object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);
    uint32_t prototype_object_id = (mascot->prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24);
    uint32_t environment_object_id = (environment_id(mascot->environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);

    ipc_packet_set_type(packet, 0x17);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, prototype_object_id));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, environment_object_id));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, mascot->state));
    if (mascot->current_action.action) {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, mascot->current_action.action->name));
    } else {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, ""));
    }
    ENSURE_MARSHALLER(ipc_packet_write_uint16(packet, mascot->action_index));

    if (mascot->current_behavior) {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, mascot->current_behavior->name));
    }
    else {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, ""));
    }

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, mascot->current_affordance));

    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, mascot->as_p));
    for (int i = 0; i < mascot->as_p; i++) {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, mascot->action_stack[i].action->name));
        ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, mascot->action_index_stack[i]));
    }

    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, mascot->behavior_pool_len));
    for (int i = 0; i < mascot->behavior_pool_len; i++) {
        ENSURE_MARSHALLER(ipc_packet_write_string(packet, mascot->behavior_pool[i].behavior->name));
        ENSURE_MARSHALLER(ipc_packet_write_uint64(packet, mascot->behavior_pool[i].frequency));
    }

    ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, MASCOT_LOCAL_VARIABLE_COUNT));
    for (int i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT+1; i++) {
        ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, mascot->local_variables[i].kind));
        ENSURE_MARSHALLER(ipc_packet_copy_to(packet, &mascot->local_variables[i].value, 4));
        ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, mascot->local_variables[i].used));
        if (mascot->local_variables[i].expr.expression_prototype) {
            ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, mascot->local_variables[i].expr.expression_prototype->evaluate_once));
            ENSURE_MARSHALLER(ipc_packet_write_uint16(packet, mascot->local_variables[i].expr.expression_prototype->body->id));
        } else {
            ENSURE_MARSHALLER(ipc_packet_write_uint8(packet, 0));
            ENSURE_MARSHALLER(ipc_packet_write_uint16(packet, 0));
        }
    }

    return packet;
}

ipc_packet_t* protocol_builder_mascot_clicked(struct mascot* mascot, protocol_click_event_t* event)
{
    ipc_packet_t* packet = ipc_allocate_packet(4);

    uint32_t object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);
    uint32_t click_event_object_id = (protocol_click_event_id(event) & 0x00FFFFFF) | (PROTOCOL_OBJECT_CLICK << 24);

    ipc_packet_set_type(packet, 0x18);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, click_event_object_id));

    return packet;
}

ipc_packet_t* protocol_builder_selection_done(protocol_selection_t* selection, struct mascot* mascot, environment_t* environment, uint32_t x, uint32_t y, uint32_t locx, uint32_t locy)
{
    ipc_packet_t* packet = ipc_allocate_packet(24);

    uint32_t object_id = (protocol_selection_id(selection) & 0x00FFFFFF) | (PROTOCOL_OBJECT_SELECTION << 24);
    uint32_t mascot_object_id = 0;
    uint32_t environment_object_id = 0;

    if (mascot) mascot_object_id = (mascot->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_MASCOT << 24);
    if (environment) environment_object_id = (environment_id(environment) & 0x00FFFFFF) | (PROTOCOL_OBJECT_ENVIRONMENT << 24);

    ipc_packet_set_type(packet, 0x1F);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, environment_object_id));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, mascot_object_id));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, x));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, y));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, locx));
    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, locy));

    return packet;
}

ipc_packet_t* protocol_builder_selection_canceled(protocol_selection_t* selection)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (protocol_selection_id(selection) & 0x00FFFFFF) | (PROTOCOL_OBJECT_SELECTION << 24);

    ipc_packet_set_type(packet, 0x20);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_import_started(protocol_import_t* import)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (protocol_import_id(import) & 0x00FFFFFF) | (PROTOCOL_OBJECT_IMPORT << 24);

    ipc_packet_set_type(packet, 0x24);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_import_finished(protocol_import_t* import, const char* path)
{
    ipc_packet_t* packet = ipc_allocate_packet(256);
    uint32_t object_id = (protocol_import_id(import) & 0x00FFFFFF) | (PROTOCOL_OBJECT_IMPORT << 24);

    ipc_packet_set_type(packet, 0x25);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, path));

    return packet;
}

ipc_packet_t* protocol_builder_import_progress(protocol_import_t* import, float progress)
{
    ipc_packet_t* packet = ipc_allocate_packet(4);
    uint32_t object_id = (protocol_import_id(import) & 0x00FFFFFF) | (PROTOCOL_OBJECT_IMPORT << 24);

    ipc_packet_set_type(packet, 0x26);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_float(packet, progress));

    return packet;
}

ipc_packet_t* protocol_builder_import_failed(protocol_import_t* import, uint32_t error_code)
{
    ipc_packet_t* packet = ipc_allocate_packet(4);
    uint32_t object_id = (protocol_import_id(import) & 0x00FFFFFF) | (PROTOCOL_OBJECT_IMPORT << 24);

    ipc_packet_set_type(packet, 0x23);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, error_code));

    return packet;
}

ipc_packet_t* protocol_builder_export_failed(protocol_export_t* export, uint32_t error_code)
{
    ipc_packet_t* packet = ipc_allocate_packet(4);
    uint32_t object_id = (protocol_export_id(export) & 0x00FFFFFF) | (PROTOCOL_OBJECT_EXPORT << 24);

    ipc_packet_set_type(packet, 0x28);
    ipc_packet_set_object(packet, object_id);

    ENSURE_MARSHALLER(ipc_packet_write_uint32(packet, error_code));

    return packet;
}

ipc_packet_t* protocol_builder_export_finished(protocol_export_t* export)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (protocol_export_id(export) & 0x00FFFFFF) | (PROTOCOL_OBJECT_EXPORT << 24);

    ipc_packet_set_type(packet, 0x29);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_click_event_expired(protocol_click_event_t* event)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);
    uint32_t object_id = (protocol_click_event_id(event) & 0x00FFFFFF) | (PROTOCOL_OBJECT_CLICK << 24);

    ipc_packet_set_type(packet, 0x55);
    ipc_packet_set_object(packet, object_id);

    return packet;
}

ipc_packet_t* protocol_builder_config_key(const char* key, const char* value)
{
    ipc_packet_t* packet = ipc_allocate_packet(512);

    ipc_packet_set_type(packet, 0x54);

    ENSURE_MARSHALLER(ipc_packet_write_string(packet, key));
    ENSURE_MARSHALLER(ipc_packet_write_string(packet, value));

    return packet;
}

ipc_packet_t* protocol_builder_prototype_withdrawn(struct mascot_prototype* prototype)
{
    ipc_packet_t* packet = ipc_allocate_packet(0);

    ipc_packet_set_type(packet, 0x57);
    ipc_packet_set_object(packet, (prototype->id & 0x00FFFFFF) | (PROTOCOL_OBJECT_PROTOTYPE << 24));

    return packet;
}

ipc_packet_t* protocol_builder_shm_pool_failed(protocol_shm_pool_t* pool) {
    ipc_packet_t* packet = ipc_allocate_packet(0);

    ipc_packet_set_type(packet, 0x41);
    ipc_packet_set_object(packet, protocol_shm_pool_id(pool));

    return packet;
}

ipc_packet_t* protocol_builder_shm_pool_imported(protocol_shm_pool_t* pool) {
    ipc_packet_t* packet = ipc_allocate_packet(0);

    ipc_packet_set_type(packet, 0x40);
    ipc_packet_set_object(packet, protocol_shm_pool_id(pool));

    return packet;
}

ipc_packet_t* protocol_builder_popup_dismissed(protocol_popup_t* popup) {
    ipc_packet_t* packet = ipc_allocate_packet(0);

    ipc_packet_set_type(packet, 0x42);
    ipc_packet_set_object(packet, protocol_popup_id(popup));

    return packet;
}


// -------------------------------------------------------------------

bool protocol_handler_client_hello(struct protocol_client* client, ipc_packet_t* packet)
{

    if (client->initialized) return true;

    static uint64_t server_protocol_version;
    static uint64_t server_min_protocol_version;
    static bool server_protocol_version_set = false;

    if (!server_protocol_version_set) {
        server_protocol_version = version_to_i64(WL_SHIMEJI_PROTOCOL_VERSION);
        server_min_protocol_version = version_to_i64(WL_SHIMEJI_PROTOCOL_MIN_VER);
        server_protocol_version_set = true;
    }

    uint64_t client_version;

    ENSURE_MARSHALLER(ipc_packet_read_uint64(packet, &client_version));

    if (client_version > server_protocol_version || client_version < server_min_protocol_version) {
        char client_version_str[64];
        snprintf(client_version_str, sizeof(client_version_str), "%" PRIu64, client_version);
        char server_version_str[64];
        snprintf(server_version_str, sizeof(server_version_str), "%" PRIu64, server_protocol_version);
        char server_min_version_str[64];
        snprintf(server_min_version_str, sizeof(server_min_version_str), "%" PRIu64, server_min_protocol_version);

        const char* format[3] = {
            client_version_str,
            server_version_str,
            server_min_version_str
        };

        ipc_packet_t* notice = protocol_builder_notice(NOTICE_SEVERITY_ERROR, (client_version > server_protocol_version ? "connector.error.version.too_new" : "connector.error.version.too_old"), format, 3, false);

        ipc_connector_send(client->connector, notice);
        return false;
    }

    struct protocol_server_state* state = protocol_get_server_state();

    if (state->initialization_result) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "connector.error.initialization", (const char**)state->initialization_errors, state->initialization_errors_count, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    ipc_packet_t* server_hello = protocol_builder_server_hello();
    ipc_connector_send(client->connector, server_hello);

    pthread_mutex_lock(&state->prototypes_mutex);
    for (int32_t i = 0; i < mascot_prototype_store_count(state->prototypes); i++) {
        struct mascot_prototype* prototype = mascot_prototype_store_get_index(state->prototypes, i);
        if (prototype) {
            protocol_server_announce_new_prototype(prototype, client);
        }
    }
    pthread_mutex_unlock(&state->prototypes_mutex);

    ipc_packet_t* prototypes_commit = protocol_builder_commit_prototypes();
    ipc_connector_send(client->connector, prototypes_commit);

    pthread_mutex_lock(&state->environment_mutex);
    for (uint32_t i = 0; i < list_size(state->environments); i++) {
        struct environment* environment = list_get(state->environments, i);
        if (environment) {
            protocol_server_announce_new_environment(environment, client);
        }
    }
    pthread_mutex_unlock(&state->environment_mutex);

    client->initialized = true;
    client->protocol_version = client_version;

    ipc_packet_t* start_session = protocol_builder_start();
    ipc_connector_send(client->connector, start_session);

    return true;
}

bool protocol_handler_mascot_get_info(struct protocol_client* client, ipc_packet_t* packet)
{
    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t id = ipc_packet_get_object(packet) & 0x00FFFFFF;

    pthread_mutex_lock(&state->environment_mutex);
    struct mascot* mascot = NULL;
    for (uint32_t i = 0, c = list_count(state->environments); i < list_size(state->environments) && c; i++) {
        environment_t* environment = list_get(state->environments, i);
        if (!environment) continue;
        c--;

        if ((mascot = environment_mascot_by_id(environment, id))) break;
    }
    pthread_mutex_unlock(&state->environment_mutex);

    if (mascot) {
        ipc_packet_t* information = protocol_builder_mascot_info(mascot);
        ipc_connector_send(client->connector, information);
    } else {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "information.mascot.error.not_found", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    return true;
}

bool protocol_handler_select(struct protocol_client* client, ipc_packet_t* packet)
{
    struct protocol_server_state* state = protocol_get_server_state();
    struct list* environments;
    uint8_t environment_count;
    uint32_t new_object_id;

    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint8(packet, &environment_count));

    if (environment_count) {
        environments = list_init(environment_count);
    }
    else environments = state->environments;

    pthread_mutex_lock(&state->environment_mutex);
    for (uint8_t i = 0; i < environment_count; i++) {
        uint32_t environment;
        if (ipc_packet_read_uint32(packet, &environment)) {
            pthread_mutex_unlock(&state->environment_mutex);
            list_free(environments);
            return false;
        };
        for (uint32_t j = 0, c = list_count(state->environments); j < list_size(state->environments) && c; j++) {
            environment_t* env = list_get(environments, j);
            if (!env) continue;
            c--;
            if (environment_id(env) == (environment & 0x00FFFFFF)) {
                list_add(environments, env);
                break;
            }
        }
    }
    protocol_selection_t* selection = protocol_server_start_selection(environments, client, new_object_id);
    pthread_mutex_unlock(&state->environment_mutex);
    if (environments != state->environments) list_free(environments);
    return (!!selection);
}

bool protocol_handler_reload_prototype(struct protocol_client* client, ipc_packet_t* packet)
{
    struct protocol_server_state* state = protocol_get_server_state();

    char prototype_path[256] = {0};
    uint8_t length = 255;
    ENSURE_MARSHALLER(ipc_packet_read_string(packet, prototype_path, &length));
    if (!length) mascot_prototype_store_reload(state->prototypes);
    else {
        struct mascot_prototype* prototype = mascot_prototype_new();
        enum mascot_prototype_load_result result = mascot_prototype_load(prototype, state->prototypes_location, prototype_path);
        if (result == PROTOTYPE_LOAD_SUCCESS) {
            struct mascot_prototype* old_prototype = mascot_prototype_store_get(state->prototypes, prototype->name);
            if (old_prototype) {
                mascot_prototype_store_remove(state->prototypes, old_prototype);
            }
            mascot_prototype_store_add(state->prototypes, prototype);
            protocol_server_announce_new_prototype(prototype, NULL);
            return true;
        }
        ipc_packet_t* warning = NULL;
        switch (result) {
            case PROTOTYPE_LOAD_NOT_FOUND:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.not_found", (const char**)&prototype_path, 1, true);
                break;
            case PROTOTYPE_LOAD_NOT_DIRECTORY:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.not_directory", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_PERMISSION_DENIED:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.permission_denied", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_UNKNOWN_ERROR:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.unknown", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_MANIFEST_INVALID:
            case PROTOTYPE_LOAD_MANIFEST_NOT_FOUND:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.manifest.invalid", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_PROGRAMS_INVALID:
            case PROTOTYPE_LOAD_PROGRAMS_NOT_FOUND:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.programs.invalid", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_ACTIONS_INVALID:
            case PROTOTYPE_LOAD_ACTIONS_NOT_FOUND:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.actions.invalid", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_BEHAVIORS_INVALID:
            case PROTOTYPE_LOAD_BEHAVIORS_NOT_FOUND:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.behaviors.invalid", NULL, 0, true);
                break;
            case PROTOTYPE_LOAD_ASSETS_FAILED:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.assets.invalid", NULL, 0, true);
                break;
            default:
                warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototypes.error.unknown", NULL, 0, true);
        }
        if (warning) ipc_connector_send(client->connector, warning);
    }
    return true;
}

bool protocol_handler_spawn(struct protocol_client* client, ipc_packet_t* packet)
{
    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t environment_object_id;
    uint32_t prototype_object_id;
    uint32_t x,y;
    char summon_behavior[256] = {0};
    uint8_t behavior_name_length = 255;

    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &prototype_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &environment_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &x));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &y));
    ENSURE_MARSHALLER(ipc_packet_read_string(packet, summon_behavior, &behavior_name_length));

    environment_t* target_environment = NULL;
    struct mascot_prototype* prototype = NULL;
    ipc_packet_t* warning = NULL;

    prototype = mascot_prototype_store_get_by_id(state->prototypes, prototype_object_id & 0x00FFFFFF);

    pthread_mutex_lock(&state->environment_mutex);
    for (uint32_t i = 0, c = list_count(state->environments); i < list_size(state->environments) && c; i++) {
        environment_t* environment = list_get(state->environments, i);
        if (!environment) continue;
        c--;
        if (environment_id(environment) == (environment_object_id & 0x00FFFFFF)) {
            target_environment = environment;
            break;
        }
    }
    pthread_mutex_unlock(&state->environment_mutex);

    if (!prototype) warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "spawn.error.prototype.not_found", NULL, 0, true);
    if (!target_environment) warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "spawn.error.environment.not_found", NULL, 0, true);

    if (prototype && target_environment) {
        environment_summon_mascot(target_environment, prototype, x, y, NULL, NULL);
    }

    if (warning) ipc_connector_send(client->connector, warning);
    return true;
}

bool protocol_handler_dispose(struct protocol_client* client, ipc_packet_t* packet)
{
    UNUSED(client);

    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t mascot_id = ipc_packet_get_object(packet) & 0x00FFFFFF;
    struct mascot* mascot = NULL;

    environment_t* environment = NULL;
    pthread_mutex_lock(&state->environment_mutex);
    for (uint32_t i = 0, c = list_count(state->environments); i < list_size(state->environments) && c; i++) {
        environment = list_get(state->environments, i);
        if (!environment) continue;
        c--;
        if ((mascot = environment_mascot_by_id(environment, mascot_id))) break;
    }
    pthread_mutex_unlock(&state->environment_mutex);

    if (mascot) environment_remove_mascot(environment, mascot);

    return true;
}

bool protocol_handler_apply_behavior(struct protocol_client* client, ipc_packet_t* packet)
{
    UNUSED(client);

    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t mascot_id = ipc_packet_get_object(packet) & 0x00FFFFFF;
    struct mascot* mascot = NULL;

    char behavior_name[256] = {0};
    uint8_t namelen = 255;

    ENSURE_MARSHALLER(ipc_packet_read_string(packet, behavior_name, &namelen));

    pthread_mutex_lock(&state->environment_mutex);
    for (uint32_t i = 0, c = list_count(state->environments); i < list_size(state->environments) && c; i++) {
        environment_t* environment = list_get(state->environments, i);
        if (!environment) continue;
        c--;
        if ((mascot = environment_mascot_by_id(environment, mascot_id))) break;
    }
    pthread_mutex_unlock(&state->environment_mutex);

    if (mascot) {
        const struct mascot_behavior* behavior = mascot_prototype_behavior_by_name(mascot->prototype, behavior_name);
        if (!behavior) {
            ipc_packet_t* warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "prototype.error.no_behavior", (const char**)&behavior_name, 1, true);
            ipc_connector_send(client->connector, warning);
            return true;
        }

        mascot_set_behavior(mascot, behavior);
    } else {
        ipc_packet_t* warning = protocol_builder_notice(NOTICE_SEVERITY_WARNING, "apply_behavior.error.no_mascot", (const char**)&mascot_id, 1, true);
        ipc_connector_send(client->connector, warning);
    }

    return true;
}

bool protocol_handler_environment_close(struct protocol_client* client, ipc_packet_t* packet)
{
    UNUSED(client);
    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t object_id = ipc_packet_get_object(packet) & 0x00FFFFFF;

    pthread_mutex_lock(&state->environment_mutex);
    for (uint32_t i = 0, c = list_count(state->environments); i < list_size(state->environments) && c; i++) {
        environment_t* environment = list_get(state->environments, i);
        if (environment) {
            c--;
            if (environment_id(environment) == object_id) {
                pthread_mutex_unlock(&state->environment_mutex);
                environment_ask_close(environment);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&state->environment_mutex);

    ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "environment.close.error.not_found", NULL, 0, false);
    ipc_connector_send(client->connector, error);

    return true;
}

// bool protocol_handler_plugin_set_policy(struct protocol_client* client, ipc_packet_t* packet);
// bool protocol_handler_plugin_restore_windows(struct protocol_client* client, ipc_packet_t* packet);
// bool protocol_handler_plugin_deactivate(struct protocol_client* client, ipc_packet_t* packet);
// bool protocol_handler_plugin_activate(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_selection_cancel(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t object_id = ipc_packet_get_object(packet);
    protocol_selection_t* selection = protocol_client_remove_object(client, object_id);
    if (!selection)
        return false;
    protocol_selection_cancel(selection);
    return true;
}

bool protocol_handler_share_shm_pool(struct protocol_client* client, ipc_packet_t* packet)
{
    int32_t shmpool_fd;
    uint32_t new_object_id;
    uint32_t size;

    ENSURE_MARSHALLER(ipc_packet_consume_fd(packet, &shmpool_fd));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &size));

    protocol_shm_pool_t* pool = protocol_shm_pool_new(client, new_object_id, shmpool_fd, size);
    if (!pool) {
        pool = protocol_server_ephermal_shm_pool(new_object_id);
        ipc_packet_t* packet = protocol_builder_shm_pool_failed(pool);
        ipc_connector_send(client->connector, packet);
        free(pool);
        return true;
    }
    ipc_packet_t* imported = protocol_builder_shm_pool_imported(pool);
    ipc_connector_send(client->connector, imported);
    return true;
}

bool protocol_handler_shm_pool_create_buffer(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t protocol_shm_pool_id = ipc_packet_get_object(packet);
    if (!protocol_shm_pool_id) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    protocol_shm_pool_t* pool = protocol_client_find_object(client, protocol_shm_pool_id);
    if (!pool) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    uint32_t new_object_id;
    uint32_t offset;
    uint32_t width, height;
    uint32_t stride;
    uint32_t format;

    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &offset));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &width));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &height));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &stride));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &format));

    protocol_buffer_t* buffer = protocol_shm_pool_buffer_new(client, pool, new_object_id, width, height, stride, format, offset);
    if (!buffer)
        return false;
    return true;
}

bool protocol_handler_shm_pool_destroy(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t protocol_shm_pool_id = ipc_packet_get_object(packet);
    if (!protocol_shm_pool_id) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }
    protocol_shm_pool_t* pool = protocol_client_find_object(client, protocol_shm_pool_id);
    if (!pool) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }
    protocol_shm_pool_destroy(pool);
    return true;
}

bool protocol_handler_buffer_destroy(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t buffer_object_id = ipc_packet_get_object(packet);
    if (!protocol_client_find_object(client, buffer_object_id)) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }
    protocol_buffer_t* buffer = protocol_client_find_object(client, buffer_object_id);
    if (!buffer) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }
    protocol_buffer_destroy(buffer);
    return true;
}

bool protocol_handler_click_event_accept(struct protocol_client* client, ipc_packet_t* packet)
{

    struct protocol_server_state* state = protocol_get_server_state();

    uint32_t click_event_object_id = ipc_packet_get_object(packet);
    uint32_t new_object_id;
    uint32_t width, height;
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &width));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &height));

    if (!new_object_id) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    bool failed = false;
    if (!click_event_object_id) failed = true;
    if (!state->last_click_event) failed = true;
    else if (protocol_click_event_is_expired(state->last_click_event)) failed = true;

    if (failed) {
        protocol_popup_t* popup = protocol_server_ephermal_popup(new_object_id);
        ipc_packet_t* dismissed = protocol_builder_popup_dismissed(popup);
        ipc_connector_send(client->connector, dismissed);
        free(popup);
        return true;
    }

    protocol_popup_t* popup = protocol_server_click_accept(client, width, height, new_object_id);
    if (!popup) return false;

    return true;
}

bool protocol_handler_popup_child_popup(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t parent_object_id = ipc_packet_get_object(packet);
    uint32_t new_object_id;
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &width));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &height));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &x));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &y));

    if (!parent_object_id || !new_object_id) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    protocol_popup_t* parent_popup = protocol_client_find_object(client, parent_object_id);
    if (!parent_popup) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    protocol_popup_t* child_popup = protocol_popup_child_popup(client, parent_popup, x, y, width, height, new_object_id);
    if (!child_popup) return false;

    return true;
}

bool protocol_handler_popup_attach(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t popup_id = ipc_packet_get_object(packet);
    uint32_t buffer_object_id;
    uint32_t x_damage;
    uint32_t y_damage;
    uint32_t width_damage;
    uint32_t height_damage;

    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &buffer_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &x_damage));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &y_damage));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &width_damage));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &height_damage));

    protocol_popup_t* popup = protocol_client_find_object(client, popup_id);
    if (!popup) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    protocol_buffer_t* buffer = protocol_client_find_object(client, buffer_object_id);
    if (!buffer) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    // protocol_popup_attach(popup, buffer, x_damage, y_damage, width_damage, height_damage);
    return true;
}

bool protocol_handler_popup_dismiss(struct protocol_client* client, ipc_packet_t* packet)
{
    uint32_t popup_id = ipc_packet_get_object(packet);
    protocol_popup_t* popup = protocol_client_find_object(client, popup_id);
    if (!popup) {
        ipc_packet_t* error = protocol_builder_notice(NOTICE_SEVERITY_ERROR, "object_id.error.invalid", NULL, 0, false);
        ipc_connector_send(client->connector, error);
        return false;
    }

    // protocol_popup_dismiss(client, popup);
    return true;
}

bool protocol_handler_set_config_key(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_get_config_key(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_list_config_keys(struct protocol_client* client, ipc_packet_t* packet);

bool protocol_handler_import(struct protocol_client* client, ipc_packet_t* packet)
{
    int32_t fd;
    uint32_t new_object_id;
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ipc_packet_consume_fd(packet, &fd);
    protocol_server_import(client, fd, new_object_id, ipc_packet_get_flags(packet));
    return true;
}

bool protocol_handler_export(struct protocol_client* client, ipc_packet_t* packet)
{

    struct protocol_server_state* state = protocol_get_server_state();

    int32_t fd;
    uint32_t new_object_id;
    uint32_t prototype_object_id;
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &new_object_id));
    ENSURE_MARSHALLER(ipc_packet_read_uint32(packet, &prototype_object_id));

    struct mascot_prototype* prototype = mascot_prototype_store_get_by_id(state->prototypes, prototype_object_id & 0x00FFFFFF);
    if (!prototype) {
        protocol_export_t* ephermal_export = protocol_server_ephermal_export(new_object_id);
        ipc_packet_t* failed = protocol_builder_export_failed(ephermal_export, 6);
        ipc_connector_send(client->connector, failed);
        free(ephermal_export);
        return true;
    }

    if (prototype->unlinked) {
        protocol_export_t* ephermal_export = protocol_server_ephermal_export(new_object_id);
        ipc_packet_t* failed = protocol_builder_export_failed(ephermal_export, 6);
        ipc_connector_send(client->connector, failed);
        free(ephermal_export);
        return true;
    }

    mascot_prototype_link(prototype);

    ipc_packet_consume_fd(packet, &fd);
    protocol_export_t* export = protocol_server_export(client, fd, new_object_id, prototype);
    if (!export) {
        mascot_prototype_unlink(prototype);
    }
    return true;
}
