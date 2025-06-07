#!/bin/bash

set -e

echo "Cleaning the mess here"
./wire/clean.sh

echo "Building"
./wire/build-d.sh

echo "Putting on robot now..."
./wire/deploy-d.sh
