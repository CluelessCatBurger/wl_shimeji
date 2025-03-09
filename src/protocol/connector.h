#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <master_header.h>

typedef struct ipc_connector ipc_connector_t;
typedef struct ipc_packet ipc_packet_t;

ipc_connector_t* ipc_initialize_connector(int32_t connector_fd, int32_t epoll_fd, void* userdata);
void ipc_destroy_connector(ipc_connector_t* connector);

ipc_packet_t* ipc_allocate_packet(uint16_t max_size);
void ipc_free_packet(ipc_packet_t* packet);

// Copy data as is
int32_t ipc_packet_copy_to(ipc_packet_t* packet, const void* data, uint16_t length);
int32_t ipc_packet_copy_from(ipc_packet_t* packet, const void* data, uint16_t length);

// Header manipulation
void ipc_packet_set_type(ipc_packet_t* packet, uint8_t type);
void ipc_packet_set_length(ipc_packet_t* packet, uint16_t length);
void ipc_packet_set_flags(ipc_packet_t* packet, uint8_t flags);
void ipc_packet_set_object(ipc_packet_t* packet, uint32_t object_id);
uint8_t ipc_packet_get_type(ipc_packet_t* packet);
uint16_t ipc_packet_get_length(ipc_packet_t* packet);
uint8_t ipc_packet_get_flags(ipc_packet_t* packet);
uint32_t ipc_packet_get_object(ipc_packet_t* packet);

// Writers
int32_t ipc_packet_write_uint8  (ipc_packet_t* packet, uint8_t value);
int32_t ipc_packet_write_uint16 (ipc_packet_t* packet, uint16_t value);
int32_t ipc_packet_write_uint32 (ipc_packet_t* packet, uint32_t value);
int32_t ipc_packet_write_uint64 (ipc_packet_t* packet, uint64_t value);
int32_t ipc_packet_write_string (ipc_packet_t* packet, const char* str);
int32_t ipc_packet_write_bytes  (ipc_packet_t* packet, const void* data, uint16_t length);
int32_t ipc_packet_write_float  (ipc_packet_t* packet, float value);
int32_t ipc_packet_write_double (ipc_packet_t* packet, double value);
int32_t ipc_packet_put_fd       (ipc_packet_t* packet, int32_t fd);

// Readers
int32_t ipc_packet_read_uint8  (ipc_packet_t* packet, uint8_t* value);
int32_t ipc_packet_read_uint16 (ipc_packet_t* packet, uint16_t* value);
int32_t ipc_packet_read_uint32 (ipc_packet_t* packet, uint32_t* value);
int32_t ipc_packet_read_uint64 (ipc_packet_t* packet, uint64_t* value);
int32_t ipc_packet_read_string (ipc_packet_t* packet, char* str, uint8_t* length);
int32_t ipc_packet_read_bytes  (ipc_packet_t* packet, void* data, uint16_t length);
int32_t ipc_packet_read_float  (ipc_packet_t* packet, float* value);
int32_t ipc_packet_read_double (ipc_packet_t* packet, double* value);
int32_t ipc_packet_consume_fd  (ipc_packet_t* packet, int32_t* fd);

// Connector functions
int32_t ipc_connector_send(ipc_connector_t* connector, ipc_packet_t* packet);
int32_t ipc_connector_receive(ipc_connector_t* connector, ipc_packet_t** packet);
int32_t ipc_connector_flush(ipc_connector_t* connector);

#endif
