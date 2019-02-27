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

if [ -f /opt/retropie/configs/all/runcommand-onstart.sh ]; then
    echo "ERROR: /opt/retropie/configs/all/runcommand-onstart.sh already exist. Can't go on." 1>&2
    echo "ERROR: Move this file out of the way and maybe merge your own script later." 1>&2
    exit 1
fi

if [ -f /opt/retropie/configs/all/runcommand-onend.sh ]; then
    echo "ERROR: /opt/retropie/configs/all/runcommand-onend.sh already exist. Can't go on." 1>&2
    echo "ERROR: Move this file out of the way and maybe merge your own script later." 1>&2
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

cat LICENSE.txt

./build.sh || exit 1

chown -R pi /home/pi/arcade1up-lcd-marquee

ln -sf /home/pi/arcade1up-lcd-marquee/runcommand-onend.sh /opt/retropie/configs/all/runcommand-onend.sh
ln -sf /home/pi/arcade1up-lcd-marquee/runcommand-onstart.sh /opt/retropie/configs/all/runcommand-onstart.sh
ln -sf /home/pi/arcade1up-lcd-marquee/runcommand-onstart-marquee-lcd.pl /opt/retropie/configs/all/runcommand-onstart-marquee-lcd.pl
ln -sf /home/pi/arcade1up-lcd-marquee/marquee-lcd-dbus.conf /etc/dbus-1/system.d/marquee-lcd-dbus.conf
ln -sf /home/pi/arcade1up-lcd-marquee/marquee-lcd.service /lib/systemd/system/marquee-lcd.service

systemctl daemon-reload
systemctl restart dbus.service
systemctl enable marquee-lcd
systemctl start marquee-lcd

echo "Should be good to go now! (I hope.)"

# end of install.sh ...
