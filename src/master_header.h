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

#define WL_SHIMEJI_VERSION "0.0.0"

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

PRINT_FORMAT void __error(const char*, int, const char*, ...);
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
#define RESET "\033[0m"

#define ERROR(x, ...) __error(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define INFO(x, ...) __info(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define WARN(x, ...) __warn(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define DEBUG(x, ...) __debug(__FILE__, __LINE__, x, ##__VA_ARGS__)
#define LOG(logtype, color, x, ...) __clog(color, logtype, __FILE__, __LINE__, x, ##__VA_ARGS__)
#define TRACE(x, ...) __clog(CYAN, "TRACE", __FILE__, __LINE__, x, ##__VA_ARGS__)

#endif
