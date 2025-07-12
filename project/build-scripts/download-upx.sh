#!/usr/bin/env bash

UPX_VER_DEFAULT="5.0.1"

if [[ $1 != "" ]]; then
    UPX_VER="$1"
else
    UPX_VER=$UPX_VER_DEFAULT
fi

ADEPS="$HOME/.anki"

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="amd64_linux"
elif [[ "$(uname -a)" == *"aarch64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="arm64_linux"
else
    echo "Skipping upx download"
    exit 0
fi

echo "UPX platform: $HOST"

#https://github.com/upx/upx/releases/download/v5.0.0/upx-5.0.0-arm64_linux.tar.xz

UPX_NAME="upx-${UPX_VER}-${HOST}"

if [[ ! -f "$HOME/.anki/upx/dist/$UPX_VER/upx" ]]; then
    mkdir -p "$HOME/.anki/upx/dist/$UPX_VER"
    cd "$HOME/.anki/upx/dist/$UPX_VER"
    echo "Downloading UPX $UPX_VER..."
    wget -q --show-progress "https://github.com/upx/upx/releases/download/v${UPX_VER}/${UPX_NAME}.tar.xz"
    echo "Extracting UPX $UPX_VER..."
    tar -xf "${UPX_NAME}.tar.xz"
    mv "upx-${UPX_VER}-${HOST}/upx" .
    rm -rf "upx-${UPX_VER}-${HOST}*"
fi

