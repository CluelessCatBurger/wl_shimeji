#include "packet_handler.h"
#include "environment.h"
#include "mascot.h"
#include "config.h"
#include "list.h"
#include <stdbool.h>
#include <string.h>
#include <malloc.h>

#define IPC_VER 1

bool write_byte(struct packet* packet, uint8_t value)
{
    if (packet->position + 1 > packet->size) {
        return false;
    }
    packet->buffer[packet->position] = value;
    packet->position++;
    return true;
}

bool write_str(struct packet* packet, const char* string)
{
    if (!string) {
        if (!write_byte(packet, 0)) {
            return false;
        }
        return true;
    }
    uint8_t slen = strlen(string);
    if (packet->position + slen + 1 > packet->size) {
        return false;
    }
    packet->buffer[packet->position] = slen;
    packet->position++;
    memcpy(packet->buffer + packet->position, string, slen);
    packet->position += slen;
    return true;
}

bool write_int32(struct packet* packet, int32_t value)
{
    if (packet->position + 4 > packet->size) {
        return false;
    }
    memcpy(packet->buffer + packet->position, &value, 4);
    packet->position += 4;
    return true;
}

bool write_float(struct packet* packet, float value)
{
    if (packet->position + 4 > packet->size) {
        return false;
    }
    memcpy(packet->buffer + packet->position, &value, 4);
    packet->position += 4;
    return true;
}

bool write_long(struct packet* packet, uint64_t value)
{
    if (packet->position + 8 > packet->size) {
        return false;
    }
    memcpy(packet->buffer + packet->position, &value, 8);
    packet->position += 8;
    return true;
}

bool write_double(struct packet* packet, double value)
{
    if (packet->position + 8 > packet->size) {
        return false;
    }
    memcpy(packet->buffer + packet->position, &value, 8);
    packet->position += 8;
    return true;
}

bool write_bytes(struct packet* packet, uint8_t* bytes, uint16_t size)
{
    if (packet->position + size > packet->size) {
        return false;
    }
    memcpy(packet->buffer + packet->position, bytes, size);
    packet->position += size;
    return true;
}

bool write_variable(struct packet* packet, struct mascot_local_variable* var)
{

    if (!write_int32(packet, var->value.i)) {
        return false;
    }
    if (!write_byte(packet, var->kind)) {
        return false;
    }
    if (!write_byte(packet, var->used)) {
        return false;
    }
    if (!write_byte(packet, var->expr.evaluated)) {
        return false;
    }
    if (var->expr.expression_prototype) {
        if (!write_byte(packet, 1)) {
            return false;
        }
        if (!write_int32(packet, var->expr.expression_prototype->body->id)) {
            return false;
        }
    } else {
        if (!write_byte(packet, 0)) {
            return false;
        }
    }
    return true;
}

bool read_bytes(struct packet* packet, uint8_t* out, uint16_t size)
{
    if (packet->position + size > packet->size) {
        return false;
    }
    memcpy(out, packet->buffer + packet->position, size);
    packet->position += size;
    return true;
}

bool read_byte(struct packet* packet, uint8_t* out)
{
    if (packet->position + 1 > packet->size) {
        return false;
    }
    *out = packet->buffer[packet->position];
    packet->position++;
    return true;
}

bool read_string(struct packet* packet, uint8_t* out, uint16_t max_size)
{
    uint8_t size;
    if (!read_byte(packet, &size)) {
        return false;
    }
    if (size > max_size) {
        return false;
    }
    if (!read_bytes(packet, out, size)) {
        return false;
    }
    out[size] = 0;
    return true;
}

bool read_int32(struct packet* packet, int32_t* out)
{
    if (packet->position + 4 > packet->size) {
        return false;
    }
    memcpy(out, packet->buffer + packet->position, 4);
    packet->position += 4;
    return true;
}

