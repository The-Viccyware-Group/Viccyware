#!/usr/bin/env bash

# usage: ./cloud/protoc-wrapper.sh <path/to/protoc> <args>

SCRIPT_PATH="$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))"

TOPLEVEL="$(cd "${SCRIPT_PATH}/.." && pwd)"

PATH="$PATH:${TOPLEVEL}/cloud/go/bin" $1 ${@:2}