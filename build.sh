#!/bin/bash

gcc -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard -Wall -Os -o marquee-displaydaemon marquee-displaydaemon.c `sdl2-config --cflags` `pkg-config --cflags --libs dbus-1 libevdev` -lm -Wl,-rpath,\$ORIGIN ./libSDL2-2.0.so.0

