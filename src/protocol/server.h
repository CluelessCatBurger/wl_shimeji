#ifndef PROTOCOL_CLIENT_H
#define PROTOCOL_CLIENT_H

#include "connector.h"
#include <list.h>
#include <mascot_config_parser.h>
#include <pthread.h>
#include <environment.h>

typedef enum {
    PROTOCOL_OBJECT_NONE = 0,
    PROTOCOL_OBJECT_ENVIRONMENT, // Describes environment type
    PROTOCOL_OBJECT_MASCOT, // Describes mascot type
    PROTOCOL_OBJECT_PLUGIN, // Describes plugin type
    PROTOCOL_OBJECT_PROTOTYPE, // Describes prototype type
    PROTOCOL_OBJECT_SELECTION, // Describes selection type
    PROTOCOL_OBJECT_IMPORT, // Describes import type
    PROTOCOL_OBJECT_EXPORT, // Describes export type
    PROTOCOL_OBJECT_SHM_POOL, // Describes shared memory pool type
    PROTOCOL_OBJECT_BUFFER, // Describes buffer type
    PROTOCOL_OBJECT_POPUP, // Describes popup type
    PROTOCOL_OBJECT_CLICK, // Describes click event
    PROTOCOL_OBJECT_TYPES_MAX // Maximum number of protocol object types
} protocol_object_type_t;

struct protocol_object {
    uint32_t id;
    protocol_object_type_t type;
    void* data;
};

struct protocol_client {
    ipc_connector_t* connector;
    struct list* objects; // List of protocol objects that client owns

    uint64_t protocol_version;

    bool initialized; // Is client initialized? If not, any packets aside Client Hello is protocol violation and client will be disconnected.
};

typedef struct protocol_click_event protocol_click_event_t;
typedef struct protocol_selection protocol_selection_t;
typedef struct protocol_import protocol_import_t;
typedef struct protocol_export protocol_export_t;
typedef struct protocol_shm_pool protocol_shm_pool_t;
typedef struct protocol_buffer protocol_buffer_t;
typedef struct protocol_popup protocol_popup_t;

struct protocol_server_state {
    struct list* clients;
    struct list* environments;
    struct list* plugins;

    // In progress imports and exports
    struct list* imports;
    struct list* exports;

    protocol_click_event_t* last_click_event;
    protocol_selection_t* active_selection;

    mascot_prototype_store* prototypes;
    struct mascot_affordance_manager affordance_manager;

    char configuration_root  [PATH_MAX];
    char prototypes_location [PATH_MAX];
    char plugins_location    [PATH_MAX];
    char configuration_file  [PATH_MAX];

    bool    initialization_result;
    char ** initialization_errors;
    uint8_t initialization_errors_count;

    bool stop;

    pthread_mutex_t environment_mutex;
    pthread_mutex_t prototypes_mutex;
    pthread_mutex_t clients_mutex;
};

typedef enum {
    PROTOCOL_CLIENT_HANDLED = 0,
    PROTOCOL_CLIENT_UNHANDLED, // If not handled, packet is dropped
    PROTOCOL_CLIENT_VIOLATION, // If packet is invalid, connection is dropped
    PROTOCOL_INVALID_ARGUMENT,
} protocol_error;

void protocol_set_server_state(struct protocol_server_state* state);
struct protocol_server_state* protocol_get_server_state();

struct protocol_client* protocol_accept_connection(ipc_connector_t* connector);
void protocol_disconnect_client(struct protocol_client* client); // Acts as destructor for protocol_client

void protocol_server_broadcast_packet(ipc_packet_t* packet);

protocol_error protocol_client_handle_packet(struct protocol_client* client, ipc_packet_t* packet);

void protocol_init();

// Broadcasters

