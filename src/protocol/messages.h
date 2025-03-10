#ifndef PROTOCOL_MESSAGES_H
#define PROTOCOL_MESSAGES_H

#include "connector.h"
#include "environment.h"
#include "server.h"

#define NOTICE_SEVERITY_INFO    0
#define NOTICE_SEVERITY_WARNING 1
#define NOTICE_SEVERITY_ERROR   2

bool protocol_handler_client_hello(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_mascot_get_info(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_select(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_reload_prototype(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_spawn(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_dispose(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_environment_close(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_plugin_set_policy(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_plugin_restore_windows(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_plugin_deactivate(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_plugin_activate(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_selection_cancel(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_share_shm_pool(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_shm_pool_create_buffer(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_shm_pool_destroy_buffer(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_buffer_destroy(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_click_event_accept(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_click_event_ignore(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_popup_child_popup(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_popup_attach(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_popup_discard(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_apply_behavior(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_set_config_key(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_get_config_key(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_list_config_keys(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_import(struct protocol_client* client, ipc_packet_t* packet);
bool protocol_handler_export(struct protocol_client* client, ipc_packet_t* packet);

ipc_packet_t* protocol_builder_server_hello();
ipc_packet_t* protocol_builder_disconnect();
ipc_packet_t* protocol_builder_notice(uint8_t severity, const char* message, const char** format_parts, uint8_t format_parts_count, bool alert);
ipc_packet_t* protocol_builder_start();
ipc_packet_t* protocol_builder_environment(environment_t* environment);
ipc_packet_t* protocol_builder_environment_changed(environment_t* environment);
ipc_packet_t* protocol_builder_environment_mascot(environment_t* environment, struct mascot* mascot);
ipc_packet_t* protocol_builder_environment_withdrawn(environment_t* environment);
ipc_packet_t* protocol_builder_prototype(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_name(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_display_name(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_path(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_fd(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_action(struct mascot_prototype* prototype, const struct mascot_action* action);
ipc_packet_t* protocol_builder_prototype_behavior(struct mascot_prototype* prototype, const struct mascot_behavior* behavior);
ipc_packet_t* protocol_builder_prototype_icon(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_author(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_prototype_version(struct mascot_prototype* prototype);
ipc_packet_t* protocol_builder_commit_prototypes();
ipc_packet_t* protocol_builder_mascot_migrated(struct mascot* mascot, environment_t* environment);
ipc_packet_t* protocol_builder_mascot_disposed(struct mascot* mascot);
ipc_packet_t* protocol_builder_mascot_info(struct mascot* mascot);
ipc_packet_t* protocol_builder_mascot_clicked(struct mascot* mascot, protocol_click_event_t* event);
ipc_packet_t* protocol_builder_selection_done(protocol_selection_t* selection, struct mascot* mascot, environment_t* environment, uint32_t x, uint32_t y, uint32_t locx, uint32_t locy);
ipc_packet_t* protocol_builder_selection_canceled(protocol_selection_t* selection);
ipc_packet_t* protocol_builder_import_started(protocol_import_t* import);
ipc_packet_t* protocol_builder_import_finished(protocol_import_t* import, const char* path);
ipc_packet_t* protocol_builder_import_progress(protocol_import_t* import, float progress);
ipc_packet_t* protocol_builder_import_failed(protocol_import_t* import, uint32_t error_code);
ipc_packet_t* protocol_builder_export_failed(protocol_export_t* export, uint32_t error_code);
ipc_packet_t* protocol_builder_export_finished(protocol_export_t* export);
ipc_packet_t* protocol_builder_click_event_expired(protocol_click_event_t* event);
ipc_packet_t* protocol_builder_config_key(const char* key, const char* value);
ipc_packet_t* protocol_builder_prototype_withdrawn(struct mascot_prototype* prototype);


#endif
