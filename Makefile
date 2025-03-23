
override SRCDIR := src
override BUILDDIR := build
override UTILS_DIR := utils
override ASSETS_DIR := assets
override TARGET = $(BUILDDIR)/shimeji-overlayd
override PLUGINS_TARGET = $(BUILDDIR)/libpluginsupport.so

override PYTHON3 := $(shell which python3)

PREFIX ?= /usr/local

override CFLAGS  += -I$(SRCDIR) -I$(BUILDDIR) -Wall -Wextra -fno-strict-aliasing
override CFLAGS  += $(shell pkg-config --cflags wayland-client)
override LDFLAGS += $(shell pkg-config wayland-client wayland-cursor libarchive --libs) -lm

override WAYLAND_PROTOCOLS_DIR ?= $(shell pkg-config wayland-protocols --variable=pkgdatadir)
override WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

ifeq (,$(WAYLAND_PROTOCOLS_DIR))
$(error Missing wayland-protocols!)
endif
ifeq (,$(WAYLAND_SCANNER))
$(error Missing wayland-scanner!)
endif

override WL_PROTO_DIR := $(BUILDDIR)/wayland-protocols

override WL_HEADERS := \
	$(WL_PROTO_DIR)/cursor-shape-v1.h \
	$(WL_PROTO_DIR)/viewporter.h \
	$(WL_PROTO_DIR)/tablet-v2.h \
	$(WL_PROTO_DIR)/xdg-shell.h \
	$(WL_PROTO_DIR)/fractional-scale-v1.h \
	$(WL_PROTO_DIR)/wlr-layer-shell.h \
	$(WL_PROTO_DIR)/xdg-output.h

override SRC := \
	$(wildcard $(SRCDIR)/*.c) \
	$(wildcard $(SRCDIR)/actions/*.c) \
	$(wildcard $(SRCDIR)/protocol/*.c) \
	$(WL_HEADERS:.h=.c)

override OBJS := $(SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
override OBJS := $(OBJS:$(WL_PROTO_DIR)/%.c=$(WL_PROTO_DIR)/%.o)

override DEPS := $(OBJS:.o=.d)
override DIRS := $(sort $(BUILDDIR) $(dir $(OBJS)))

override PLUGINS_SRC := src/environment.c src/mascot.c src/expressions.c src/utils.c src/plugins.c src/config.c \
	$(WL_HEADERS:.h=.c)
override PLUGINS_OBJS := $(PLUGINS_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
override PLUGINS_OBJS := $(PLUGINS_OBJS:$(WL_PROTO_DIR)/%.c=$(WL_PROTO_DIR)/%.o)

override _ := $(shell mkdir -p $(DIRS))

# Generate wayland-protocols headers & sources
protocols-autogen:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml         $(WL_PROTO_DIR)/cursor-shape-v1.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/staging/cursor-shape/cursor-shape-v1.xml         $(WL_PROTO_DIR)/cursor-shape-v1.c
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/viewporter/viewporter.xml                 $(WL_PROTO_DIR)/viewporter.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/stable/viewporter/viewporter.xml                 $(WL_PROTO_DIR)/viewporter.c
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml                      $(WL_PROTO_DIR)/tablet-v2.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/stable/tablet/tablet-v2.xml                      $(WL_PROTO_DIR)/tablet-v2.c
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml                   $(WL_PROTO_DIR)/xdg-shell.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml                   $(WL_PROTO_DIR)/xdg-shell.c
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/staging/fractional-scale/fractional-scale-v1.xml $(WL_PROTO_DIR)/fractional-scale-v1.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/staging/fractional-scale/fractional-scale-v1.xml $(WL_PROTO_DIR)/fractional-scale-v1.c
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml   $(WL_PROTO_DIR)/xdg-output.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-output/xdg-output-unstable-v1.xml   $(WL_PROTO_DIR)/xdg-output.c
	$(WAYLAND_SCANNER) client-header wlr-protocols/wlr-layer-shell-unstable-v1.xml                             $(WL_PROTO_DIR)/wlr-layer-shell.h
	$(WAYLAND_SCANNER) private-code  wlr-protocols/wlr-layer-shell-unstable-v1.xml                             $(WL_PROTO_DIR)/wlr-layer-shell.c

# Rule to build wayland-protocol sources
$(WL_PROTO_DIR)/%.o: $(WL_PROTO_DIR)/%.c Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

# Rule to build generic source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

$(PLUGINS_TARGET): $(PLUGINS_OBJS)
	$(CC) $(shell pkg-config --cflags wayland-client) $(PLUGINS_SRC) -DPLUGINSUPPORT_IMPLEMENTATION -I$(BUILDDIR) -fPIC -shared -lm -o $(PLUGINS_TARGET)

# Rule to build the binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Rule to build shimejictl
$(UTILS_DIR)/shimejictl: $(SRCDIR)/shimejictl/client.py
	mkdir utils
	$(PYTHON3) scripts/py-compose.py -s $< -o $@

$(SRC): protocols-autogen
$(PLUGINS_SRC): protocols-autogen

.PHONY: all
all: $(TARGET) $(PLUGINS_TARGET) $(UTILS_DIR)/shimejictl

.PHONY: clean
clean:
	@-rm -rf $(TARGET) $(PLUGINS_TARGET) $(BUILDDIR) $(UTILS_DIR)/shimejictl $(UTILS_DIR)

.PHONY: install_plugins
install_plugins:
	install -d $(DESTDIR)$(PREFIX)/lib/
	install -m755 $(PLUGINS_TARGET) $(DESTDIR)$(PREFIX)/lib/

.PHONY: install
install: $(UTILS_DIR)/shimejictl
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -m755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -m755 $(UTILS_DIR)/shimejictl $(DESTDIR)$(PREFIX)/bin/shimejictl
	install -d $(DESTDIR)$(PREFIX)/usr/share/systemd/user/
	install -m644 systemd/wl_shimeji.socket $(DESTDIR)$(PREFIX)/usr/share/systemd/user/
	install -m644 systemd/wl_shimeji.service $(DESTDIR)$(PREFIX)/usr/share/systemd/user/

# Handle header dependency rebuild
sinclude $(DEPS)

.DEFAULT_GOAL := $(TARGET)
