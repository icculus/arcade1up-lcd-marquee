#!/bin/bash

gcc -Wall -Os -o marquee-displaydaemon marquee-displaydaemon.c `sdl2-config --cflags --libs`
