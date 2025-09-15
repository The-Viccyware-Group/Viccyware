#!/bin/bash

$HOME/.anki/vicos-sdk/dist/5.3.0-r07/prebuilt/bin/arm-oe-linux-gnueabi-clang++ \
-L$(pwd)/../../3rd/libarchive/vicos/lib -I$(pwd)/../../3rd/libarchive/vicos/include \
-larchive -lpthread -O2 \
-o update-engine update-engine.cpp
