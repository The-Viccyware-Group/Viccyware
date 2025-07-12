#!/usr/bin/env bash

VER_DEFAULT="31.1"

if [[ $1 != "" ]]; then
    VER="$1"
else
    VER=$VER_DEFAULT
fi

ADEPS="$HOME/.anki"

#https://github.com/protocolbuffers/protobuf/releases/download/v31.1/protoc-31.1-linux-x86_64.zip

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="linux-x86_64"
elif [[ "$(uname -a)" == *"aarch64"* && "$(uname -a)" == *"Linux"* ]]; then
    HOST="linux-aarch_64"
elif [[ "$(uname -a)" == *"arm64"* && "$(uname -a)" == *"Darwin"* ]]; then
    HOST="osx-aarch_64"
else
    echo "protoc: unknown arch"
    exit 0
fi

echo "ProtoC platform: $HOST"

#https://github.com/upx/upx/releases/download/v5.0.0/upx-5.0.0-arm64_linux.tar.xz

PROTOC_NAME="protoc-${VER}-${HOST}"

if [[ ! -f "$HOME/.anki/protoc/dist/$VER/bin/protoc" ]]; then
    mkdir -p "$HOME/.anki/protoc/dist/$VER"
    cd "$HOME/.anki/protoc/dist/$VER"
    echo "Downloading ProtoC $VER..."
    wget -q --show-progress "https://github.com/protocolbuffers/protobuf/releases/download/v${VER}/${PROTOC_NAME}.zip"
    echo "Extracting ProtoC $VER..."
    unzip "${PROTOC_NAME}.zip"
    rm -f ${PROTOC_NAME}.zip
fi

