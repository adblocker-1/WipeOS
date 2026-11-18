# ShredOS GUI
CC ?= gcc
TARGET ?= shredos-gui
LVGL_DIR := lvgl
CFLAGS ?= -O2
APP_CFLAGS := -I. -Iapp -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE -Wall -Wno-unused-function -Wno-missing-field-initializers -std=gnu11
LDLIBS := -lm -lpthread

LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c')
APP_SRCS := $(wildcard app/*.c)
OBJS := $(patsubst %.c,build/%.o,$(LVGL_SRCS) $(APP_SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c lv_conf.h app/app.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_CFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/shredos-gui

.PHONY: all clean install
