BUILDDIR         ?= build
PLUGINS_OUT_DIR  ?= $(BUILDDIR)/plugins
CC               ?= cc
OBJCOPY          ?= objcopy
CFLAGS           ?=
LDFLAGS          ?=

DATA_SRC         := data.js
ROOT_SRC         := root.c
DATA_OBJ         := $(BUILDDIR)/data.o
ROOT_OBJ         := $(BUILDDIR)/root.o
TARGET           := $(PLUGINS_OUT_DIR)/libkwinsupport.so

.PHONY: all clean
all: $(TARGET)

# ensure build dirs exist
$(BUILDDIR) $(PLUGINS_OUT_DIR):
	@mkdir -p $@

# embed data.js as an object file
$(DATA_OBJ): $(DATA_SRC) | $(BUILDDIR)
	$(OBJCOPY) -I binary -O default $< $@

# compile root.c
$(ROOT_OBJ): $(ROOT_SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@ -fPIC

# link shared object
$(TARGET): $(DATA_OBJ) $(ROOT_OBJ) | $(PLUGINS_OUT_DIR)
	$(CC) $(LDFLAGS) -shared -lwayland-shimeji-plugins -lsystemd -o $@ $^

# clean up
clean:
	rm -rf $(BUILDDIR) $(TARGET)
