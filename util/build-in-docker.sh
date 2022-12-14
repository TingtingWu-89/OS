#!/bin/bash

# Give other users access to /root
# (We probably should have just built the build tools somewhere else...)
chmod o+x /root
chmod -R o+rw /root/gcc_local/bin

# Who owns this directory?
NEWUID=`stat -c '%u' .`

if [[ "$NEWUID" == "0" ]]; then
    echo "Are you running this on Docker for Mac? Owner UID is 0, going to use 501 instead."
    NEWUID=501
fi

# Create a fake user with this name
useradd -u $NEWUID local

# Map the build tools
ln -s /root/gcc_local util/local

# Run make as local
runuser -u local -- sh -c 'make base/lib/libc.so && make -j4' || exit 1

# Remove the build tools
rm util/local