bool read_float(struct packet* packet, float* out)
{
    if (packet->position + 4 > packet->size) {
        return false;
    }
    memcpy(out, packet->buffer + packet->position, 4);
    packet->position += 4;
    return true;
}

bool read_long(struct packet* packet, uint64_t* out)
{
    if (packet->position + 8 > packet->size) {
        return false;
    }
    memcpy(out, packet->buffer + packet->position, 8);
    packet->position += 8;
    return true;
}

bool read_header(struct packet* packet, uint8_t* packet_id, uint8_t* version, uint16_t* payload_size, uint32_t* event_id)
{
    *packet_id = packet->buffer[0];
    *version = packet->buffer[1];
    memcpy(payload_size, packet->buffer + 2, 2);
    memcpy(event_id, packet->buffer + 4, 4);
    packet->position = 8;
    return true;
}

bool write_header(struct packet* packet, uint8_t packet_id, uint8_t version, uint16_t payload_size, uint32_t event_id)
{
    packet->buffer[0] = packet_id;
    packet->buffer[1] = version;
    memcpy(packet->buffer + 2, &payload_size, 2);
    memcpy(packet->buffer + 4, &event_id, 4);
    packet->position = 8;
    return true;
}

bool handle_client_hello(struct daemon_data* daemon_data, struct client *client, struct packet *packet)
{
    daemon_data->send_packet(client, build_server_hello());
    if (daemon_data->initialized) {
        handle_initialized_internal(daemon_data, client, packet);
    }
    return true;
}

bool handle_initialized_internal(struct daemon_data* daemon_data, struct client* client, struct packet* packet)
{
    UNUSED(packet);
    daemon_data->send_packet(client, build_initialization_status(0, NULL));
    for (int32_t i = 0; i < mascot_prototype_store_count(daemon_data->prototypes); i++) {
        struct mascot_prototype* prototype = mascot_prototype_store_get_index(daemon_data->prototypes, i);
        daemon_data->send_packet(client, build_prototype_announcement(prototype));
    }
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        daemon_data->send_packet(client, build_environment(env, 0));
    }
    daemon_data->send_packet(client, build_information(daemon_data));
    daemon_data->send_packet(client, build_done());
    return true;
}

bool handle_describe_prototype(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t prototype_id;
    if (!read_int32(packet, &prototype_id)) {
        return false;
    }
    struct mascot_prototype* prototype = mascot_prototype_store_get_by_id(daemon_data->prototypes, prototype_id);
    if (!prototype) {
        return false;
    }
    daemon_data->send_packet(client, build_description_part(packet->event_id, 0, (void*)prototype->name, strlen(prototype->name)));
    daemon_data->send_packet(client, build_description_part(packet->event_id, 1, (void*)prototype->display_name, strlen(prototype->display_name)));
    daemon_data->send_packet(client, build_description_part(packet->event_id, 2, (void*)prototype->path, strlen(prototype->path)));
    for (int32_t i = 0; i < prototype->actions_count; i++) {
        const struct mascot_action* action = prototype->action_definitions[i];
        daemon_data->send_packet(client, build_description_part(packet->event_id, 3, (void*)action->name, strlen(action->name)));
    }
    for (int32_t i = 0; i < prototype->behavior_count; i++) {
        const struct mascot_behavior* behavior = prototype->behavior_definitions[i];
        daemon_data->send_packet(client, build_description_part(packet->event_id, 4, (void*)behavior->name, strlen(behavior->name)));
    }
    daemon_data->send_packet(client, build_description_part(packet->event_id, 5, (void*)&prototype->reference_count, 2));
    daemon_data->send_packet(client, build_description_described(packet->event_id));
    return true;
}

bool handle_get_mascots_by_env(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t env_id;
    if (!read_int32(packet, &env_id)) {
        return false;
    }
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        if (environment_id(env) == (uint32_t)env_id) {
            daemon_data->send_packet(client, build_mascot_ids(env, packet->event_id));
            return true;
        }
    }
    return false;
}

