#!/usr/bin/env bash

set -e

echo "Cleaning the mess here"
./build/clean.sh

./build/bd.sh
