#!/usr/bin/env bash

if [[ ! -f ./ANKI_VERSION ]]; then
    if [[ -f ../ANKI_VERSION ]]; then
        cd ..
    else
        echo "This script must be run in the victor repo. ./wire/build.sh"
        exit 1
    fi
fi

rm -rf _build generated

echo "cleaned"
