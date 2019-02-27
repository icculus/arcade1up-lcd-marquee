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

systemctl stop marquee-lcd
systemctl disable marquee-lcd

rm /opt/retropie/configs/all/runcommand-onend.sh
rm /opt/retropie/configs/all/runcommand-onstart.sh
rm /opt/retropie/configs/all/runcommand-onstart-marquee-lcd.pl
rm /etc/dbus-1/system.d/marquee-lcd-dbus.conf
rm /lib/systemd/system/marquee-lcd.service

systemctl restart dbus.service
systemctl daemon-reload

echo "All done."

