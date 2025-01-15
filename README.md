# wl_shimeji - Shimeji reimplementation for the Wayland

This is a reimplementation of the [Shimeji](https://kilkakon.com/shimeji/) for Wayland in C.

<p align="center">
  <img src="./docs/screenshot.png" style="width: 100%;">
</p>

Shimejis on the screenshot by [paccha](https://linktr.ee/paccha_) and [Moneka](https://x.com/Monikaphobia)

# Requirements

## Build requirements
- libspng
  - Arch: `pacman -S libspng`
  - Debian: `apt install libspng libspng-dev`
- libwayland-client:
  - Arch: `pacman -S wayland`
  - Debian: `apt install libwayland-client0 libwayland-client-dev`
- wayland-protocols:
  - Arch: `pacman -S wayland-protocols`
  - Debian: `apt install wayland-protocols`
- wlr-protocols:
  - Arch: `pacman -S wlr-protocols`
  - Debian: `apt install wlr-protocols`

## Runtime requirements
Your compositor should *at least* support xdg-shell, wlr-layer-shell protocols, and provide wl_subcompositor interface.

# Building

```sh
git clone --recursive https://github.com/CluelessCatBurger/wl_shimeji.git
cd wl_shimeji
make -j$(nproc)
make install
```

# How to use

## Converting to shimeji-ee format
First, you need to find some Shimeji-ee mascots. They are usually distributed as zip archives. You can find them on the internet.
When you got shimejis of your choice, you need to convert them to the wl_shimeji format. Simplest way to do it is to use `shimejictl import` command.

```sh
shimejictl import /path/to/shimeji-ee.zip
```

It will convert all shimejis from img/ directory of the Shimeji-ee instance to the wl_shimeji format and save them to .local/share/wl_shimeji/shimejis/ directory.

## Summoning mascots
After you converted your mascots, you can summon them to the screen, but first you need to know the name of the mascot you want to summon.

```sh
shimejictl list
```
It will print the list of all available mascots.

Then you can summon mascots by three ways:
1. By name, on random screen:
```sh
shimejictl summon mascot_name
```
2. By name, on specific screen and position:
```sh
shimejictl summon mascot_name --select
```
3. You can summon all known mascots at once (Note: it will only work if overlay is not running):
```sh
shimejictl summon all
```

All summon commands will start the overlay if it's not running.

## Dismissing mascots
You can dismiss mascots by using `shimejictl dismiss` command.

```sh
shimejictl dismiss
```
It will ask you to select the mascot you want to dismiss.
Command also takes three flags:
- `--all` - dismiss all mascots at once (mutually exclusive with `--all-other`)
- `--all-other` - dismiss all mascots except the selected one (mutually exclusive with `--all`)
- `--same` - tells overlay to target mascots of the same type as the selected one

So for example following command will dismiss all mascots:
```sh
shimejictl dismiss --all
```

And this command will dismiss all mascots of the same type as the selected one:
```sh
shimejictl dismiss --all-other --same
```

## Stopping application
You can stop the application by using `shimejictl stop` command.

## Configuration
You can configure some options of overlay by using `shimejictl config` command.
```sh
shimejictl config set "option_name" "value"
```

You also can get option value by using `get` action:
```sh
shimejictl config get "option_name"
```

Or values of all options by using `list` action:
```sh
shimejictl config list
```

Config file is usually located at .config/wl_shimeji/shimeji.conf

# Advanced usage

## Using custom config path
`shimejictl` takes `--config-path` argument to specify custom path to the configuration directory.

## Converting mascots on individual basis
You can convert mascots to the wl_shimeji format on individual basis by using `shimejictl convert` command.

```sh
shimejictl convert .../Shimeji-EE/img/mascot_name
```
Following command will convert mascot with the name `mascot_name` from the Shimeji-ee instance to the wl_shimeji format and save it to .local/share/wl_shimeji/shimejis/ directory.
IMPORTANT: `convert` command doesn't have full instance context, so conf/ subdirectory with actions.xml and behaviors.xml inside the mascot directory *IS REQUIRED*.

## Getting mascot information
Useful for debugging, `shimejictl info` command prints information about the selected mascot.

```sh
shimejictl info
```

## Launching overlay in the foreground
`shimejictl foreground` command launches overlay in the foreground.

```sh
shimejictl foreground
```

## Exporting configurations
To export already converted mascots to a configuration pack, use the export subcommand. This will create a zip archive containing all mascots and their configurations, named export.wlshm in the current directory.

```sh
shimejictl export
```

You can also specify a custom output path by passing it as an argument:

```sh
shimejictl export /path/to/output/file.wlshm
```

You can import this configuration pack later by using `import` subcommand.

## Running overlay manually
You can start overlay manually by running `shimeji-overlayd`. It's not recommended and *useless* in case you don't pass -se flag to it or
if you don't pass unix fd using -cfd flag, unless you implementing another frontend for the overlay. If -se or -cfd not specified
overlay will close immediately after start.

Arguments:
  `-s`  - path to the overlay socket file.
  `-c`  - path to the configuration directory.
  `-cfd`, `--caller-fd` - paired unix socket fd used for communication. If closed and no other clients connected nor no mascots exists, overlay will close.
  `-p`  - path to the plugins directory.
  `-se` - summon everyone. If specified, overlay will summon all known mascots upon start.
  `-v`, `--version`  - version of the overlay.
  `--no-tablets` - disable wp_tablet_v2 wayland protocol support. (broken on kwin right now anyway)
  `--no-viewporter` - disable wp_viewporter wayland protocol support. wl_shimeji will use wl_surface.set_buffer_scale() instead.
  `--no-fractional-scale` - disable wp_fractional_scale wayland protocol support. Will use wl_output's scale info instead
  `--no-cursor-shape` - disable wp_cursor_shape wayland protocol support. Will disable cursor shapes for different actions.
  `--no-plugins` - disable plugins

# Features

1. Less CPU
    - Because of C and Wayland, I was able to make this program as lightweight as possible.
    - When compared to the original Shimeji, my implementation for same configuration uses in ~20 times less CPU.
    (Tested on AMD Ryzen 9 5900HX, 7 mascots. Original Shimeji used ~19% of one CPU core. My implementation used ~0.9% of one CPU core)
    (Do not trust these numbers, they can be different for your configuration)
2. Less RAM
    - Because of prototypes-based method, mascots use as less memory as possible.
    (Tested on AMD Ryzen 9 5900HX, 7 mascots. Original Shimeji ~880MiB. My implementation used ~56MiB)
    (Do not trust these numbers, they can be different for your configuration)
3. Fast VM-based condition evaluation:
    - I implemented simple virtual machine and bytecode compiler for javascript-like conditions used in the original Shimeji.
    - It's not as fast as native code, but it's faster than the original Shimeji.

## Configuration

On first start, the program will create a configuration file in the config directory.
Currently config file allows to:
- Enable/Disable mascot breeding
- Enable/Disable mascot dragging
- Enable/Disable IE interactions (doesn't make sense without plugins)
- Enable/Disable IE throwing (doesn't make sense without plugins)
- Enable/Disable cursor position tracking (doesn't make sense without plugins)
- Set max number of mascots
- Set IE Throw Policy (doesn't make sense without plugins)

All parameters except max number and IE Throw Policy is booleans.
IE Throw Policy and max number is integers.

I will not explain possible values for IE Throw Policy for now.

## Plugins

Plugins is a way to provide non-portable ways to interact with the environment.
It can be used for:
- Getting cursor position
- Informations about active window
- Moving windows

Currently I have 1 experimental plugin that implements all of the above for kwin_wayland compositor, I don't recommend to use it though.
If you want to risk, you can compile it with following command:

  ```sh
    gcc -I./build src/plugins/kwinsupport.c -shared -fPIC -lpluginsupport -lsystemd -o kwinsupport.so
  ```

Compiled plugins should be placed in the plugins/ directory (relative to the configuration directory).

## TODO LIST:

- [ ] Write documentation for plugins API
- [ ] Add more configuration options

## Notes:

- About environment interaction: It will be available only through the plugins. Wayland protocol doesn't allow to interact with windows of other applications,
  and will NEVER allow it. It's a security feature. If you think that it makes Wayland bad, ask yourself a question: Why application even should have access to
  the windows of other applications? Try to find at least 5 apps that need this feature and then come back to me.
- About plugins: It will be .so files that will be loaded at runtime. It can implement any functionality to lookup positions of active window, pointer, etc.
  There is no way to make it portable. For example, you theoretically can use kdotool to get and move windows, but it's will work only under kwin.
- X11 support? No, X11 doesn't provide key features that I need for this project. You can try to implement it. For it to work, you need to implement each and every function from
  src/environment.h
- ~~Windows~~/Macos/FreeBSD/InsertYourOS support? If you will able somehow run Wayland on it or reimplement funcs defined in src/environment.h, then yes. I don't have any plans to do it.
- If you want to help me with this project, beware src/mascot_config_parser.c. I warned you. (No, really, even i don't understand what's going on there.)
- Windows support? After searching for a while, I found that there is a similar mechanism to wl_subcompositor in Windows called "DirectComposition".
  I will not implement it by myself, but if you want to try, you can do it.

# Blabbering section:

## What is Shimeji?

Shimeji is a program that creates a little character that wanders around your desktop. It can be customized with different skins and can be set to interact with the desktop environment.

## Why did I start this project?

The original Shimeji is written in Java and very unoptimized. It uses a lot of CPU and memory.
For example, from cold start, in some configurations, setup from 7 mascots can eat up 880MiB of RAM and ~19% of one cpu core (AMD Ryzen 9 5900HX)

I think this is unacceptable for such a simple program.
Moreover, original Shimeji is starting lagging when too many mascots on the screen, and because each mascot is a SEPARATE WINDOW, it slow down entire
compositor, with probaility of crashing it.

Even if we close eyes on the graphics, the original Shimeji uses javascript for the conditions, so it's makes everything even slower.

## How my reimplementation is better?

When I started this project, i started with the idea of making it as fast and as lightweight as possible.
So i started to analyze the original Shimeji and found things that can be definitely improved.
For example, when original Shimeji creates a new mascot, it creates instance of the `Mascot` class. What's wrong with it?
Actually nothing, but if we check how instances of `Mascot` are created, we can see that each instance is holds it's own copy of configuration and all sprites.
This is a big problem, as configuration and sprites are not very small, so it's a waste of memory.

What we can do then to improve it? Answer is simple: Prototypes! Instead of copying configuration and sprites for each mascot, we can create a prototype of the mascot
and then reference it in the actual mascot, maintaining the same functionality, but with less memory usage.

So my entire reimplementation is based on this idea. I'm using prototypes for mascots, and only the necessary data is copied for each mascot.

But what about language and graphics? Why I choose C and Wayland from all the possible options?

### Wayland

This is quite interesting story. When I started this project, i was very interested in the Wayland protocol and how it works.
But this is not enough to choose it as a base for the project, so why Wayland? The answer is subcompositors.
In Wayland, everything that shown on the screen is a surface with it's role. For example, the window is a surface with a role of `xdg_toplevel`.
But what if we want to make complex window with multiple layers? Usually, we need to rerender everything in region that need to be updated.
It's can be difficult, it is developer's responsibility to handle it, and you should be smarter than me to do it.
But in Wayland, we have a concept of subcompositors. Subcompositor is a method to make composite window from multiple surfaces, and it's compositor's
responsibility to handle how window should be rendered. We will return to this later.

Then, if we return to prototypes, we can also find that wayland from box allows us create buffer only from shared memory, but at the same time, we
can reuse same buffer for multiple surfaces. This is exactly what we need for prototypes! We create our buffer only once, and then we reuse for each mascot when we need to.

Then we can start to implement our shimeji project, and...

Oh. oh no. We forgot about nature of Wayland.
Wayland is protocol with security in mind. It's not like X11, where you can do everything you want with the windows.
So what can we do? Or rather, what can’t we do? We can't position our mascots freely on the screen as we don't have access to the window positions (at all).

So, then what we actually can do?
If we open wayland.app, we can see set of protocols that exists in the Wayland. Not each protocol is implemented in each compositor,
but lets see what it can offer us.

So, wlroots created wlr-layer-shell protocol. This protocol allows us to create overlays on the screen. If we can create fullscreen overlay with no input and fully transparent,
we can guarantee that pixel on the overlay is the same as pixel on the screen, so now we can set position of our mascots freely.
But i don't want to render anything, what then I can do?

Now we can return to the subcompositors. We can use subcompositor functionality to avoid manual rendering of mascots. Instead we create subsurface for each mascot
and then attach it to our overlay. Now we can set position of our mascots freely and we don't need to render anything manually, because we use precreated buffers for each possible sprite.

### C

Now, when we decided to use Wayland, we need to choose language.
Oh, no language currently (Feb 2024) supports Wayland on same level as C (We need low level access to the Wayland protocol). Guess, I had no choice.
Anyway, it will economize memory and CPU usage, so it's not that bad.
That pretty much sums up why I choose C.

### Conditions and expressions in configuration files

Original Shimeji includes a feature called "Using JavaScript in places where you should not use JavaScript". I not expert in Shimeji-ee, but i think that
one of the reasons why 7 mascots can eat up 880MiB of RAM. I will not test it, but i decided that i will not include javascript in my project. Even if it means
that i will be required to implement compiler of javascript and my own virtual machine for it.
So now we have our own virtual machine instead of javascript. It was great experience.
