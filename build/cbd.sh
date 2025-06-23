#!/usr/bin/env bash

set -e

echo "Cleaning the mess here"
./build/clean.sh

echo "Building"
./build/build-v.sh

echo "Sending to robot now..."
./build/deploy-v.sh