bool handle_get_mascot_info(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t mascot_id;
    if (!read_int32(packet, &mascot_id)) {
        return false;
    }
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        struct mascot* mascot = environment_mascot_by_id(env, mascot_id);
        if (mascot) {
            daemon_data->send_packet(client, build_mascot_info(mascot, packet->event_id));
            return true;
        }
    }
    return false;
}

static void summon_result(struct mascot* mascot, void* data)
{
    struct client* client = (struct client*)data;
    struct daemon_data* daemon_data = client->daemon_data;
    if (mascot) {
        daemon_data->send_packet(client, build_mascot_event_request_result(0, 0, NULL, 0, NULL));
    } else {
        daemon_data->send_packet(client, build_mascot_event_request_result(0, 1, "summon.failure.generic", 0, NULL));
    }
}

bool handle_summon(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    char name[256];
    int32_t env_id;
    if (!read_string(packet, (uint8_t*)name, 255)) {
        return false;
    }
    if (!read_int32(packet, &env_id)) {
        return false;
    }
    struct environment* env = NULL;
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* e = list_get(daemon_data->environments, i);
        if (environment_id(e) == (uint32_t)env_id) {
            env = e;
            break;
        }
    }
    if (!env) {
        daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 1, "summon.failure.no_env", 0, NULL));
        return true;
    }
    struct mascot_prototype* prototype = mascot_prototype_store_get(daemon_data->prototypes, name);
    if (!prototype) {
        daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 1, "summon.failure.no_proto", 1, (const char**)&name));
        return true;
    }

    int32_t x, y;
    if (!read_int32(packet, &x)) {
        return false;
    }
    if (!read_int32(packet, &y)) {
        return false;
    }
    environment_summon_mascot(env, prototype, x, y, summon_result, client);
    return true;
}

bool handle_dismiss(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t mascot_id;
    if (!read_int32(packet, &mascot_id)) {
        return false;
    }
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        struct mascot* mascot = environment_mascot_by_id(env, mascot_id);
        if (mascot) {
            environment_remove_mascot(env, mascot);
            daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 0, NULL, 0, NULL));
            return true;
        }
    }
    daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 1, "dismiss.failure.no_mascot", 0, NULL));
    return true;
}

bool handle_behavior(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t mascot_id;
    if (!read_int32(packet, &mascot_id)) {
        return false;
    }
    char behavior[256];
    if (!read_string(packet, (uint8_t*)behavior, 255)) {
        return false;
    }
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        struct mascot* mascot = environment_mascot_by_id(env, mascot_id);
        if (mascot) {
            const struct mascot_behavior* b = mascot_prototype_behavior_by_name(mascot->prototype, behavior);
            if (b) {
                mascot_set_behavior(mascot, b);
                daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 0, NULL, 0, NULL));
                return true;
            }
            daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 1, "behavior.failure.no_behavior", 1, (const char**)&behavior));
        }
    }
    daemon_data->send_packet(client, build_mascot_event_request_result(packet->event_id, 1, "behavior.failure.no_mascot", 0, NULL));
    return true;
}

bool handle_reload_prototype(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    char path[256];
    if (!read_string(packet, (uint8_t*)path, 255)) {
        return false;
    }

    if (strncmp(path, daemon_data->config_path, 255) == 0) {
        daemon_data->send_packet(client, build_reloaded(packet->event_id, 1, "reload.failure.config_protected", 0, NULL));
        return true;
    }

    struct mascot_prototype* prototype = mascot_prototype_new();
    if (!prototype) {
        daemon_data->send_packet(client, build_reloaded(packet->event_id, 1, "reload.failure.oom", 0, NULL));
        return true;
    }

    if (!mascot_prototype_load(prototype, path)) {
        mascot_prototype_unlink(prototype);
        daemon_data->send_packet(client, build_reloaded(packet->event_id, 1, "reload.failure.load_failed", 1, (const char**)&path));
        return true;
    }

    struct mascot_prototype* old_prototype = mascot_prototype_store_get(daemon_data->prototypes, prototype->name);
    if (old_prototype) {
        mascot_prototype_store_remove(daemon_data->prototypes, old_prototype);
    }

    mascot_prototype_store_add(daemon_data->prototypes, prototype);
    daemon_data->send_packet(client, build_reloaded(packet->event_id, 0, NULL, 0, NULL));
    return true;
}

