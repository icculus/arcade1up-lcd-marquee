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

echo "We're going to download, build and install what you need for the"
echo " LCD marquee. This doesn't take long. We'll be installing Mercurial if"
echo " necessary, and putting a systemd service in place to manage the LCD"
echo " control process."
echo
echo "This process is known to work on a reasonably modern version of"
echo " RetroPie, and probably generic Raspbian. YMMV. You can report problems"
echo " to Ryan <icculus@icculus.org>"
echo
echo "Press enter to start, CTRL-C now to abort."

read waitforinput </dev/tty

cd /home/pi

if [ ! -d arcade1up-lcd-marquee ]; then
    hg clone https://hg.icculus.org/icculus/arcade1up-lcd-marquee || exit 1
    cd arcade1up-lcd-marquee
else
    cd arcade1up-lcd-marquee
    hg pull
    hg update
fi

chown -R pi ../arcade1up-lcd-marquee

exec ./install.sh

