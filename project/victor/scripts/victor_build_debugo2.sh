#!/usr/bin/env bash

export GIT_PROJ_ROOT=`git rev-parse --show-toplevel`
${GIT_PROJ_ROOT}/project/victor/build-victor.sh \
		-c Debug \
		-O2 \
    -DUSE_ANKITRACE=OFF \
    -DDO_DEV_POSE_CHECKS=0 \
		"$@"
