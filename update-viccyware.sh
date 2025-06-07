#!/usr/bin/env bash
git pull
cd EXTERNALS
git checkout viccyware
git pull
cd ..
cd resources/config/engine/animations
git checkout main
git pull
cd ../../../../
