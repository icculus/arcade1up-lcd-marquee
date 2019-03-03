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

if [ ! -d /home/pi/arcade1up-lcd-marquee ]; then
    echo "ERROR: This script expects you to have a writable /home/pi/arcade1up-lcd-marquee directory." 1>&2
    exit 1
fi

if [ -f /opt/retropie/configs/all/runcommand-onstart.sh ]; then
    if [ `readlink /opt/retropie/configs/all/runcommand-onstart.sh` != "/home/pi/arcade1up-lcd-marquee/runcommand-onstart.sh" ]; then
        echo "ERROR: /opt/retropie/configs/all/runcommand-onstart.sh already exist. Can't go on." 1>&2
        echo "ERROR: Move this file out of the way and maybe merge your own script later." 1>&2
        exit 1
    fi
fi

if [ -f /opt/retropie/configs/all/runcommand-onend.sh ]; then
    if [ `readlink /opt/retropie/configs/all/runcommand-onend.sh` != "/home/pi/arcade1up-lcd-marquee/runcommand-onend.sh" ]; then
        echo "ERROR: /opt/retropie/configs/all/runcommand-onend.sh already exist. Can't go on." 1>&2
        echo "ERROR: Move this file out of the way and maybe merge your own script later." 1>&2
        exit 1
    fi
fi

cd /home/pi/arcade1up-lcd-marquee

apt install libdbus-1-dev libevdev-dev libxml-libxml-perl

echo "Icculus's LCD Marquee software installer!"
echo

cat LICENSE.txt

echo "building latest version. This takes 15-20 seconds on a Raspberry Pi 3+..."
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

echo >> /boot/config.txt
echo "# Don't use the DSI-connnected touchscreen display as the primary output." >> /boot/config.txt
echo "display_default_lcd=0" >> /boot/config.txt
echo >> /boot/config.txt
echo "# rotate image to be right-side-up (we have the screen installed upside-down)." >> /boot/config.txt
echo "lcd_rotate=2" >> /boot/config.txt
echo >> /boot/config.txt

sync

echo
echo "Should be good to go now! (I hope.)"
echo
echo "If your touchscreen is the primary display (and/or upside down),"
echo " reboot now to fix it. Just run 'sudo reboot' or cycle the power."
echo
echo "btw, I have a Patreon that pays for fun things like this."
echo " Throw in a buck if you dig this sort of thing:"
echo
echo "  https://patreon.com/icculus"
echo
echo "Thanks!"
echo "--ryan."
echo

# end of install.sh ...
