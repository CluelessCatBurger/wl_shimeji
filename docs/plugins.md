# Plugins

Currently some features like interaction with windows is only available via plugins.
By default wl_shimeji will try to load .so files located in the `/usr/lib/wl_shimeji/` directory. Search path can be overridden by adding `plugins_location` key with absolute path to the plugins directory to the config file.

## How to write plugins

To write plugins, you just need to create a shared library that exports few symbols.
There are currently 3 required symbols and 2 optional ones:
- `wl_shimeji_plugin_init`: Initializes the plugin. (Required)
- `wl_shimeji_plugin_deinit`: Deinitializes the plugin. (Required)
- `wl_shimeji_plugin_tick`: Ticks the plugin. (Required)
- `wl_shimeji_plugin_move`: Moves currently active window.
- `wl_shimeji_plugin_restore`: Restores positions of all windows that are currently out of screen.

Shared library should be linked against `libwayland-shimeji-plugins.so`.

### Initialization of the plugin

Plugin must expose `wl_shimeji_plugin_init` symbol that will be called when plugin is loaded. Plugin is free to do any initialization required in order to work properly there.
Somewhere in the init function plugin must call plugin_init (declaration can be found in the src/plugins.h of the wl_shimeji) if it decides that plugin is initialized successfully. It is error to return 0 (aka success) from this function if plugin_init was never called or failed.

During initialization wl_shimeji will give plugin a function pointers to some callbacks that can be used to supply information about environment from plugin.

Currently exists only three callbacks:
- `set_cursor_pos_func` - Sets public cursor position that is available for mascots.
- `set_active_ie_func` - Sets information about currently active window on the entire workspace (so only one active window per all screens). `is_active` should be set to false if there are no active, non-fullscreen, not minimized windows. It's generally recommended to skip all surfaces that cannot be defined as windows: overlays, popups, etc. Plugin should supply global position of the window and its size.
- `window_moved_hint_func` - Sends hint to wl_shimeji that window are somehow moved. It is used to cancel mascot actions that allows windows to be moved, so there will not be fights between user and mascot(Btw mascot will always win).

### Deinitialization
Plugin must expose `wl_shimeji_plugin_deinit` symbol that will be called when plugin is unloaded. Plugin is free to do any cleanup required in order to work properly there.
plugin_deinit should be called here if possible.

### Tick

Plugin must expose `wl_shimeji_plugin_tick` symbol that will be called every tick. Plugin is free to do anything it wants here, so it can be used to update internal state of plugin, check for new events, etc.
Quota is 40000 usec.

### Moving and restoring

Plugin **may** expose `wl_shimeji_plugin_move` and `wl_shimeji_plugin_restore` functions to allow mascots to move windows. If move is exposed, it is recommended to expose restore as well, unless compositor allows user to restore window positions  by some simple way.

### Segmentation faults or/and deadlocks

wl_shimeji will try it best to avoid termination if plugin is crashed, but it is not guaranteed.
On segfault wl_shimeji will try to call plugin's deinit and then free all that it can free. If plugin takes too much time in tick, move, deinit or restore, wl_shimeji will treat it as a crash.

### Examples

You can find example plugin implementation in src/plugins/kwinsupport, it's buggy, but it's works i guess
