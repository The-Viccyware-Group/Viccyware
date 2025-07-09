#!/bin/bash

set -e

if [[ ! -f ./CPPLINT.cfg ]]; then
    if [[ -f ../CPPLINT.cfg ]]; then
        cd ..
    else
        echo "This script must be run in the victor repo. ./wire/build.sh"
        exit 1
    fi
fi

VICDIR="$(pwd)"

cd ~
if [[ ! -d .anki ]]; then
    echo "Downloading ~/.anki folder contents..."
    git clone https://github.com/kercre123/anki-deps
    mv anki-deps .anki
fi

cd "${VICDIR}"

if [[ ! -d .anki ]]; then
    echo "Downloading 3rd folder contents..."
    wget https://modder.my.to/old-3rd.tar.gz
    tar xzf old-3rd.tar.gz
    rm old-3rd.tar.gz
fi

echo "Building victor..."

./project/victor/scripts/victor_build_debugo2.sh

echo "Copying vic-cloud and vic-gateway..."
cp bin/* _build/vicos/Debug/bin/

echo

echo "Build was successful!"
