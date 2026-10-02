# Luxor UI simulator: compiles the real main/ui.c against LVGL 8.3.11 on a PC.
#
#   git clone --depth 1 --branch v8.3.11 https://github.com/lvgl/lvgl.git ~/lvgl
#   make LVGL_DIR=~/lvgl -j8            build ./luxor_sim
#   make LVGL_DIR=~/lvgl check          text-fit audit of every screen and theme
#   make LVGL_DIR=~/lvgl gallery        render every screen to gallery.png (needs python3-pil)
#
# stub/sdkconfig.h is the LVGL part of the firmware's sdkconfig, with
# LV_COLOR_16_SWAP off so frames come out in plain RGB565.

LVGL_DIR ?= $(HOME)/lvgl
MAIN     := ../../main
CC       ?= cc
CFLAGS   := -O1 -w '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"' -DLV_CONF_SKIP \
            -Istub -I$(LVGL_DIR) -I$(LVGL_DIR)/src -I$(MAIN)
OBJDIR   := build

LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null)
APP_SRC  := $(MAIN)/ui.c $(MAIN)/battery.c $(wildcard $(MAIN)/fonts/*.c) sim.c
OBJS     := $(patsubst $(LVGL_DIR)/%.c,$(OBJDIR)/lvgl/%.o,$(LVGL_SRC)) \
            $(patsubst %.c,$(OBJDIR)/app/%.o,$(notdir $(APP_SRC)))

vpath %.c $(MAIN) $(MAIN)/fonts .

.PHONY: all check gallery clean
all: luxor_sim

luxor_sim: $(OBJS)
	@$(CC) -o $@ $^ -lm

$(OBJDIR)/lvgl/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/app/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

check: luxor_sim
	@./luxor_sim check

APPS := 0 1 2 3 4 5 6 7 8 9 10
gallery: luxor_sim
	@mkdir -p frames
	@for a in $(APPS); do ./luxor_sim render 0 $$a 82 frames/ivory_$$a.raw; done
	@for a in $(APPS); do ./luxor_sim render 2 $$a 82 frames/noir_$$a.raw; done
	@python3 montage.py gallery_ivory.png $(foreach a,$(APPS),frames/ivory_$(a).raw)
	@python3 montage.py gallery_noir.png $(foreach a,$(APPS),frames/noir_$(a).raw)
	@echo "wrote gallery_ivory.png gallery_noir.png"

clean:
	rm -rf $(OBJDIR) frames luxor_sim gallery_*.png
