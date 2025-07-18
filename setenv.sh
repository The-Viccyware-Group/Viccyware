#!/usr/bin/env bash

export GIT_PROJ_ROOT="$(
  cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 \
  && git rev-parse --show-toplevel
)"

alias victor_restart="cd ${GIT_PROJ_ROOT} && ${GIT_PROJ_ROOT}/project/victor/scripts/victor_restart.sh"
alias victor_start="cd ${GIT_PROJ_ROOT} && ${GIT_PROJ_ROOT}/project/victor/scripts/victor_start.sh"
alias victor_stop="cd ${GIT_PROJ_ROOT} && ${GIT_PROJ_ROOT}/project/victor/scripts/victor_stop.sh"

alias victor_build_release="cd ${GIT_PROJ_ROOT} && ${GIT_PROJ_ROOT}/project/victor/scripts/victor_build_release.sh"
alias victor_deploy_run="cd ${GIT_PROJ_ROOT} && ${GIT_PROJ_ROOT}/project/victor/scripts/victor_deploy_run.sh"

function IS_ROBOT_IP_THERE() {
    if [[ ! -f "$GIT_PROJ_ROOT/robot_ip.txt" ]]; then
        echo "Robot IP file doesn't exist. What is your robot's IP?"
        echo -n "(XXX.XXX.XXX.XXX): "
        read ipaddr
        echo "$ipaddr" > "$GIT_PROJ_ROOT/robot_ip.txt"
    fi
}

alias vclean='rm -rf ${GIT_PROJ_ROOT}/_build ${GIT_PROJ_ROOT}/generated'
alias vbuild=victor_build_release
alias vdeploy='IS_ROBOT_IP_THERE; victor_deploy_run'
alias vbd='victor_build_release && victor_deploy_run'
