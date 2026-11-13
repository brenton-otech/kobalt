#!/bin/sh
# Name: Kobalt
# Author: Kobalt contributors
# Scriptlet for jailbroken firmware without KUAL. Tapping this item in the
# Kindle library starts Kobalt from the package extracted at the USB root.
if [ ! -f /mnt/us/kobalt/launcher/launch.sh ]; then
    echo 'Kobalt is not installed: extract the package at the Kindle USB root.'
    exit 1
fi
exec sh /mnt/us/kobalt/launcher/launch.sh
