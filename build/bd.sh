#!/usr/bin/env bash

set -e

echo "Building..."
./build/build-v.sh

echo "Sending build to bot..."
./build/deploy-v.sh
