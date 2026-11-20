#!/bin/sh
# Name: Kobalt (pause stock UI)
# Author: Kobalt contributors
# Scriptlet for jailbroken firmware without KUAL. Starts Kobalt like "Kobalt",
# but pauses the stock interface instead of stopping it.
if [ ! -f /mnt/us/kobalt/launcher/launch.sh ]; then
    echo 'Kobalt is not installed: extract the package at the Kindle USB root.'
    exit 1
fi
exec sh /mnt/us/kobalt/launcher/launch.sh --pause-ui
