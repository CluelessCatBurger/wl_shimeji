
override SRCDIR := src
override BUILDDIR := build
override UTILS_DIR := utils
override ASSETS_DIR := assets
override TARGET = $(BUILDDIR)/shimeji-overlayd

PREFIX ?= /usr/local

override CFLAGS  += -I$(SRCDIR) -I$(BUILDDIR) -O2 -Wall -Wextra -fno-strict-aliasing -DWL_SHIMEJI_ASSETS_PATH='"$(PREFIX)/share/wl-shimeji/assets"'
override LDFLAGS += $(shell pkg-config wayland-client spng --libs) -lm

override WAYLAND_PROTOCOLS_DIR := $(shell pkg-config wayland-protocols --variable=pkgdatadir)
override WLR_PROTOCOLS_DIR = $(shell pkg-config wlr-protocols --variable=pkgdatadir)
override WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

ifeq (,$(WAYLAND_PROTOCOLS_DIR))
$(error Missing wayland-protocols!)
endif
ifeq (,$(WLR_PROTOCOLS_DIR))
$(error Missing wlr-protocols!)
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
	$(WL_PROTO_DIR)/wlr-layer-shell.h

override SRC := \
	$(wildcard $(SRCDIR)/*.c) \
	$(wildcard $(SRCDIR)/actions/*.c) \
	$(WL_HEADERS:.h=.c)

override OBJS := $(SRC:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
override OBJS := $(OBJS:$(WL_PROTO_DIR)/%.c=$(WL_PROTO_DIR)/%.o)

override DEPS := $(OBJS:.o=.d)
override DIRS := $(sort $(BUILDDIR) $(dir $(OBJS)))

override _ := $(shell mkdir -p $(DIRS))

# Ignore deleted header files
%.h:
	@:

# Generate wayland-protocols headers & sources
$(WL_PROTO_DIR)/%.h:
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
	$(WAYLAND_SCANNER) client-header $(WLR_PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml             $(WL_PROTO_DIR)/wlr-layer-shell.h
	$(WAYLAND_SCANNER) private-code  $(WLR_PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml             $(WL_PROTO_DIR)/wlr-layer-shell.c

# Rule to build wayland-protocol sources
$(WL_PROTO_DIR)/%.o: $(WL_PROTO_DIR)/%.c Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

# Rule to build generic source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(WL_HEADERS) Makefile
	$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -c $< -o $@

# Rule to build the binary
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

.PHONY: all
all: $(TARGET)

.PHONY: clean
clean:
	@-rm -r $(BUILDDIR) $(TARGET)

.NOTPARALLEL: $(WL_PROTO_DIR)/%.o

.PHONY: install
install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(PREFIX)/share/wl-shimeji/assets/
	install -m755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	install -m755 $(UTILS_DIR)/shimejictl $(DESTDIR)$(PREFIX)/bin/shimejictl
	install -m644 $(ASSETS_DIR)/* $(DESTDIR)$(PREFIX)/share/wl-shimeji/assets/

# Handle header dependency rebuild
sinclude $(DEPS)
