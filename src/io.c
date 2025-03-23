#include <linux/limits.h>
#define _GNU_SOURCE
#include "io.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <fnmatch.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/***************** io_find Implementation *****************/

/*
 * int32_t io_find(const char* path, const char* pattern, int32_t flags,
 *                 char*** results, int32_t* count);
 *
 * Searches for files/directories (skipping names starting with '.')
 * in the directory "path" matching "pattern" (if pattern is NULL, "*" is used).
 * If IO_RECURSIVE is set in flags, subdirectories are searched recursively.
 * Filtering by type is applied if IO_FILE_TYPE_REGULAR or IO_FILE_TYPE_DIRECTORY
 * is specified. The returned names are relative to "path".
 */
int32_t io_find(const char* path, const char* pattern, int32_t flags,
                char*** results, int32_t* count) {
    if (!path || !results || !count)
        return IO_INVALID_ARGUMENTS;
    if (!pattern)
        pattern = "*";

    int32_t res_cap = 16;
    *results = malloc(res_cap * sizeof(char*));
    if (!*results)
        return IO_UNKNOWN_ERROR;
    *count = 0;

    /* For recursion we maintain a simple stack of relative directory paths.
       The base directory is represented by the empty string. */
    typedef struct {
        char* rel;
    } StackElem;
    int32_t stack_cap = 16;
    int32_t stack_count = 0;
    StackElem* stack = malloc(stack_cap * sizeof(StackElem));
    if (!stack) {
        free(*results);
        return IO_UNKNOWN_ERROR;
    }
    stack[stack_count].rel = strdup("");
    if (!stack[stack_count].rel) {
        free(stack);
        free(*results);
        return IO_UNKNOWN_ERROR;
    }
    stack_count++;

    while (stack_count > 0) {
        /* Pop a directory from the stack */
        StackElem cur = stack[stack_count - 1];
        stack_count--;

        /* Build full directory path: if cur.rel is empty, use path; otherwise join them */
        char full_path[1024];
        if (cur.rel[0] == '\0')
            snprintf(full_path, sizeof(full_path), "%s", path);
        else
            snprintf(full_path, sizeof(full_path), "%s/%s", path, cur.rel);

        DIR* dir = opendir(full_path);
        if (!dir) {
            free(cur.rel);
            /* If base directory is unreadable, return error */
            if (cur.rel[0] == '\0') {
                free(stack);
                for (int i = 0; i < *count; i++)
                    free((*results)[i]);
                free(*results);
                return IO_NO_SUCH_FILE_OR_DIRECTORY;
            }
            continue;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            /* Skip ".", "..", and names starting with '.' */
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0 ||
                entry->d_name[0] == '.')
                continue;

            char rel_entry[1024];
            if (cur.rel[0] == '\0')
                snprintf(rel_entry, sizeof(rel_entry), "%s", entry->d_name);
            else
                snprintf(rel_entry, sizeof(rel_entry), "%s/%s", cur.rel, entry->d_name);

            /* Check pattern match on entry name */
            int match = (fnmatch(pattern, entry->d_name, 0) == 0);
            int is_reg = (entry->d_type == DT_REG);
            int is_dir = (entry->d_type == DT_DIR);

            /* Determine if we add this entry based on type filtering */
            int add = 0;
            if (match) {
                if (flags & (IO_FILE_TYPE_REGULAR | IO_FILE_TYPE_DIRECTORY)) {
                    if ((flags & IO_FILE_TYPE_REGULAR) && is_reg)
                        add = 1;
                    if ((flags & IO_FILE_TYPE_DIRECTORY) && is_dir)
                        add = 1;
                } else {
                    add = 1;
                }
            }
            if (add) {
                if (*count >= res_cap) {
                    int32_t new_cap = res_cap * 2;
                    char** temp = realloc(*results, new_cap * sizeof(char*));
                    if (!temp) {
                        closedir(dir);
                        for (int i = 0; i < stack_count; i++)
                            free(stack[i].rel);
                        free(stack);
                        for (int i = 0; i < *count; i++)
                            free((*results)[i]);
                        free(*results);
                        free(cur.rel);
                        return IO_UNKNOWN_ERROR;
                    }
                    *results = temp;
                    res_cap = new_cap;
                }
                (*results)[*count] = strdup(rel_entry);
                if (!(*results)[*count]) {
                    closedir(dir);
                    for (int i = 0; i < stack_count; i++)
                        free(stack[i].rel);
                    free(stack);
                    for (int i = 0; i < *count; i++)
                        free((*results)[i]);
                    free(*results);
                    free(cur.rel);
                    return IO_UNKNOWN_ERROR;
                }
                (*count)++;
            }

            /* If recursion is enabled and this entry is a directory,
               push it onto the stack unless we are filtering directories
               and the name already matched (in which case we don’t descend). */
            if ((flags & IO_RECURSIVE) && is_dir) {
                if ((flags & IO_FILE_TYPE_DIRECTORY) && match) {
                    /* Do not descend */
                } else {
                    char* new_rel = strdup(rel_entry);
                    if (!new_rel)
                        continue;
                    if (stack_count >= stack_cap) {
                        int32_t new_stack_cap = stack_cap * 2;
                        StackElem* temp = realloc(stack, new_stack_cap * sizeof(StackElem));
                        if (!temp) {
                            free(new_rel);
                            continue;
                        }
                        stack = temp;
                        stack_cap = new_stack_cap;
                    }
                    stack[stack_count].rel = new_rel;
                    stack_count++;
                }
            }
        }
        closedir(dir);
        free(cur.rel);
    }
    free(stack);
    return 0;
}

