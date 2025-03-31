/*
    plugin.c - wl_shimeji's plugin support

    Copyright (C) 2025  CluelessCatBurger <github.com/CluelessCatBurger>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include "plugins.h"
#include "environment.h"

#include <dlfcn.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>

// Signal handler for SIGSEGV to ensure that plugin execution does not crash the main program

// static jmp_buf last_okay_state;

// static void signal_handler(int sig)
// {
//     UNUSED(sig);

//     void *backtrace_buf[50] = {0};
//     int backtrace_size = backtrace(backtrace_buf, 50);

//     TRACE("Segfault occuried in plugin!");
//     TRACE("Backtrace:");
//     fputs(CYAN, stderr);
//     backtrace_symbols_fd(backtrace_buf, backtrace_size, STDERR_FILENO);
//     TRACE("End of stack trace");

//     longjmp(last_okay_state, 0);
// }

struct plugin {
    uint32_t refcounter;
};
struct plugin_window {
    uint32_t refcounter;
};
struct plugin_output {
    uint32_t refcounter;
};

plugin_t* plugin_load(int32_t at_fd, const char* path, enum plugin_exec_error *error)
{
    UNUSED(at_fd);
    UNUSED(path);
    *error = PLUGIN_EXEC_ABORT;
    return NULL;
}

plugin_t* plugin_ref(plugin_t* plugin)
{
    if (!plugin)
        return NULL;

    plugin->refcounter++;
    return plugin;
}

void plugin_unref(plugin_t* plugin)
{
    if (!plugin)
        return;

    plugin->refcounter--;
    if (!plugin->refcounter)
        free(plugin);
}

void plugin_unload(plugin_t* plugin)
{
    UNUSED(plugin);
    return;
}

enum plugin_exec_error* plugin_set_caps(plugin_t* plugin, int32_t caps)
{
    UNUSED(plugin);
    UNUSED(caps);
    return NULL;
}

enum plugin_exec_error* plugin_tick(plugin_t* plugin)
{
    UNUSED(plugin);
    return NULL;
}

plugin_output_t* plugins_request_output(struct bounding_box* geometry)
{
    UNUSED(geometry);
    return NULL;
}

plugin_output_t* plugins_output_ref(plugin_output_t* output)
{
    if (!output)
        return NULL;

    output->refcounter++;
    return output;
}

void plugins_output_unref(plugin_output_t* output)
{
    if (!output)
        return;

    output->refcounter--;
    if (!output->refcounter)
        free(output);
}

void plugin_output_set_listener(plugin_output_t* output, struct plugin_output_event* listener, void* userdata)
{
    UNUSED(output);
    UNUSED(listener);
    UNUSED(userdata);
    return;
}

void plugin_output_commit(plugin_output_t* output)
{
    UNUSED(output);
    return;
}

void plugin_output_destroy(plugin_output_t* output)
{
    UNUSED(output);
    return;
}

plugin_window_t* plugin_window_ref(plugin_window_t* window)
{
    if (!window)
        return NULL;

    window->refcounter++;
    return window;
}

void plugin_window_unref(plugin_window_t* window)
{
    if (!window)
        return;

    window->refcounter--;
    if (!window->refcounter)
        free(window);
}

void plugin_window_set_listener(plugin_window_t* window, struct plugin_window_event* listener, void* userdata)
{
    UNUSED(window);
    UNUSED(listener);
    UNUSED(userdata);
    return;
}

bool plugin_window_grab_control(plugin_window_t* window)
{
    UNUSED(window);
    return false;
}

bool plugin_window_move(plugin_window_t* window, int32_t x, int32_t y)
{
    UNUSED(window);
    UNUSED(x);
    UNUSED(y);
    return false;
}

bool plugin_window_drop_control(plugin_window_t* window)
{
    UNUSED(window);
    return false;
}

bool plugin_window_restore_position(plugin_window_t* window)
{
    UNUSED(window);
    return false;
}

struct bounding_box* plugin_window_geometry(plugin_window_t* window)
{
    UNUSED(window);
    return NULL;
}
