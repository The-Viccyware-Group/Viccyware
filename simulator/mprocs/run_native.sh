#!/bin/bash

SCRIPT_PATH=$(dirname $([ -L $0 ] && echo "$(dirname $0)/$(readlink -n $0)" || echo $0))
SCRIPT_NAME=`basename ${0}`
TOPLEVEL=$(cd "$SCRIPT_PATH/../.." && pwd)

cd "${TOPLEVEL}"

if [[ "${OSPATH}" == "" ]]; then
	echo "OSPATH wasn't provided"
	exit 1
fi

TO_RUN="$1"

if [[ $TO_RUN == "" ]]; then
	echo "usage: OSPATH=<path> run_native.sh <program> --and --args"
	exit 1
fi

shift

BIN="${TOPLEVEL}/_build/vicos/Simulator/native/${TO_RUN}"

if [[ ! -x "$BIN" ]]; then
	echo "no host ${TO_RUN}, running the one in the chroot"
	exec bash "${TOPLEVEL}/simulator/mprocs/run_chroot.sh" "/anki/bin/${TO_RUN}" "$@"
fi

exec env VIC_MIC_DUMP=/tmp/cloud_chroot.raw VIC_FS_ROOT="${OSPATH}" "$BIN" "$@"