/*
 * int32_t io_find_at(int32_t atfd, const char* path, const char* pattern,
 *                    int32_t flags, char*** results, int32_t* count);
 *
 * Similar to io_find, but the lookup is anchored at file descriptor 'atfd'.
 * If path is the empty string (""), it is treated as AT_EMPTYPATH and the directory
 * referenced by atfd is used as the base.
 */
int32_t io_find_at(int32_t atfd, const char* path, const char* pattern,
                   int32_t flags, char*** results, int32_t* count) {
    if (!results || !count)
        return IO_INVALID_ARGUMENTS;
    if (!pattern)
        pattern = "*";

    int base_fd;
    int base_fd_is_dup = 0;
    if (path[0] == '\0') {
        base_fd = atfd;
    } else {
        base_fd = openat(atfd, path, O_DIRECTORY | O_RDONLY);
        if (base_fd < 0)
            return IO_NO_SUCH_FILE_OR_DIRECTORY;
        base_fd_is_dup = 1;
    }

    int32_t res_cap = 16;
    *results = malloc(res_cap * sizeof(char*));
    if (!*results) {
        if (base_fd_is_dup)
            close(base_fd);
        return IO_UNKNOWN_ERROR;
    }
    *count = 0;

    typedef struct {
        char* rel;
    } StackElem;
    int32_t stack_cap = 16;
    int32_t stack_count = 0;
    StackElem* stack = malloc(stack_cap * sizeof(StackElem));
    if (!stack) {
        free(*results);
        if (base_fd_is_dup)
            close(base_fd);
        return IO_UNKNOWN_ERROR;
    }
    stack[stack_count].rel = strdup("");
    if (!stack[stack_count].rel) {
        free(stack);
        free(*results);
        if (base_fd_is_dup)
            close(base_fd);
        return IO_UNKNOWN_ERROR;
    }
    stack_count++;

    while (stack_count > 0) {
        StackElem cur = stack[stack_count - 1];
        stack_count--;

        int cur_fd;
        if (cur.rel[0] == '\0') {
            cur_fd = dup(base_fd);
            if (cur_fd < 0) {
                free(cur.rel);
                continue;
            }
        } else {
            cur_fd = openat(base_fd, cur.rel, O_DIRECTORY | O_RDONLY);
            if (cur_fd < 0) {
                free(cur.rel);
                continue;
            }
        }

        DIR* dir = fdopendir(cur_fd);
        if (!dir) {
            close(cur_fd);
            free(cur.rel);
            continue;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0 ||
                entry->d_name[0] == '.')
                continue;

            char rel_entry[1024];
            if (cur.rel[0] == '\0')
                snprintf(rel_entry, sizeof(rel_entry), "%s", entry->d_name);
            else
                snprintf(rel_entry, sizeof(rel_entry), "%s/%s", cur.rel, entry->d_name);

            int match = (fnmatch(pattern, entry->d_name, 0) == 0);
            int is_reg = (entry->d_type == DT_REG);
            int is_dir = (entry->d_type == DT_DIR);

            int add = 0;
            if (match) {
                if (flags & (IO_FILE_TYPE_REGULAR | IO_FILE_TYPE_DIRECTORY)) {
                    if ((flags & IO_FILE_TYPE_REGULAR) && is_reg)
                        add = 1;
                    if ((flags & IO_FILE_TYPE_DIRECTORY) && is_dir)
                        add = 1;
                } else {
                    add = 1;
                }
            }
            if (add) {
                if (*count >= res_cap) {
                    int32_t new_cap = res_cap * 2;
                    char** temp = realloc(*results, new_cap * sizeof(char*));
                    if (!temp) {
                        closedir(dir);
                        for (int i = 0; i < stack_count; i++)
                            free(stack[i].rel);
                        free(stack);
                        for (int i = 0; i < *count; i++)
                            free((*results)[i]);
                        free(*results);
                        free(cur.rel);
                        if (base_fd_is_dup)
                            close(base_fd);
                        return IO_UNKNOWN_ERROR;
                    }
                    *results = temp;
                    res_cap = new_cap;
                }
                (*results)[*count] = strdup(rel_entry);
                if (!(*results)[*count]) {
                    closedir(dir);
                    for (int i = 0; i < stack_count; i++)
                        free(stack[i].rel);
                    free(stack);
                    for (int i = 0; i < *count; i++)
                        free((*results)[i]);
                    free(*results);
                    free(cur.rel);
                    if (base_fd_is_dup)
                        close(base_fd);
                    return IO_UNKNOWN_ERROR;
                }
                (*count)++;
            }

            if ((flags & IO_RECURSIVE) && is_dir) {
                if ((flags & IO_FILE_TYPE_DIRECTORY) && match) {
                    /* Do not descend */
                } else {
                    char* new_rel = strdup(rel_entry);
                    if (!new_rel)
                        continue;
                    if (stack_count >= stack_cap) {
                        int32_t new_stack_cap = stack_cap * 2;
                        StackElem* temp = realloc(stack, new_stack_cap * sizeof(StackElem));
                        if (!temp) {
                            free(new_rel);
                            continue;
                        }
                        stack = temp;
                        stack_cap = new_stack_cap;
                    }
                    stack[stack_count].rel = new_rel;
                    stack_count++;
                }
            }
        }
        closedir(dir);
        free(cur.rel);
    }
    free(stack);
    if (base_fd_is_dup)
        close(base_fd);
    return 0;
}