// Broadcast a new environment to all clients or to a specific one
void protocol_server_announce_new_environment(environment_t* environment, struct protocol_client* client);
void protocol_server_environment_widthdraw(environment_t* environment);
void protocol_server_environment_changed(environment_t* environment);
void protocol_server_environment_emit_mascot(environment_t* environment, struct mascot* mascot);

void protocol_server_announce_new_prototype(struct mascot_prototype* prototype, struct protocol_client* client);
void protocol_server_prototype_withdraw(struct mascot_prototype* prototype);

void protocol_server_mascot_migrated(struct mascot* mascot, environment_t* new_environment);
void protocol_server_mascot_destroyed(struct mascot* mascot);

protocol_click_event_t* protocol_server_announce_new_click(struct mascot* mascot, uint32_t x, uint32_t y);
protocol_popup_t* protocol_server_click_accept(struct protocol_client* client, uint32_t popup_width, uint32_t popup_height, uint32_t id);

protocol_selection_t* protocol_server_start_selection(struct list* environments, struct protocol_client* author, uint32_t id);
void protocol_selection_cancel(protocol_selection_t* selection);

protocol_import_t* protocol_server_import(struct protocol_client* client, int32_t fd, uint32_t id, uint8_t flags);
protocol_export_t* protocol_server_export(struct protocol_client* client, int32_t fd, uint32_t id, struct mascot_prototype* prototype);

protocol_import_t* protocol_server_ephermal_import(int32_t id);
protocol_export_t* protocol_server_ephermal_export(int32_t id);
protocol_shm_pool_t* protocol_server_ephermal_shm_pool(int32_t id);
protocol_popup_t* protocol_server_ephermal_popup(int32_t id);

uint32_t protocol_click_event_id(protocol_click_event_t* event);
uint32_t protocol_selection_id(protocol_selection_t* selection);
uint32_t protocol_import_id(protocol_import_t* import);
uint32_t protocol_export_id(protocol_export_t* export);
uint32_t protocol_shm_pool_id(protocol_shm_pool_t* pool);
uint32_t protocol_buffer_id(protocol_buffer_t* buffer);
uint32_t protocol_popup_id(protocol_popup_t* popup);

protocol_shm_pool_t* protocol_shm_pool_new(struct protocol_client* client, uint32_t id, int32_t fd, uint32_t size);
protocol_buffer_t* protocol_shm_pool_buffer_new(struct protocol_client* client, protocol_shm_pool_t* pool, uint32_t id, uint32_t width, uint32_t height, uint32_t stride, uint32_t format, uint32_t offset);
void protocol_shm_pool_destroy(protocol_shm_pool_t* pool);
void protocol_buffer_destroy(protocol_buffer_t* buffer);

protocol_popup_t* protocol_popup_child_popup(struct protocol_client* client, protocol_popup_t* popup, int32_t x, int32_t y, uint32_t width, uint32_t height, uint32_t id);
void protocol_popup_mapped(protocol_popup_t* popup);
void protocol_popup_dismissed(protocol_popup_t* popup);
void protocol_popup_enter(protocol_popup_t* popup, int32_t x, int32_t y);
void protocol_popup_attach(protocol_popup_t* popup, protocol_buffer_t* buffer, int32_t x, int32_t y, uint32_t width, uint32_t height);
void protocol_popup_leave(protocol_popup_t* popup);
void protocol_popup_clicked(protocol_popup_t* popup, int32_t x, int32_t y, uint32_t button);
void protocol_popup_motion(protocol_popup_t* popup, int32_t x, int32_t y);
void protocol_popup_dismiss(struct protocol_client* client, protocol_popup_t* popup);
void protocol_popup_frame(protocol_popup_t* popup);

bool protocol_click_event_is_expired(protocol_click_event_t* event);

int32_t protocol_client_push_object(struct protocol_client* client, uint32_t id, uint32_t type, void* object);
void* protocol_client_find_object(struct protocol_client* client, uint32_t id);
void* protocol_client_remove_object(struct protocol_client* client, uint32_t id);

#endif