bool handle_stop(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    UNUSED(client);
    UNUSED(packet);
    daemon_data->stop();
    return true;
}

bool handle_config(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t key_id;
    int32_t value;
    uint8_t action;
    if (!read_int32(packet, &key_id)) {
        return false;
    }
    if (!read_byte(packet, &action)) {
        return false;
    }

    if ((uint32_t)key_id >= CONFIG_PARAM_COUNT) {
        return false;
    }

    if (action == 0) {
        int32_t value = config_getter_table(key_id);
        daemon_data->send_packet(client, build_config_response(key_id, true, value));
    } else {
        if (!read_int32(packet, &value)) {
            return false;
        }
        bool res = config_setter_table(key_id, value);
        daemon_data->send_packet(client, build_config_response(key_id, res, value));
    }
    return true;
}

bool handle_subscribe(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t event_id;
    if (!read_int32(packet, &event_id)) {
        return false;
    }
    if (event_id != 0) {
        return false;
    }
    daemon_data->click_event_subscription = client;
    return true;
}

bool handle_unsubscribe(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t event_id;
    if (!read_int32(packet, &event_id)) {
        return false;
    }
    if (event_id != 0) {
        return false;
    }
    if (daemon_data->click_event_subscription == client) {
        daemon_data->click_event_subscription = NULL;
    }
    return true;
}

static void selection_callback(environment_t* env, int32_t x, int32_t y, environment_subsurface_t* surface, void* data)
{
    struct client_selection* selection = (struct client_selection*)data;
    struct daemon_data* daemon_data = selection->client->daemon_data;
    daemon_data->send_packet(selection->client,
        build_selected(
            selection->event_id,
            environment_subsurface_get_mascot(surface),
            env,
            x, y, x, y
        )
    );
    for (uint32_t i = 0; i < list_size(selection->environments); i++) {
        struct environment* e = list_get(selection->environments, i);
        if (!e) continue;
        environment_select_position(e, NULL, NULL);
        environment_set_input_state(e, false);
    }
    list_free(selection->environments);
    list_remove(selection->client->selections, list_find(selection->client->selections, selection));
    free(selection);
}

bool handle_select(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    int32_t env_id;
    if (!read_int32(packet, &env_id)) {
        return false;
    }

    struct client_selection* selection = calloc(1, sizeof(struct client_selection));
    if (!selection) {
        return false;
    }

    selection->environments = list_init(4);
    if (!selection->environments) {
        free(selection);
        return false;
    }

    selection->event_id = packet->event_id;
    selection->client = client;

    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        struct environment* env = list_get(daemon_data->environments, i);
        if (!env) continue;
        if (environment_id(env) == (uint32_t)env_id || env_id == -1) {
            list_add(selection->environments, env);
            environment_select_position(env, selection_callback, selection);
            environment_set_input_state(env, true);
        }
    }

    if (list_size(selection->environments) == 0) {
        list_free(selection->environments);
        free(selection);
        return false;
    }

    list_add(client->selections, selection);
    return true;
}