/***************** io_copydir and io_copydir_at *****************/

/* Helper: Copies a file from src to dst. */
static int copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return IO_UNKNOWN_ERROR;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out, buf + written, n - written);
            if (w < 0) {
                close(in);
                close(out);
                return IO_UNKNOWN_ERROR;
            }
            written += w;
        }
    }
    close(in);
    close(out);
    if (n < 0)
        return IO_UNKNOWN_ERROR;
    return 0;
}

/* Recursively copies the directory tree from src to dst. */
static int copy_directory(const char* src, const char* dst) {
    struct stat st;
    if (stat(src, &st) < 0)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    if (mkdir(dst, st.st_mode) != 0 && errno != EEXIST)
        return IO_UNKNOWN_ERROR;

    DIR* d = opendir(src);
    if (!d)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    struct dirent* entry;
    int ret = 0;
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char src_path[4096], dst_path[4096];
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);
        if (entry->d_type == DT_DIR)
            ret = copy_directory(src_path, dst_path);
        else
            ret = copy_file(src_path, dst_path);
        if (ret != 0)
            break;
    }
    closedir(d);
    return ret;
}

/*
 * int32_t io_copydir(const char* src, const char* dst);
 *
 * Recursively copies the directory at 'src' to 'dst'.
 */
int32_t io_copydir(const char* src, const char* dst) {
    if (!src || !dst)
        return IO_INVALID_ARGUMENTS;
    return copy_directory(src, dst);
}

/*
 * int32_t io_copydir_at(int32_t src_fd, const char* src_path,
 *                       int32_t dst_fd, const char* dst_path);
 *
 * Recursively copies a directory tree. 'src_path' is relative to 'src_fd'
 * and 'dst_path' is relative to 'dst_fd'.
 */
static int copy_directory_at_internal(int src_fd, const char* src_path, int dst_fd, const char* dst_path) {
    int sfd = openat(src_fd, src_path, O_DIRECTORY | O_RDONLY);
    if (sfd < 0)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    struct stat st;
    if (fstatat(src_fd, src_path, &st, 0) < 0) {
        close(sfd);
        return IO_UNKNOWN_ERROR;
    }
    if (mkdirat(dst_fd, dst_path, st.st_mode) != 0 && errno != EEXIST) {
        close(sfd);
        return IO_UNKNOWN_ERROR;
    }
    DIR* d = fdopendir(sfd);
    if (!d) {
        close(sfd);
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    }
    struct dirent* entry;
    int ret = 0;
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char src_entry[4096], dst_entry[4096];
        snprintf(src_entry, sizeof(src_entry), "%s/%s", src_path, entry->d_name);
        snprintf(dst_entry, sizeof(dst_entry), "%s/%s", dst_path, entry->d_name);
        if (entry->d_type == DT_DIR)
            ret = copy_directory_at_internal(src_fd, src_entry, dst_fd, dst_entry);
        else {
            int in = openat(src_fd, src_entry, O_RDONLY);
            if (in < 0) { ret = IO_NO_SUCH_FILE_OR_DIRECTORY; break; }
            int out = openat(dst_fd, dst_entry, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out < 0) { close(in); ret = IO_UNKNOWN_ERROR; break; }
            char buf[4096];
            ssize_t n;
            while ((n = read(in, buf, sizeof(buf))) > 0) {
                ssize_t written = 0;
                while (written < n) {
                    ssize_t w = write(out, buf + written, n - written);
                    if (w < 0) {
                        close(in);
                        close(out);
                        ret = IO_UNKNOWN_ERROR;
                        goto cleanup;
                    }
                    written += w;
                }
            }
            close(in);
            close(out);
            if (n < 0) { ret = IO_UNKNOWN_ERROR; break; }
        }
    }
