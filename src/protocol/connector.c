#include "connector.h"
#include <utils.h>
#include <malloc.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <list.h>

struct __attribute__((packed,aligned(__alignof__(uint32_t)))) ipc_header {
    uint8_t type;
    uint8_t flags;
    uint16_t length;
    uint32_t object_id;
};

struct ipc_packet {
    struct ipc_header* header; // Header of the packet, points to the start of the packet data
    uint8_t* buffer;

    int32_t fd_list[10];

    uint16_t reader_position;
    uint8_t  fd_reader_position;
    uint8_t  fd_writer_position;

    uint16_t *writer_position;
    uint16_t allocated_length;
};

struct ipc_connector {
    int32_t fd;
    struct list* queue;
    int32_t epoll_fd;
    void* userdata;
    uint8_t mode;
};

ipc_packet_t* ipc_allocate_packet(uint16_t length)
{

    if (UINT16_MAX - length < 8) {
        WARN("[BUG] Bad ipc allocation attempt: Requested %u bytes, which doesn't give enough space for the header", length);
        return NULL;
    }

    ipc_packet_t* packet = calloc(1, sizeof(struct ipc_packet));
    if (!packet) {
        WARN("[BUG] Failed to allocate ipc packet");
        return NULL;
    }

    uint8_t* packet_buffer = calloc(1, length + 8);
    if (!packet_buffer) {
        WARN("[BUG] Failed to allocate buffer with length %u for ipc packet", length + 8);
        free(packet);
        return NULL;
    }

    packet->header = (struct ipc_header*)packet_buffer;
    packet->buffer = packet_buffer;

    packet->writer_position = &packet->header->length;
    packet->allocated_length = length + 8;
    *packet->writer_position = 8;
    packet->reader_position = 8;

    return packet;
}

void ipc_free_packet(ipc_packet_t* packet)
{

    for (uint8_t i = 0; i < packet->fd_writer_position; i++) {
        close(packet->fd_list[i]);
    }

    free(packet->buffer);
    free(packet);
}

uint8_t ipc_packet_get_type(ipc_packet_t* packet)
{
    return packet->header->type;
}

uint16_t ipc_packet_get_length(ipc_packet_t* packet)
{
    return packet->header->length;
}

uint8_t ipc_packet_get_flags(ipc_packet_t* packet)
{
    return packet->header->flags;
}

uint32_t ipc_packet_get_object(ipc_packet_t* packet)
{
    return packet->header->object_id;
}


// Writers start here
int32_t ipc_packet_copy_to(ipc_packet_t* packet, const void* data, uint16_t length)
{
    if (packet->allocated_length - packet->header->length < length) {
        WARN("[BUG] ipc_packet_copy_to: Not enough space in packet");
        return -1;
    }

    memcpy(packet->buffer + packet->header->length, data, length);
    packet->header->length += length;

    return 0;
}

int32_t ipc_packet_write_uint8(ipc_packet_t *packet, uint8_t value)
{
    if (packet->allocated_length - packet->header->length < 1) {
        WARN("[BUG] ipc_packet_write_uint8: Not enough space in packet");
        return -1;
    }

    packet->buffer[packet->header->length++] = value;

    return 0;
}

int32_t ipc_packet_write_uint16(ipc_packet_t *packet, uint16_t value)
{
    return ipc_packet_copy_to(packet, (void*)&value, sizeof(value));
}

int32_t ipc_packet_write_uint32(ipc_packet_t *packet, uint32_t value)
{
    return ipc_packet_copy_to(packet, (void*)&value, sizeof(value));
}

int32_t ipc_packet_write_uint64(ipc_packet_t *packet, uint64_t value)
{
    return ipc_packet_copy_to(packet, (void*)&value, sizeof(value));
}

