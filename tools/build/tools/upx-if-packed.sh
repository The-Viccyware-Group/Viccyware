#!/bin/bash

if [[ ! -f "${1}" ]]; then
	echo "must give me a file to pack"
	exit 1
fi

if [[ ! "$(strings ${1})" == *'This file is packed with the UPX executable packer'* ]]; then
	upx --lzma --best "${1}" > /dev/null
fi
