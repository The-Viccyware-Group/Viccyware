#!/usr/bin/env bash
git reset --hard
git submodule sync --recursive
git submodule update --init --force --recursive
git clean -ffdx
git submodule foreach --recursive git clean -ffdx
git pull
cd EXTERNALS
git checkout viccyware
git reset --hard
git clean -ffdx
git pull
cd ..
cd resources/config/engine/animations
git checkout main
git reset --hard
git clean -ffdx
git pull
cd ../../../../