int32_t ipc_packet_write_string(ipc_packet_t *packet, const char* str)
{
    ssize_t length = str ? strlen(str) : 0;

    if (packet->allocated_length - packet->header->length < length + 1) {
        WARN("[BUG] ipc_packet_write_string: Not enough space in packet");
        return -1;
    }

    ipc_packet_write_uint8(packet, (uint8_t)length);
    if (length) ipc_packet_copy_to(packet, str, (uint16_t)length);
    return 0;
}

int32_t ipc_packet_write_bytes(ipc_packet_t *packet, const void *data, uint16_t length)
{
    if (packet->allocated_length - packet->header->length < length + 1) {
        WARN("[BUG] ipc_packet_write_bytes: Not enough space in packet");
        return -1;
    }

    ipc_packet_write_uint16(packet, length);
    ipc_packet_copy_to(packet, data, length);

    return 0;
}

int32_t ipc_packet_write_float(ipc_packet_t *packet, float value)
{
    return ipc_packet_copy_to(packet, (void*)&value, sizeof(value));
}

int32_t ipc_packet_write_double(ipc_packet_t *packet, double value)
{
    return ipc_packet_copy_to(packet, (void*)&value, sizeof(value));
}

int32_t ipc_packet_put_fd(ipc_packet_t *packet, int32_t fd)
{
    if (fd < 0) {
        WARN("[BUG] ipc_packet_put_fd: Invalid file descriptor");
        return -EINVAL;
    }

    if (packet->fd_writer_position >= 10) {
        WARN("[BUG] ipc_packet_put_fd: Too many file descriptors");
        return -EINVAL;
    }

    int dupped_fd = dup(fd);
    if (dupped_fd == -1) {
        WARN("[BUG] ipc_packet_put_fd: Failed to duplicate file descriptor");
        return -errno;
    }

    packet->fd_list[packet->fd_writer_position++] = dupped_fd;
    return 0;
}

// Reader section

int32_t ipc_packet_copy_from(ipc_packet_t *packet, const void *data, uint16_t length)
{
    if (packet->header->length - packet->reader_position < length) {
        WARN("[BUG] ipc_packet_copy_from: Not enough data to read");
        return -1;
    }

    memcpy((void*)data, packet->buffer + packet->reader_position, length);
    packet->reader_position += length;

    return 0;
}

int32_t ipc_packet_read_uint8(ipc_packet_t *packet, uint8_t *value)
{
    if (packet->header->length - packet->reader_position < 1) {
        WARN("[BUG] ipc_packet_read_uint8: Not enough data to read");
        return -1;
    }

    *value = *(uint8_t*)(packet->buffer + packet->reader_position);
    packet->reader_position += 1;

    return 0;
}

int32_t ipc_packet_read_uint16(ipc_packet_t *packet, uint16_t *value)
{
    return ipc_packet_copy_from(packet, value, sizeof(uint16_t));
}

int32_t ipc_packet_read_uint32(ipc_packet_t *packet, uint32_t *value)
{
    return ipc_packet_copy_from(packet, value, sizeof(uint32_t));
}

int32_t ipc_packet_read_uint64(ipc_packet_t *packet, uint64_t *value)
{
    return ipc_packet_copy_from(packet, value, sizeof(uint64_t));
}

int32_t ipc_packet_read_string(ipc_packet_t *packet, char *value, uint8_t* length)
{
    uint8_t string_length;

    if (ipc_packet_read_uint8(packet, &string_length)) return -1;

    if (packet->header->length - packet->reader_position < string_length) {
        WARN("[BUG] ipc_packet_read_string: Not enough data to read");
        return -1;
    }

    memcpy(value, packet->buffer + packet->reader_position, string_length);
    packet->reader_position += string_length;

    *length = string_length;

    return 0;
}

int32_t ipc_packet_read_bytes(ipc_packet_t *packet, void *data, uint16_t length)
{
    uint16_t data_length;
    if (ipc_packet_read_uint16(packet, &data_length)) return -1;

    if (data_length > length) {
        WARN("[BUG] ipc_packet_read_bytes: String too long for buffer");
        return -1;
    }

    if (packet->header->length - packet->reader_position < data_length) {
        WARN("[BUG] ipc_packet_read_bytes: Not enough data to read");
        return -1;
    }

    memcpy(data, packet->buffer + packet->reader_position, length);
    packet->reader_position += length;

    return 0;
}

