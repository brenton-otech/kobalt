#!/bin/sh

# "kpm launch kobalt" runs this hook from the package folder. Start Kobalt the
# same way as the KUAL entry; extra arguments, such as --library, pass through.
exec sh "${KOBALT_KPM_APP:-/mnt/us/kobalt}/launcher/launch.sh" "$@"