cleanup:
    closedir(d);
    return ret;
}

int32_t io_copydir_at(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path) {
    if (src_fd < 0 || dst_fd < 0 || !src_path || !dst_path)
        return IO_INVALID_ARGUMENTS;
    return copy_directory_at_internal(src_fd, src_path, dst_fd, dst_path);
}

/***************** io_rename, io_renameat, io_swap, io_swapat *****************/

/*
 * int32_t io_rename(const char* src, const char* dst, int32_t flags);
 *
 * Renames (moves) the file/directory from 'src' to 'dst'. If flags is nonzero,
 * the glibc-provided renameat2 is used (which supports flags such as RENAME_EXCHANGE).
 */
int32_t io_rename(const char* src, const char* dst, int32_t flags) {
    if (!src || !dst)
        return IO_INVALID_ARGUMENTS;
    int ret;
    if (flags != 0) {
        /* renameat2 is available in glibc with _GNU_SOURCE defined */
        ret = renameat2(AT_FDCWD, src, AT_FDCWD, dst, flags);
    } else {
        ret = rename(src, dst);
    }
    return (ret == 0) ? 0 : IO_UNKNOWN_ERROR;
}

/*
 * int32_t io_renameat(int32_t src_fd, const char* src_path,
 *                     int32_t dst_fd, const char* dst_path, int32_t flags);
 *
 * Renames (moves) the file/directory from src_path (relative to src_fd)
 * to dst_path (relative to dst_fd). Uses renameat2 if flags != 0.
 */
int32_t io_renameat(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path, int32_t flags) {
    if (!src_path || !dst_path)
        return IO_INVALID_ARGUMENTS;
    int ret;
    if (flags != 0) {
        ret = renameat2(src_fd, src_path, dst_fd, dst_path, flags);
    } else {
        ret = renameat(src_fd, src_path, dst_fd, dst_path);
    }
    return (ret == 0) ? 0 : IO_UNKNOWN_ERROR;
}

/*
 * int32_t io_swap(const char* src, const char* dst);
 *
 * Atomically swaps the names of 'src' and 'dst' using RENAME_EXCHANGE.
 */
int32_t io_swap(const char* src, const char* dst) {
    return io_renameat(AT_FDCWD, src, AT_FDCWD, dst, RENAME_EXCHANGE);
}

/*
 * int32_t io_swapat(int32_t src_fd, const char* src_path,
 *                   int32_t dst_fd, const char* dst_path);
 *
 * Atomically swaps the names of 'src_path' and 'dst_path' relative to the provided file descriptors.
 */
int32_t io_swapat(int32_t src_fd, const char* src_path, int32_t dst_fd, const char* dst_path) {
    return io_renameat(src_fd, src_path, dst_fd, dst_path, RENAME_EXCHANGE);
}

/***************** io_unlink and io_unlinkat *****************/

/* Helper: Recursively unlinks (deletes) a file/directory tree at path. */
static int unlink_recursive(const char* path) {
    struct stat st;
    if (lstat(path, &st) < 0)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path);
        if (!d)
            return IO_UNKNOWN_ERROR;
        struct dirent* entry;
        int ret = 0;
        while ((entry = readdir(d)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            ret = unlink_recursive(child);
            if (ret != 0)
                break;
        }
        closedir(d);
        if (ret == 0 && rmdir(path) != 0)
            ret = IO_UNKNOWN_ERROR;
        return ret;
    } else {
        return (unlink(path) == 0) ? 0 : IO_UNKNOWN_ERROR;
    }
}

