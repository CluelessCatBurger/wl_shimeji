#ifndef PACKET_HANDLE_H
#define PACKET_HANDLE_H

#include "master_header.h"

#include "mascot.h"
#include "environment.h"
#include "list.h"

#include <pthread.h>

struct daemon_data;

struct client_selection {
    uint32_t event_id;
    struct list* environments;
    struct client* client;
};

struct client {
    int fd;
    struct list* popups;
    pthread_mutex_t popups_mutex;
    struct daemon_data* daemon_data;
    struct list* selections;
};

struct packet {
    uint16_t position;
    uint16_t size;
    uint8_t* buffer;

    uint8_t packet_type;
    uint8_t version;
    uint16_t payload_size;
    uint32_t event_id;

    bool buffer_owned;

    int32_t fd; // File descriptor from auxdata
};

struct daemon_data {
    struct list* environments;
    mascot_prototype_store* prototypes;
    struct list* clients;

    const char *config_path;

    bool initialized;

    struct client* click_event_subscription;

    pthread_mutex_t env_mutex;
    pthread_mutex_t proto_mutex;
    pthread_mutex_t client_mutex;

    void (*send_packet)(struct client*, struct packet*);
    void (*stop)();
};

struct client* connect_client(int fd, struct daemon_data* data);
void disconnect_client(struct client* client);

typedef bool (*packet_handler)(struct client*, struct packet*);

bool handle_client_hello(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_initialized_internal(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_describe_prototype(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_get_mascots_by_env(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_get_mascot_info(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_summon(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_dismiss(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_behavior(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_reload_prototype(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_stop(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_config(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_subscribe(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_unsubscribe(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_select(struct daemon_data* daemon_data, struct client* client, struct packet* packet);
bool handle_deselect(struct daemon_data* daemon_data, struct client* client, struct packet* packet);

struct packet* new_packet(uint16_t size, uint8_t* data, int32_t fd);
void destroy_packet(struct packet* packet);

struct packet* build_server_hello();
struct packet* build_initialization_status(uint8_t status, const char* reason);
struct packet* build_prototype_announcement(struct mascot_prototype* proto);
struct packet* build_information(struct daemon_data* data);
struct packet* build_done();
struct packet* build_description_part(uint32_t event_id, uint8_t type, void* data, uint16_t size);
struct packet* build_description_described(uint32_t event_id);
struct packet* build_environment(environment_t* env, uint8_t action);
struct packet* build_mascot(struct mascot* mascot, uint8_t action);
struct packet* build_mascot_ids(environment_t* env, uint32_t event_id);
struct packet* build_mascot_info(struct mascot* mascot, uint32_t event_id);
struct packet* build_mascot_event_request_result(uint32_t event_id, uint8_t result, const char* reason, uint8_t args_count, const char** args);
struct packet* build_reloaded(uint32_t event_id, uint8_t result, const char* reason, uint8_t args_count, const char** args);
struct packet* build_config_response(uint32_t key_id, bool success, int32_t value);
struct packet* build_click_event(struct mascot* mascot, environment_t* env, int32_t x, int32_t y, int32_t global_x, int32_t global_y, uint32_t button);
struct packet* build_selected(uint32_t event_id, struct mascot* mascot, environment_t* env, int32_t x, int32_t y, int32_t global_x, int32_t global_y);

bool handle_packet(struct daemon_data* daemon_data, struct client* client, struct packet* packet);

bool read_header(struct packet* packet, uint8_t* packet_id, uint8_t* version, uint16_t* payload_size, uint32_t* event_id);
bool write_header(struct packet* packet, uint8_t packet_id, uint8_t version, uint16_t payload_size, uint32_t event_id);



#endif
