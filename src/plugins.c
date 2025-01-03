#include "plugins.h"
#include "environment.h"

#include <dlfcn.h>
#include <signal.h>
#include <setjmp.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>

// Signal handler for SIGSEGV to ensure that plugin execution does not crash the main program

static jmp_buf last_okay_state;

static void signal_handler(int sig)
{
    UNUSED(sig);

    void *backtrace_buf[50] = {0};
    int backtrace_size = backtrace(backtrace_buf, 50);

    TRACE("Segfault occuried in plugin!");
    TRACE("Backtrace:");
    fputs(CYAN, stderr);
    backtrace_symbols_fd(backtrace_buf, backtrace_size, STDERR_FILENO);
    TRACE("End of stack trace");

    longjmp(last_okay_state, 0);
}

struct plugin* plugin_open(const char* filename)
{
    if (!filename) {
        return NULL;
    }

    void* handle = dlopen(filename, RTLD_NOW);

    if (!handle) {
        WARN("Failed to open plugin %s: %s", filename, dlerror());
        return NULL;
    }

    struct plugin* plugin_info = dlsym(handle, "plugin_info");
    if (!plugin_info) return NULL;

    struct plugin* plugin = NULL;
    plugin = calloc(1, sizeof(struct plugin));
    if (!plugin) goto fail_plugin_open;

    *plugin = *plugin_info;

    plugin->handle = handle;

    DEBUG("Opened plugin %s", filename);

    return plugin;

fail_plugin_open:
    dlclose(handle);
    free(plugin);
    return NULL;
}

void plugin_close(struct plugin* plugin)
{
    if (!plugin) {
        return;
    }

    dlclose(plugin->handle);
    free(plugin);

    DEBUG("Plugin closed");
}

enum plugin_initialization_result plugin_init(struct plugin* plugin, uint32_t allowed_capabilities, const char** error_message)
{
    if (!plugin) return PLUGIN_INIT_NULLPTR;
    if (!plugin->init) {
        *error_message = "Plugin does not have an init function";
        return PLUGIN_INIT_BAD_DESCRIPTION;
    }
    if (!plugin->deinit) {
        *error_message = "Plugin does not have a deinit function";
        return PLUGIN_INIT_BAD_DESCRIPTION;
    }
    if (!plugin->execute) {
        *error_message = "Plugin does not have a execute function";
        return PLUGIN_INIT_BAD_DESCRIPTION;
    }
    if (plugin->provides & PLUGIN_PROVIDES_IE_MOVE) {
        if (!(plugin->provides & PLUGIN_PROVIDES_IE_POSITION)) {
            *error_message = "Plugin provides IE_MOVE but does not provide IE_POSITION";
            return PLUGIN_INIT_BAD_DESCRIPTION;
        }
        if (!plugin->execute_ie_move) {
            *error_message = "Plugin provides IE_MOVE but does not have a execute_ie_move function";
            return PLUGIN_INIT_BAD_DESCRIPTION;
        }
    }
    if (plugin->provides & PLUGIN_PROVIDES_IE_POSITION) {
        if (!plugin->execute_ie_attach_mascot || !plugin->execute_ie_detach_mascot) {
            *error_message = "Plugin provides IE_POSITION but does not have execute_ie_attach_mascot or execute_ie_detach_mascot functions";
            return PLUGIN_INIT_BAD_DESCRIPTION;
        }
    }
    if (!plugin->initialize_ie) {
        *error_message = "Plugin does not have a initialize_ie function";
        return PLUGIN_INIT_BAD_DESCRIPTION;
    }
    if (!plugin->deinitialize_ie) {
        *error_message = "Plugin does not have a deinitialize_ie function";
        return PLUGIN_INIT_BAD_DESCRIPTION;
    }

    enum plugin_initialization_result result;
    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->init(allowed_capabilities, error_message);
    }
    else {
        result = PLUGIN_INIT_SEGFAULT;
        *error_message = "Plugin caused a segmentation fault during initialization";
    }
    sigaction(SIGSEGV, &old_action, NULL);

    DEBUG("Initialized plugin %s, caps %d %d (allowed, effective)", plugin->name, allowed_capabilities, plugin->effective_caps);

    return result;
}