bool handle_deselect(struct daemon_data *daemon_data, struct client *client, struct packet *packet)
{
    UNUSED(daemon_data);
    int32_t env_id;
    if (!read_int32(packet, &env_id)) {
        return false;
    }

    for (uint32_t i = 0; i < list_size(client->selections); i++) {
        struct client_selection* selection = list_get(client->selections, i);
        if (!selection) continue;
        if (selection->event_id == packet->event_id) {
            for (uint32_t j = 0; j < list_size(selection->environments); j++) {
                struct environment* env = list_get(selection->environments, j);
                if (!env) continue;
                if (environment_id(env) == (uint32_t)env_id || env_id == -1) {
                    environment_select_position(env, NULL, NULL);
                    environment_set_input_state(env, false);
                }
            }
            list_free(selection->environments);
            list_remove(client->selections, i);
            free(selection);
            return true;
        }
    }
    return false;
}

struct client* connect_client(int fd, struct daemon_data* daemon_data)
{
    struct client* client = calloc(1, sizeof(struct client));
    if (!client) {
        return NULL;
    }
    client->fd = fd;
    client->selections = list_init(4);
    client->daemon_data = daemon_data;
    if (!client->selections) {
        free(client);
        return NULL;
    }
    return client;

}

void disconnect_client(struct client* client)
{
    for (uint32_t i = 0; i < list_size(client->selections); i++) {
        struct client_selection* selection = list_get(client->selections, i);
        if (!selection) continue;
        for (uint32_t j = 0; j < list_count(selection->environments); j++) {
            struct environment* env = list_get(selection->environments, j);
            if (!env) continue;
            environment_select_position(env, NULL, NULL);
            environment_set_input_state(env, false);
        }
        list_free(selection->environments);
        free(selection);
    }
    list_free(client->selections);
    close(client->fd);
    free(client);
}

bool handle_packet(struct daemon_data* daemon_data, struct client* client, struct packet* packet)
{
    uint8_t packet_id;
    uint8_t version;
    uint16_t payload_size;
    uint32_t event_id;
    if (!read_header(packet, &packet_id, &version, &payload_size, &event_id)) {
        WARN("Failed to read packet header");
        return false;
    }
    if (version != IPC_VER) {
        WARN("Invalid IPC version");
        return false;
    }

    bool result = false;
    switch (packet_id) {
        case 0x00:
            result = handle_client_hello(daemon_data, client, packet);
            break;
        case 0x06:
            result = handle_describe_prototype(daemon_data, client, packet);
            break;
        case 0x0B:
            result = handle_get_mascots_by_env(daemon_data, client, packet);
            break;
        case 0x0D:
            result = handle_get_mascot_info(daemon_data, client, packet);
            break;
        case 0x0F:
            result = handle_summon(daemon_data, client, packet);
            break;
        case 0x10:
            result = handle_dismiss(daemon_data, client, packet);
            break;
        case 0x11:
            result = handle_behavior(daemon_data, client, packet);
            break;
        case 0x13:
            result = handle_reload_prototype(daemon_data, client, packet);
            break;
        case 0x16:
            result = handle_stop(daemon_data, client, packet);
            break;
        case 0x17:
            result = handle_config(daemon_data, client, packet);
            break;
        case 0x20:
            result = handle_subscribe(daemon_data, client, packet);
            break;
        case 0x21:
            result = handle_unsubscribe(daemon_data, client, packet);
            break;
        case 0x2C:
            result = handle_select(daemon_data, client, packet);
            break;
        case 0x2D:
            result = handle_deselect(daemon_data, client, packet);
            break;
        default:
            break;
    }
    struct packet* response = new_packet(2048, NULL, -1);
    if (!response) {
        return true;
    }
    if (!result) {
        write_header(response, 0x31, IPC_VER, 0, 0);
        write_int32(response, event_id);
        daemon_data->send_packet(client, response);
    }
    return true;
}

struct packet* build_server_hello()
{
    struct packet* packet = new_packet(8, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x01, IPC_VER, 8, 0);
    return packet;
}

struct packet* build_initialization_status(uint8_t status, const char* message)
{
    struct packet* packet = new_packet(264, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x02, IPC_VER, 8, 0);
    write_byte(packet, status);
    write_str(packet, message);
    return packet;
}

