#!/usr/bin/env bash

# usage: ./tools/build/tools/upx-if-packed.sh <path/to/upx> <path/to/file>

if [[ ! -x "${1}" ]]; then
	echo "provide an executable UPX please"
fi

if [[ ! -f "${2}" ]]; then
	echo "must give me a file to pack"
	exit 1
fi

if [[ ! "$(strings ${1})" == *'This file is packed with the UPX executable packer'* ]]; then
	upx --lzma --best "${1}" > /dev/null
fi
