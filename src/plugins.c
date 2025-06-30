#include "plugins.h"
#include <stdbool.h>
#include <unistd.h>

struct plugin {
    void* dl_handle;

    // Callbacks
    init_func init;
    deinit_func deinit;
    tick_func tick;

    char* name;
    char* version;
    char* description;
    char* author;

    bool initialized;
};

int plugin_deinit(plugin_t* self);

#ifdef BUILD_PLUGIN_SUPPORT

int plugin_init(plugin_t* self, const char* name, const char* version, const char* author, const char* description, int64_t target_version)
{
    if (target_version != version_to_i64(WL_SHIMEJI_PLUGIN_TARGET_VERSION)) {
        WARN("[Plugins] Skipping plugin \"%s\": plugin not build for actual version of wl_shimeji", name);
        return -1;
    }

    self->name = strdup(name);
    self->version = strdup(version);
    self->description = strdup(description);
    self->author = strdup(author);

    self->initialized = true;

    INFO("[Plugins] \"%s\" by %s (%s) of version %s successfully initialized", self->name, self->author, self->description, self->version);

    return 0;
}

#else

#include <signal.h>
#include <setjmp.h>
#include <utarray.h>
#include <dlfcn.h>
#include "io.h"

UT_array *plugins;
UT_icd plugin_ptr_icd = {sizeof(plugin_t*), NULL, NULL, NULL};

static jmp_buf checkpoint;
__sighandler_t old_segv;

static void sighandler(int sig) {
    if (sig == SIGSEGV) {
        longjmp(checkpoint, 1);
    } else if (sig == SIGALRM) {
        longjmp(checkpoint, 2);
    }
}

static void setup_handlers(bool setup) {
    if (setup) {
        old_segv = signal(SIGSEGV, sighandler);
        signal(SIGALRM, sighandler);
        alarm(1);
    } else {
        signal(SIGSEGV, old_segv);
        signal(SIGALRM, SIG_IGN);
        alarm(0);
    }
}

static plugin_t* plugin_load(const char* path, set_cursor_pos_func posfn, set_active_ie_func iefn) {
    plugin_t* plugin = calloc(1, sizeof(plugin_t));
    if (!plugin) ERROR("OOM");

    void* dlhandle = dlopen(path, RTLD_NOW);
    if (!dlhandle) {
        WARN("Failed to load plugin at %s: %s", path, dlerror());
        return NULL;
    }

    plugin->dl_handle = dlhandle;
    plugin->init = dlsym(dlhandle, "wl_shimeji_plugin_init");
    plugin->deinit = dlsym(dlhandle, "wl_shimeji_plugin_deinit");
    plugin->tick = dlsym(dlhandle, "wl_shimeji_plugin_tick");

    if (!plugin->init) {
        WARN("[Plugins] Failed to load plugin at %s: Unable to locate init function", path);
        goto fail;
    }

    if (!plugin->deinit) {
        WARN("[Plugins] Failed to load plugin at %s: Unable to locate deinit function", path);
        goto fail;
    }

    if (!plugin->tick) {
        WARN("[Plugins] Failed to load plugin at %s: Unable to locate tick function", path);
        goto fail;
    }

    setup_handlers(true);
    int v = setjmp(checkpoint);
    if (!v) {
        if (plugin->init(plugin, posfn, iefn) != 0) {
            WARN("[Plugins] Failed to initialize plugin at %s", path);
            setup_handlers(false);
            goto fail;
        }
        setup_handlers(false);
    } else {
        if (v == 1) {
            WARN("[Plugins] Failed to initialize plugin at %s: Segmentation fault", path);
            setup_handlers(false);
            goto fail;
        } else if (v == 2) {
            WARN("[Plugins] Failed to initialize plugin at %s: Timed out", path);
            setup_handlers(false);
            goto fail;
        }
    }


    if (!plugin->initialized) {
        WARN("[Plugins] Plugin at %s caused illegal behavior: plugin->initialized is false, but we got 0 from plugin->init", path);
        goto fail;
    }

    return plugin;

    fail:
        dlclose(dlhandle);
        free(plugin);
        return NULL;
}

static void plugin_unload(plugin_t* plugin)
{
    if (!plugin) return;
    if (plugin->initialized) {
        plugin_deinit(plugin);
    }

    dlclose(plugin->dl_handle);
    free(plugin);
}

int plugins_init(const char* plugins_search_path, set_cursor_pos_func cursor_cb, set_active_ie_func active_ie_cb)
{
    char ** candidates = NULL;
    int32_t num_candidates = 0;

    if (io_find(plugins_search_path, "*.so", 0, &candidates, &num_candidates)) {
        WARN("[Plugins] Failed to search for plugins, check if fs permissions are correct");
        return -1;
    }

    utarray_new(plugins, &plugin_ptr_icd);

    for (int i = 0; i < num_candidates; i++) {
        char path[PATH_MAX] = {0};
        snprintf(path, PATH_MAX-1, "%s/%s", plugins_search_path, candidates[i]);
        plugin_t* plugin = plugin_load(path, cursor_cb, active_ie_cb);
        if (!plugin) continue;
        utarray_push_back(plugins, &plugin);
    }

    free(candidates);
    return utarray_len(plugins);
}

int plugins_tick()
{
    plugin_t** p;

    if (!plugins) return 0;

    for(p=(plugin_t**)utarray_front(plugins);
        p!=NULL;
        p=(plugin_t**)utarray_next(plugins, p))
    {
        setup_handlers(true);
        int v = setjmp(checkpoint);
        if (!v) (*p)->tick(*p);
        setup_handlers(false);
        if (v) {
            if (v == 1) {
                WARN("[Plugins] Plugin %s tick failed: Segmentation fault", (*p)->name);
            }
            else if (v == 2) {
                WARN("[Plugins] Plugin %s tick failed: Timed out", (*p)->name);
            }
            setup_handlers(true);
            v = setjmp(checkpoint);
            if (!v) (*p)->deinit(*p);
            setup_handlers(false);
            if (v == 1) {
                WARN("[Plugins] Plugin %s deinit failed: Segmentation fault", (*p)->name);
            }
            else if (v == 2) {
                WARN("[Plugins] Plugin %s deinit failed: Timed out", (*p)->name);
            }
            plugin_unload(*p);
            *p = NULL;
        }
    }

    return 0;
}

int plugins_deinit()
{
    plugin_t** p;

    if (!plugins) return 0;

    for(p=(plugin_t**)utarray_front(plugins);
        p!=NULL;
        p=(plugin_t**)utarray_next(plugins, p))
    {
        setup_handlers(true);
        int v = setjmp(checkpoint);
        if (!v) (*p)->deinit(*p);
        setup_handlers(false);

        if (v == 1) {
            WARN("[Plugins] Plugin %s deinit failed: Segmentation fault", (*p)->name);
        }
        else if (v == 2) {
            WARN("[Plugins] Plugin %s deinit failed: Timed out", (*p)->name);
        }

        plugin_unload(*p);
        *p = NULL;
    }

    utarray_free(plugins);
    return 0;
}

#endif

int plugin_deinit(plugin_t* self)
{
    if (!self->initialized) return 0;

    INFO("[Plugins] \"%s\" deinitialized", self->name);

    free(self->name);
    free(self->version);
    free(self->description);
    free(self->author);

    self->initialized = false;


    return 0;
}
