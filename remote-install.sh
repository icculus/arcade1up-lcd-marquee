#!/bin/bash

# arcade1up-lcd-marquee; control an LCD in a Arcade1Up marquee.
#
# Please see the file LICENSE.txt in the source's root directory.
#
#  This file written by Ryan C. Gordon.

if [ `id -u` != "0" ]; then
    echo "You have to run this script as root. Try sudo." 1>&2
    exit 1
fi

if [ ! -d /home/pi ]; then
    echo "ERROR: This script expects you to have a writable /home/pi directory." 1>&2
    exit 1
fi

cd /home/pi

if [ ! -d arcade1up-lcd-marquee ]; then
    hg clone https://hg.icculus.org/icculus/arcade1up-lcd-marquee || exit 1
    cd arcade1up-lcd-marquee
else
    cd arcade1up-lcd-marquee
    hg pull
    hg update
fi

exec ./install.sh