void plugin_deinit(struct plugin* plugin)
{
    if (!plugin) {
        return;
    }

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        plugin->deinit();
    }
    sigaction(SIGSEGV, &old_action, NULL);

    DEBUG("Deinitialized plugin %s", plugin->name);
}

enum plugin_execution_result plugin_execute(struct plugin* plugin, struct ie_object* ie, int32_t* pointer_x, int32_t* pointer_y, uint32_t tick)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute(ie, pointer_x, pointer_y, tick);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_ie_move(struct plugin* plugin, struct ie_object* ie, int32_t x, int32_t y)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_ie_move(ie, x, y);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_ie_attach_mascot(struct plugin* plugin, struct ie_object* ie, struct mascot* mascot)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_ie_attach_mascot(ie, mascot);
        if (result == PLUGIN_EXEC_OK) {
            mascot->associated_ie = ie;
        }
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_ie_detach_mascot(struct plugin* plugin, struct ie_object* ie, struct mascot* mascot)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_ie_detach_mascot(ie, mascot);
        if (result == PLUGIN_EXEC_OK) {
            mascot->associated_ie = NULL;
        }
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);

    if (result != PLUGIN_EXEC_OK) DEBUG("Detached for mascot %u failed: %d", mascot->id, result);

    return result;
}

enum plugin_execution_result plugin_change_capabilities(struct plugin* plugin, uint32_t capabilities)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    plugin->effective_caps = capabilities & plugin->provides;

    return PLUGIN_EXEC_OK;
}

enum plugin_execution_result plugin_get_ie_for_environment(struct plugin* plugin, environment_t* env, struct ie_object** ie)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct ie_object* result_ie = calloc(1, sizeof(struct ie_object));
    if (!result_ie) {
        return PLUGIN_EXEC_UNKNOWN_ERROR;
    }

    result_ie->parent_plugin = plugin;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);

    const char* name;
    const char* make;
    const char* model;
    const char* desc;
    uint32_t id;

    environment_get_output_id_info(env, &name, &make, &model, &desc, &id);

    if (setjmp(last_okay_state) == 0) {
        result = plugin->initialize_ie(result_ie, name, id);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
        free(result_ie);
        result_ie = NULL;
    }

    sigaction(SIGSEGV, &old_action, NULL);

    result_ie->environment = env;
    *ie = result_ie;

    return result;
}

enum plugin_execution_result plugin_execute_throw_ie(struct plugin* plugin, struct ie_object* ie, float x_velocity, float y_velocity, float gravity, uint32_t start_tick)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!plugin->execute_throw_ie) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_throw_ie(ie, x_velocity, y_velocity, gravity, start_tick);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_stop_ie(struct plugin* plugin, struct ie_object* ie)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!plugin->execute_stop_ie) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_stop_ie(ie);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_deactivate_ie(struct plugin* plugin, struct ie_object* ie)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;
    if (!ie) return PLUGIN_EXEC_NULLPTR;
    if (!plugin->execute_deactivate_ie) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_deactivate_ie(ie);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_execute_restore_ies(struct plugin* plugin)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;
    if (!plugin->execute_restore_ies) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->execute_restore_ies();
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}

enum plugin_execution_result plugin_free_ie(struct plugin* plugin, struct ie_object* ie)
{
    if (!plugin) return PLUGIN_EXEC_NULLPTR;
    if (!ie) return PLUGIN_EXEC_NULLPTR;

    enum plugin_execution_result result = PLUGIN_EXEC_UNKNOWN_ERROR;

    struct sigaction old_action;
    sigaction(SIGSEGV, &(struct sigaction) { .sa_handler = signal_handler }, &old_action);
    if (setjmp(last_okay_state) == 0) {
        result = plugin->deinitialize_ie(ie);
        free(ie);
    }
    else {
        result = PLUGIN_EXEC_SEGFAULT;
    }
    sigaction(SIGSEGV, &old_action, NULL);
    return result;
}
