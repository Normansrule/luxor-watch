# Host-side tests for Luxor.
#
# These do NOT need ESP-IDF, a toolchain for the ESP32-S3, or the watch.  They
# compile the pure-logic parts of the firmware with your system compiler and
# check them, so a geometry or sign mistake is caught in a second instead of a
# flash-and-squint cycle.
#
#   make            build and run everything
#   make layout     just the screen geometry
#   make motion     just the tilt / step / wrist-raise logic
#   make clean
#
# On the WSL2 Ubuntu box:  sudo apt install build-essential
# On Windows:              run this under WSL, or use any gcc/clang.

CC      ?= cc
CFLAGS  ?= -std=c11 -O1 -Wall -Wextra -I../main
LDLIBS  ?= -lm

BINDIR  := build
TESTS   := layout motion fitness gestures protocol battery

.PHONY: all clean $(TESTS)

all: $(TESTS)

$(BINDIR):
	@mkdir -p $(BINDIR)

layout: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_layout test_layout.c $(LDLIBS)
	@./$(BINDIR)/test_layout

motion: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_motion test_motion.c ../main/motion.c $(LDLIBS)
	@./$(BINDIR)/test_motion

fitness: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_fitness test_fitness.c ../main/fitness.c $(LDLIBS)
	@./$(BINDIR)/test_fitness

gestures: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_gestures test_gestures.c ../main/gestures.c $(LDLIBS)
	@./$(BINDIR)/test_gestures

protocol: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_protocol test_protocol.c ../main/protocol.c $(LDLIBS)
	@./$(BINDIR)/test_protocol

battery: $(BINDIR)
	@$(CC) $(CFLAGS) -o $(BINDIR)/test_battery test_battery.c ../main/battery.c $(LDLIBS)
	@./$(BINDIR)/test_battery

clean:
	@rm -rf $(BINDIR)