/* Helper: Recursively unlinks a tree at path relative to fd. */
static int unlink_recursive_at(int fd, const char* path) {
    struct stat st;
    if (fstatat(fd, path, &st, AT_SYMLINK_NOFOLLOW) < 0)
        return IO_NO_SUCH_FILE_OR_DIRECTORY;
    if (S_ISDIR(st.st_mode)) {
        int dfd = openat(fd, path, O_DIRECTORY | O_RDONLY);
        if (dfd < 0)
            return IO_UNKNOWN_ERROR;
        DIR* d = fdopendir(dfd);
        if (!d) { close(dfd); return IO_UNKNOWN_ERROR; }
        struct dirent* entry;
        int ret = 0;
        while ((entry = readdir(d)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            ret = unlink_recursive_at(dfd, entry->d_name);
            if (ret != 0)
                break;
        }
        closedir(d);
        if (ret == 0)
            ret = (unlinkat(fd, path, AT_REMOVEDIR) == 0) ? 0 : IO_UNKNOWN_ERROR;
        return ret;
    } else {
        return (unlinkat(fd, path, 0) == 0) ? 0 : IO_UNKNOWN_ERROR;
    }
}

/*
 * int32_t io_unlink(const char* path, int32_t flags);
 *
 * Unlinks (deletes) the file or directory at 'path'. If IO_RECURSIVE is set,
 * non-empty directories are removed recursively.
 */
int32_t io_unlink(const char* path, int32_t flags) {
    if (!path)
        return IO_INVALID_ARGUMENTS;
    if (flags & IO_RECURSIVE)
        return unlink_recursive(path);
    else {
        struct stat st;
        if (lstat(path, &st) < 0)
            return IO_NO_SUCH_FILE_OR_DIRECTORY;
        if (S_ISDIR(st.st_mode))
            return (rmdir(path) == 0) ? 0 : IO_UNKNOWN_ERROR;
        else
            return (unlink(path) == 0) ? 0 : IO_UNKNOWN_ERROR;
    }
}

/*
 * int32_t io_unlinkat(int32_t fd, const char* path, int32_t flags);
 *
 * Unlinks (deletes) the file or directory at 'path' relative to file descriptor 'fd'.
 * If IO_RECURSIVE is set, non-empty directories are removed recursively.
 */
int32_t io_unlinkat(int32_t fd, const char* path, int32_t flags) {
    if (!path)
        return IO_INVALID_ARGUMENTS;
    if (flags & IO_RECURSIVE)
        return unlink_recursive_at(fd, path);
    else {
        struct stat st;
        if (fstatat(fd, path, &st, AT_SYMLINK_NOFOLLOW) < 0)
            return IO_NO_SUCH_FILE_OR_DIRECTORY;
        if (S_ISDIR(st.st_mode))
            return (unlinkat(fd, path, AT_REMOVEDIR) == 0) ? 0 : IO_UNKNOWN_ERROR;
        else
            return (unlinkat(fd, path, 0) == 0) ? 0 : IO_UNKNOWN_ERROR;
    }
}

/*
 * int32_t io_mkdtempat(int dirfd, char *template);
 *
 * Creates a temporary directory with a unique name based on 'template'.
 * The template must end with "XXXXXX" and will be replaced with random lowercase letters.
 */
int32_t io_mkdtempat(int dirfd, char *template) {
    size_t len = strlen(template);
    if (len < 6 || strcmp(template + len - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    for (;;) {
        // Replace 'X's with random lowercase letters.
        for (int i = 0; i < 6; i++) {
            template[len - 6 + i] = 'a' + (rand() % 26);
        }
        if (mkdirat(dirfd, template, 0700) == 0) {
            // Successfully created the directory.
            return 0;
        }
        if (errno != EEXIST) {
            // An error other than "directory already exists" occurred.
            return -1;
        }
        // If the directory already exists, try again.
    }
}

/*
 * int32_t io_buildtreeat(int32_t dirfd, const char *path);
 *
 * Creates all directories in path to file.
 *
*/

int32_t io_buildtreeat(int32_t dirfd, const char *path) {
    if (!path)
        return IO_INVALID_ARGUMENTS;

    char buf[PATH_MAX] = {0};
    strncpy(buf, path, PATH_MAX - 1);

    char *dir = buf;
    char *next = strchr(dir, '/');
    while (next) {
        *next = '\0';
        if (mkdirat(dirfd, dir, 0755) == -1 && errno != EEXIST) {
            return IO_UNKNOWN_ERROR;
        }
        *next = '/';
        dir = next + 1;
        next = strchr(dir, '/');
    }
    return 0;
}

/*
 * int32_t io_buildtree(const char* path);
 *
 * Creates all directories in path to file.
 *
*/

int32_t io_buildtree(const char* path) {
    return io_buildtreeat(AT_FDCWD, path);
}
