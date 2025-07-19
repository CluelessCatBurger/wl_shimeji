
override SRCDIR := src
override BUILDDIR := build
override UTILS_DIR := utils
override TARGET = $(BUILDDIR)/shimeji-overlayd
override PLUGINS_LIB = $(BUILDDIR)/libwayland-shimeji-plugins.so

override PLUGINS_DIR := $(SRCDIR)/plugins
override PLUGINS_OUT_DIR := $(BUILDDIR)/plugins

override PLUGINS_SUBDIRS := $(notdir $(wildcard $(PLUGINS_DIR)/*))
override PYTHON3 := $(shell which python3)

PREFIX ?= /usr/local

override CFLAGS  += -I$(abspath $(SRCDIR)) -I$(abspath $(BUILDDIR)) -Wall -Wextra -fno-strict-aliasing
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
	$(WL_PROTO_DIR)/xdg-output.h \
	$(WL_PROTO_DIR)/alpha-modifier-v1.h

override SRC := \
	$(wildcard $(SRCDIR)/*.c) \
	$(wildcard $(SRCDIR)/actions/*.c) \
	$(wildcard $(SRCDIR)/protocol/*.c) \
	$(WL_HEADERS:.h=.c)

override OBJS := $(SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
override OBJS := $(OBJS:$(WL_PROTO_DIR)/%.c=$(WL_PROTO_DIR)/%.o)

override DEPS := $(OBJS:.o=.d)
override DIRS := $(sort $(BUILDDIR) $(dir $(OBJS)))

override PLUGINS_LIB_SRC := src/plugins.c src/utils.c
override PLUGINS_LIB_OBJS := $(PLUGINS_LIB_SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

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
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS_DIR)/staging/alpha-modifier/alpha-modifier-v1.xml     $(WL_PROTO_DIR)/alpha-modifier-v1.h
	$(WAYLAND_SCANNER) private-code  $(WAYLAND_PROTOCOLS_DIR)/staging/alpha-modifier/alpha-modifier-v1.xml     $(WL_PROTO_DIR)/alpha-modifier-v1.c
	$(WAYLAND_SCANNER) client-header wlr-protocols/wlr-layer-shell-unstable-v1.xml                             $(WL_PROTO_DIR)/wlr-layer-shell.h
	$(WAYLAND_SCANNER) private-code  wlr-protocols/wlr-layer-shell-unstable-v1.xml                             $(WL_PROTO_DIR)/wlr-layer-shell.c


# Rule to build wayland-protocol sources
$(WL_PROTO_DIR)/%.o: $(WL_PROTO_DIR)/%.c Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

# Rule to build generic source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

$(PLUGINS_LIB): $(PLUGINS_LIB_OBJS)
	$(CC) $(CFLAGS) $(PLUGINS_LIB_SRC) -DBUILD_PLUGIN_SUPPORT -I$(BUILDDIR) -fPIC -shared -lm -o $(PLUGINS_LIB)

$(PLUGINS_SUBDIRS): $(PLUGINS_LIB)
	@echo "-> Building plugin $@..."
	$(MAKE) -s -C $(PLUGINS_DIR)/$@ BUILDDIR="$(abspath $(BUILDDIR))" PLUGINS_OUT_DIR="$(abspath $(PLUGINS_OUT_DIR))" CFLAGS="$(CFLAGS) -I$(DESTDIR)$(PREFIX)/include/" LDFLAGS="$(LDFLAGS) -L$(abspath $(BUILDDIR))"

.PHONY: build-plugins
build-plugins: $(PLUGINS_SUBDIRS)

# Rule to build the binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

# Rule to build shimejictl
$(UTILS_DIR)/shimejictl: $(SRCDIR)/shimejictl/client.py
	@mkdir utils
	$(PYTHON3) scripts/py-compose.py -s $< -o $@

$(SRC): protocols-autogen

.PHONY: all
all: $(TARGET) $(PLUGINS_LIB) $(UTILS_DIR)/shimejictl

.PHONY: clean
clean:
	@-rm -rf $(TARGET) $(PLUGINS_LIB) $(BUILDDIR) $(UTILS_DIR)/shimejictl $(UTILS_DIR)

.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -m755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -m755 $(UTILS_DIR)/shimejictl $(DESTDIR)$(PREFIX)/bin/shimejictl
	install -d $(DESTDIR)$(PREFIX)/share/systemd/user/
	install -m644 systemd/wl_shimeji.socket $(DESTDIR)$(PREFIX)/share/systemd/user/
	install -m644 systemd/wl_shimeji.service $(DESTDIR)$(PREFIX)/share/systemd/user/
	install -d $(DESTDIR)$(PREFIX)/lib/
	install -m755 $(PLUGINS_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -d $(DESTDIR)$(PREFIX)/include/
	install -d $(DESTDIR)$(PREFIX)/include/wl_shimeji
	install -m655 src/plugins.h $(DESTDIR)$(PREFIX)/include/wl_shimeji/plugins.h

.PHONY: install-plugins
install-plugins: build-plugins
	install -d $(DESTDIR)$(PREFIX)/lib/wl_shimeji/
	install -m 0755 "$(PLUGINS_OUT_DIR)/"*.so "$(DESTDIR)$(PREFIX)/lib/wl_shimeji/"

# Handle header dependency rebuild
sinclude $(DEPS)

.DEFAULT_GOAL := all