struct packet* build_prototype_announcement(struct mascot_prototype* prototype)
{
    struct packet* packet = new_packet(264, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x03, IPC_VER, 8, 0);
    write_int32(packet, prototype->id);
    write_str(packet, prototype->name);
    return packet;
}

struct packet* build_information(struct daemon_data* daemon_data)
{
    struct packet* packet = new_packet(25, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x04, IPC_VER, 8, 0);
    write_int32(packet, list_count(daemon_data->clients));
    uint32_t mascots_count = 0;
    for (uint32_t i = 0; i < list_size(daemon_data->environments); i++) {
        mascots_count += environment_mascot_count(list_get(daemon_data->environments, i));
    }
    write_int32(packet, mascots_count);
    write_int32(packet, list_size(daemon_data->environments));
    write_int32(packet, mascot_prototype_store_count(daemon_data->prototypes));
    write_byte(packet, daemon_data->click_event_subscription != NULL);
    return packet;
}

struct packet* build_done()
{
    struct packet* packet = new_packet(8, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x05, IPC_VER, 0, 0);
    return packet;
}

struct packet* build_description_part(uint32_t event_id, uint8_t part, void* data, uint16_t size)
{
    struct packet* packet = new_packet(2048, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x07, IPC_VER, size + 8, 0);
    write_int32(packet, event_id);
    write_byte(packet, part);
    write_bytes(packet, (uint8_t*)&size, 2);
    write_bytes(packet, (uint8_t*)data, size);
    return packet;
}

struct packet* build_description_described(uint32_t event_id)
{
    struct packet* packet = new_packet(8, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x08, IPC_VER, 0, 0);
    write_int32(packet, event_id);

    return packet;
}

struct packet* build_environment(environment_t* env, uint8_t action) {
    struct packet* packet = new_packet(64, NULL, -1);
    if (!packet) {
        return NULL;
    }
    if (!env) {
        return NULL;
    }
    write_header(packet, 0x09, IPC_VER, 8, 0);
    write_int32(packet, environment_id(env));
    write_byte(packet, action);
    if (action == 0) {
        struct bounding_box* geometry = environment_local_geometry(env);
        write_int32(packet, geometry->x);
        write_int32(packet, geometry->y);
        write_int32(packet, geometry->width);
        write_int32(packet, geometry->height);
        write_int32(packet, environment_mascot_count(env));
    } else {
        write_int32(packet, 0);
        write_int32(packet, 0);
        write_int32(packet, 0);
        write_int32(packet, 0);
        write_int32(packet, 0);
    }
    return packet;
}

struct packet* build_mascot(struct mascot* mascot, uint8_t action)
{
    struct packet* packet = new_packet(64, NULL, -1);
    if (!packet) {
        return NULL;
    }
    if (!mascot) {
        return NULL;
    }
    const struct mascot_prototype* prototype = mascot->prototype;
    write_header(packet, 0x0A, IPC_VER, 8, 0);
    write_int32(packet, mascot->id);
    write_byte(packet, action);
    write_int32(packet, environment_id(mascot->environment));
    write_int32(packet, prototype->id);
    return packet;
}

struct packet* build_mascot_ids(environment_t* env, uint32_t event_id)
{
    struct packet* packet = new_packet(64, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x0C, IPC_VER, 8, 0);
    write_int32(packet, event_id);
    write_int32(packet, environment_mascot_count(env));
    struct list* mascots = environment_mascot_list(env, NULL);
    for (uint32_t i = 0; i < list_size(mascots); i++) {
        struct mascot* mascot = list_get(mascots, i);
        write_int32(packet, mascot->id);
    }
    return packet;
}