int32_t ipc_packet_read_float(ipc_packet_t *packet, float *value)
{
    uint32_t float_value;
    if (ipc_packet_read_uint32(packet, &float_value)) return -1;

    *value = *(float*)&float_value;

    return 0;
}

int32_t ipc_packet_read_double(ipc_packet_t *packet, double *value)
{
    uint64_t double_value;
    if (ipc_packet_read_uint64(packet, &double_value)) return -1;

    *value = *(double*)&double_value;

    return 0;
}

int32_t ipc_packet_consume_fd(ipc_packet_t *packet, int32_t *fd)
{
    if (packet->fd_writer_position <= packet->fd_reader_position) {
        WARN("[BUG] ipc_packet_consume_fd: Packet doesn't contain enough file descriptors");
        return -1;
    }

    *fd = packet->fd_list[packet->fd_reader_position++];

    return 0;
}

void ipc_packet_set_type(ipc_packet_t* packet, uint8_t type)
{
    packet->header->type = type;
}

void ipc_packet_set_flags(ipc_packet_t *packet, uint8_t flags)
{
    packet->header->flags = flags;
}

void ipc_packet_set_length(ipc_packet_t *packet, uint16_t length)
{
    packet->header->length = length;
}

void ipc_packet_set_object(ipc_packet_t *packet, uint32_t id)
{
    packet->header->object_id = id;
}

// IO

int32_t ipc_connector_receive(ipc_connector_t *connector, ipc_packet_t **packet)
{
    if (!connector || !packet) return -EINVAL;

    int32_t expected_length = recv(connector->fd, NULL, 0, MSG_PEEK | MSG_DONTWAIT | MSG_TRUNC);
    if (expected_length < 0) return -errno;
    if (expected_length < 8) {
        close(connector->fd);
        return -EAGAIN;
    }

    if (expected_length > UINT16_MAX) {
        WARN("[BUG] ipc_connector_receive: Packet length exceeds maximum");
        close(connector->fd);
        return -E2BIG;
    }

    // Start building iovec for UNIX message with file descriptors
    ipc_packet_t* received_packet = calloc(1, sizeof(ipc_packet_t));
    if (!received_packet) return -ENOMEM;

    received_packet->buffer = malloc(expected_length);
    if (!received_packet->buffer) {
        free(received_packet);
        return -ENOMEM;
    }

    received_packet->allocated_length = expected_length;
    received_packet->header = (struct ipc_header*)received_packet->buffer;

    struct iovec iovector = {
        .iov_base = received_packet->buffer,
        .iov_len = expected_length
    };

    char control_buffer[CMSG_SPACE(sizeof(int)*10)] = {0};
    struct msghdr message = {
        .msg_iov = &iovector,
        .msg_iovlen = 1,
        .msg_control = control_buffer,
        .msg_controllen = sizeof(control_buffer)
    };

    ssize_t received_bytes = recvmsg(connector->fd, &message, 0);
    if (received_bytes < 0) {
        free(received_packet->buffer);
        free(received_packet);
        return -errno;
    }

    if (received_bytes != expected_length) {
        WARN("Received %zd bytes, expected %d", received_bytes, expected_length);
        free(received_packet->buffer);
        free(received_packet);
        return -EIO;
    }

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL; cmsg = CMSG_NXTHDR(&message, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            int num_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            if (num_fds > 10) {
                WARN("Received %d file descriptors from client, expected at most 10", num_fds);
                free(received_packet->buffer);
                free(received_packet);
                return -E2BIG;
            }
            for (int i = 0; i < num_fds; i++) {
                int fd = ((int*)CMSG_DATA(cmsg))[i];
                if (fd < 0) {
                    WARN("Received invalid file descriptor %d", fd);
                    free(received_packet->buffer);
                    free(received_packet);
                    return -EBADF;
                }
                if (ipc_packet_put_fd(received_packet, fd)) {
                    free(received_packet->buffer);
                    free(received_packet);
                    return -ENOMEM;
                }
            }
        }
    }

    received_packet->reader_position = 8;

    *packet = received_packet;
    return 0;
}

