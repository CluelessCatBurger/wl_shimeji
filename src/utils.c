/*
utils.с - Util functions
Copyright (C) 2021  LekKit <github.com/LekKit>
                    0xCatPKG <0xCatPKG@rvvm.dev>
                    0xCatPKG <github.com/0xCatPKG>
                    CluelessCatBurger <github.com/CluelessCatBurger>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "master_header.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#define EVAL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define EVAL_MIN(a, b) ((a) < (b) ? (a) : (b))


static int32_t loglevel =
#ifdef VERBOSE
LOGLEVEL_DEBUG;
#else
LOGLEVEL_INFO;
#endif

int32_t LOGLEVEL(int32_t loglevel_) {
    int32_t curlevel = loglevel;
    if (loglevel >= 0) {
        loglevel = loglevel_;
    }
    return curlevel;
}

static size_t rvvm_strlcpy(char* dst, const char* src, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    if (size) dst[i] = 0;
    return i;
}

// Write current timestamp into buffer
size_t write_time(char* prefix) {
    time_t t = time(NULL);
    struct tm now;
    struct tm tm = *localtime_r(&t, &now);
    return snprintf(prefix, 31, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(t % 1000));
}

void log_print(const char* prefix, const char* fmt, va_list args)
{
    char buffer[4096] = {0};
    size_t pos = rvvm_strlcpy(buffer, prefix, sizeof(buffer));
    size_t vsp_size = sizeof(buffer) - EVAL_MIN(pos + 6, sizeof(buffer));
    if (vsp_size > 1) {
        int tmp = vsnprintf(buffer + pos, vsp_size, fmt, args);
        if (tmp > 0) pos += EVAL_MIN(vsp_size - 1, (size_t)tmp);
    }
    rvvm_strlcpy(buffer + pos, "\033[0m\n", sizeof(buffer) - pos);
    fputs(buffer, stderr);
}

PRINT_FORMAT void __clog(const char* color, const char* log_type, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char prefix[128] = {0};
    char timebuf[32] = {0};
    write_time(timebuf);
    snprintf(prefix, 127, "%s[%s][%s][%s:%d]: ", color, timebuf, log_type, file, line);
    log_print(prefix, fmt, args);
    va_end(args);
}

PRINT_FORMAT void __error(const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char prefix[128] = {0};
    char timebuf[32] = {0};
    write_time(timebuf);
    snprintf(prefix, 127, "\033[33m[%s][ERROR][%s:%d]: ", timebuf, file, line);
    log_print(prefix, fmt, args);
    va_end(args);
    abort();
}

PRINT_FORMAT void __warn(const char* file, int line, const char* fmt, ...)
{
    if (loglevel < LOGLEVEL_WARN) return;
    va_list args;
    va_start(args, fmt);
    char prefix[128] = {0};
    char timebuf[32] = {0};
    write_time(timebuf);
    snprintf(prefix, 127, "\033[93m[%s][WARN][%s:%d]: ", timebuf, file, line);
    log_print(prefix, fmt, args);
    va_end(args);
}

PRINT_FORMAT void __info(const char* file, int line, const char* fmt, ...)
{
    if (loglevel < LOGLEVEL_INFO) return;
    va_list args;
    va_start(args, fmt);
    char prefix[128] = {0};
    char timebuf[32] = {0};
    write_time(timebuf);
    snprintf(prefix, 127, "\033[94m[%s][INFO][%s:%d]: ", timebuf, file, line);
    log_print(prefix, fmt, args);
    va_end(args);
}

PRINT_FORMAT void __debug(const char* file, int line, const char* fmt, ...)
{
#ifdef VERBOSE
    if (loglevel < LOGLEVEL_DEBUG) return;
    va_list args;
    va_start(args, fmt);
    char prefix[128] = {0};
    char timebuf[32] = {0};
    write_time(timebuf);
    snprintf(prefix, 127, "\033[92m[%s][DEBUG][%s:%d]: ", timebuf, file, line);
    log_print(prefix, fmt, args);
    va_end(args);
#else
    UNUSED(file);
    UNUSED(line);
    UNUSED(fmt);
#endif
}
