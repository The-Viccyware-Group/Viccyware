#!/usr/bin/env bash

# usage: ./tools/build/tools/upx-if-packed.sh <path/to/upx> <path/to/file>

# also want to handle upx being installed on system
if [[ ! -x "${1}" && "${1}" != "upx" ]]; then
	echo "provide an executable UPX please"
	exit 1
fi

if [[ ! -f "${2}" ]]; then
	echo "must give me a file to pack"
	exit 1
fi

if [[ ! "$(strings ${2})" == *'This file is packed with the UPX executable packer'* ]]; then
	${1} --best --lzma "${2}" > /dev/null
fi