int32_t ipc_connector_send(ipc_connector_t* connector, ipc_packet_t* packet)
{
    if (!connector || !packet) return -EINVAL;

    if (connector->mode) {
        list_add(connector->queue, packet);
        return 0;
    }

    // Build unix message to send
    struct iovec iovector = {
        .iov_base = packet->buffer,
        .iov_len = packet->header->length
    };


    struct msghdr message = {
        .msg_iov = &iovector,
        .msg_iovlen = 1
    };

    char control_buffer[CMSG_SPACE(sizeof(int) * packet->fd_writer_position)];
    memset(control_buffer, 0, sizeof(control_buffer));
    if (packet->fd_writer_position) {
        message.msg_control = control_buffer;
        message.msg_controllen = CMSG_SPACE(sizeof(int) * packet->fd_writer_position);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * packet->fd_writer_position);
        memcpy(CMSG_DATA(cmsg), packet->fd_list, sizeof(int) * packet->fd_writer_position);
    }

    if (sendmsg(connector->fd, &message, MSG_DONTWAIT) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!connector->mode) {
                struct epoll_event event = {
                    .events = EPOLLIN | EPOLLRDHUP | EPOLLOUT,
                    .data.ptr = connector->userdata
                };
                if (epoll_ctl(connector->epoll_fd, EPOLL_CTL_MOD, connector->fd, &event) < 0) {
                    WARN("Failed to modify epoll event");
                    ipc_free_packet(packet);
                    return -errno;
                }
                connector->mode = 1;
            }
            list_add(connector->queue, packet);
            return 0;
        }
        ipc_free_packet(packet);
        return -errno;
    }

    ipc_free_packet(packet);
    return 0;
}

ipc_connector_t* ipc_initialize_connector(int32_t connector_fd, int32_t epoll_fd, void* userdata)
{
    ipc_connector_t* connector = calloc(1, sizeof(ipc_connector_t));
    if (!connector) return NULL;

    connector->fd = connector_fd;
    connector->queue = list_init(16);
    connector->epoll_fd = epoll_fd;
    connector->userdata = userdata;

    struct epoll_event event = {
        .events = EPOLLIN | EPOLLRDHUP,
        .data.ptr = userdata
    };

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connector->fd, &event) < 0) {
        WARN("Failed to add connector fd to epoll");
    }

    return connector;
}

void ipc_destroy_connector(ipc_connector_t* connector)
{
    if (!connector) return;

    epoll_ctl(connector->epoll_fd, EPOLL_CTL_DEL, connector->fd, NULL);

    for (uint32_t i = 0; i < list_size(connector->queue); i++) {
        ipc_packet_t* packet = list_get(connector->queue, i);
        if (packet) ipc_free_packet(packet);
    }

    list_free(connector->queue);
    free(connector);
}

int32_t ipc_connector_flush(ipc_connector_t* connector)
{
    if (!connector) return -EINVAL;

    if (connector->mode == 1) {
        struct epoll_event event = {
            .events = EPOLLIN | EPOLLRDHUP,
            .data.ptr = connector->userdata
        };

        if (epoll_ctl(connector->epoll_fd, EPOLL_CTL_MOD, connector->fd, &event) < 0) {
            WARN("Failed to modify connector fd in epoll");
        }

        connector->mode = 0;
    }

    struct list* queue = connector->queue;
    connector->queue = list_init(16);

    for (uint32_t i = 0; i < list_size(queue); i++) {
        ipc_packet_t* packet = list_get(queue, i);
        if (packet) {
            ipc_connector_send(connector, packet);
        }
    }

    list_free(queue);
    return 0;
}
