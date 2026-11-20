#!/bin/sh
# Name: Kobalt (stop stock UI)
# Author: Kobalt contributors
# Scriptlet for jailbroken firmware without KUAL. Starts Kobalt like "Kobalt",
# but stops the stock interface instead of pausing it and restarts it on exit.
if [ ! -f /mnt/us/kobalt/launcher/launch.sh ]; then
    echo 'Kobalt is not installed: extract the package at the Kindle USB root.'
    exit 1
fi
exec sh /mnt/us/kobalt/launcher/launch.sh --stop-ui
