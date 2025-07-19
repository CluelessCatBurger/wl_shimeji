#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>

#ifndef MASTER_HEADER
#define MASTER_HEADER

#define WL_SHIMEJI_VERSION "0.0.2"
#define WL_SHIMEJI_MASCOT_MIN_VER "0.0.1"
#define WL_SHIMEJI_MASCOT_CUR_VER "0.0.1"

#define WL_SHIMEJI_PROTOCOL_VERSION "0.0.1"
#define WL_SHIMEJI_PROTOCOL_MIN_VER "0.0.1"

#define WL_SHIMEJI_PLUGIN_TARGET_VERSION "0.0.2"

#if defined(GNU_EXTS) && defined(__has_attribute)
#define GNU_ATTRIBUTE(attr) __has_attribute(attr)
#else
#define GNU_ATTRIBUTE(attr) 0
#endif

#if GNU_ATTRIBUTE((format))
#define PRINT_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define PRINT_FORMAT
#endif

#define UNUSED(x) (void)(x)

__attribute__((noreturn)) PRINT_FORMAT void __error(const char*, int, const char*, ...);
PRINT_FORMAT void __warn(const char*, int, const char*, ...);
PRINT_FORMAT void __info(const char*, int, const char*, ...);
PRINT_FORMAT void __debug(const char*, int, const char*, ...);
PRINT_FORMAT void __clog(const char*, const char*, const char*, int, const char*, ...);


// Colors
#define RED "\033[33m"
#define GREEN "\033[92m"
#define YELLOW "\033[93m"
#define BLUE "\033[94m"
#define MAGENTA "\033[95m"
#define CYAN "\033[96m"
#define GRAY "\033[97m"
#define RESET "\033[0m"

#define ERROR(x, ...) __error(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define INFO(x, ...) __info(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define WARN(x, ...) __warn(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define DEBUG(x, ...) __debug(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define LOG(logtype, color, x, ...) __clog(color, logtype, __FILE__, __LINE__, x, ##__VA_ARGS__)
#define TRACE(x, ...) __clog(CYAN, "TRACE", __FILE__, __LINE__, x, ##__VA_ARGS__)

#define LOGLEVEL_WARN  2
#define LOGLEVEL_INFO  3
#define LOGLEVEL_DEBUG 5

int32_t LOGLEVEL(int32_t loglevel);

// Convert string version to uint64_t
// Version format: "major.minor.patch"
// When converts to u64, it has following structure:
// topmost bit: sign bit, marks invalid conversion
// 21 bits: major version
// 21 bits: minor version
// 21 bits: patch version
static inline int64_t version_to_i64(const char* str)
{
    if (!str) return 0; // Assume 0.0.0
    int64_t result = 0;
    char* endptr = NULL;
    int64_t major = strtoul(str, &endptr, 10);
    if (endptr == str) return -1; // Conversion failed
    if (*endptr != '.') return -1; // Invalid format
    result |= (major & 0x1FFFFF) << 42;
    str = endptr + 1;
    int64_t minor = strtoul(str, &endptr, 10);
    if (endptr == str) return -1; // Conversion failed
    if (*endptr != '.') return -1; // Invalid format
    result |= (minor & 0x1FFFFF) << 21;
    str = endptr + 1;
    int64_t patch = strtoul(str, &endptr, 10);
    if (endptr == str) return -1; // Conversion failed
    result |= patch & 0x1FFFFF;
    return result;
}

#endif
