#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stdint.h>

#define IO_UNLINKAT_RECURSIVE 1

#define IO_INVALID_ARGUMENTS -1
#define IO_NO_SUCH_FILE_OR_DIRECTORY -2
#define IO_UNKNOWN_ERROR -1234

#define IO_RECURSIVE 0x1
#define IO_FILE_TYPE_REGULAR 0x2
#define IO_FILE_TYPE_DIRECTORY 0x4
#define IO_CASE_INSENSITIVE 0x8

int32_t io_find(const char* path, const char* pattern, int32_t flags, char*** results, int32_t* count);
int32_t io_findat(int32_t fd, const char* path, const char* pattern, int32_t recursive, int32_t file_type, char*** results, int32_t* count);
int32_t io_copydir(const char* src, const char* dst);
int32_t io_copydir_at(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path);
int32_t io_rename(const char* src, const char* dst, int32_t flags);
int32_t io_renameat(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path, int32_t flags);
int32_t io_swap(const char* src, const char* dst);
int32_t io_swapat(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path);
int32_t io_unlink(const char* path, int32_t flags);
int32_t io_unlinkat(int32_t fd, const char* path, int32_t flags);
int32_t io_mkdtempat(int dirfd, char *template);

#endif