struct packet* build_mascot_info(struct mascot* mascot, uint32_t event_id)
{
    struct packet* packet = new_packet(64, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x0E, IPC_VER, 8, 0);
    write_int32(packet, event_id);
    write_int32(packet, environment_id(mascot->environment));
    write_int32(packet, mascot->prototype->id);
    write_int32(packet, mascot->action_index);
    write_int32(packet, mascot->action_duration);
    write_int32(packet, mascot->action_tick);
    if (mascot->current_action.action) write_str(packet, mascot->current_action.action->name);
    else write_str(packet, NULL);
    if (mascot->current_behavior) write_str(packet, mascot->current_behavior->name);
    else write_str(packet, NULL);
    write_str(packet, mascot->current_affordance);
    write_int32(packet, mascot->state);
    write_byte(packet, mascot->as_p);
    for (int32_t i = mascot->as_p; i > 0; i--) {
        write_str(packet, mascot->action_stack[i - 1].action->name);
        write_int32(packet, mascot->action_index_stack[i - 1]);
    }
    write_byte(packet, mascot->behavior_pool_len);
    for (int32_t i = 0; i < mascot->behavior_pool_len; i++) {
        write_str(packet, mascot->behavior_pool[i].behavior->name);
        write_long(packet, mascot->behavior_pool[i].behavior->frequency);
    }
    write_int32(packet, MASCOT_LOCAL_VARIABLE_COUNT);
    for (int32_t i = 0; i < MASCOT_LOCAL_VARIABLE_COUNT; i++) {
        write_variable(packet, &mascot->local_variables[i]);
    }
    return packet;
}

struct packet* build_mascot_event_request_result(uint32_t event_id, uint8_t result, const char* reason, uint8_t args_count, const char** args)
{
    struct packet* packet = new_packet(2048, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x12, IPC_VER, 8, 0);
    write_int32(packet, event_id);
    write_byte(packet, result);
    write_str(packet, reason);
    write_byte(packet, args_count);
    for (uint8_t i = 0; i < args_count; i++) {
        write_str(packet, args[i]);
    }
    return packet;
}

struct packet* build_reloaded(uint32_t event_id, uint8_t result, const char* reason, uint8_t args_count, const char** args)
{
    struct packet* packet = new_packet(2048, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x14, IPC_VER, 8, 0);
    write_int32(packet, event_id);
    write_byte(packet, result);
    write_str(packet, reason);
    write_byte(packet, args_count);
    for (uint8_t i = 0; i < args_count; i++) {
        write_str(packet, args[i]);
    }
    return packet;
}

struct packet* build_config_response(uint32_t key_id, bool success, int32_t value)
{
    struct packet* packet = new_packet(32, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x18, IPC_VER, 8, 0);
    write_int32(packet, key_id);
    write_byte(packet, success);
    write_int32(packet, value);
    return packet;
}

struct packet* build_selected(uint32_t event_id, struct mascot* mascot, environment_t* env, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    struct packet* packet = new_packet(64, NULL, -1);
    if (!packet) {
        return NULL;
    }
    write_header(packet, 0x2E, IPC_VER, 8, 0);
    write_int32(packet, event_id);
    if (mascot) {
        write_int32(packet, mascot->id);
    } else {
        write_int32(packet, -1);
    }
    write_int32(packet, environment_id(env));
    write_int32(packet, x1);
    write_int32(packet, y1);
    write_int32(packet, x2);
    write_int32(packet, y2);
    return packet;
}

struct packet* new_packet(uint16_t size, uint8_t* buffer, int32_t fd)
{
    struct packet* packet = calloc(1, sizeof(struct packet));
    if (!packet) {
        return NULL;
    }
    packet->size = size;
    packet->position = 0;
    packet->fd = fd;

    if (buffer) {
        packet->buffer = buffer;
    } else {
        packet->buffer = calloc(1, size);
        if (!packet->buffer) {
            free(packet);
            return NULL;
        }
        packet->buffer_owned = true;
    }

    return packet;
}

void destroy_packet(struct packet* packet)
{
    if (packet->buffer_owned) {
        free(packet->buffer);
    }
    free(packet);
}
