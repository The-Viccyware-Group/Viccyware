#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/.."
cd $DIR/..

if [[ ! -f robot_ip.txt ]]; then
    echo "You must echo your robot's IP to robot_ip.txt in the root of this repo before running this script."
    exit 1
fi

if [[ ! -f robot_sshkey ]]; then
    echo "You must copy your robot's SSH key the root of this repo with the name robot_sshkey before running this script."
    exit 1
fi

docker build \
--build-arg DIR_PATH="$(pwd)" \
--build-arg USER_NAME=$USER \
--build-arg UID=$(id -u $USER) \
--build-arg GID=$(id -g $USER) \
-t vic-standalone-builder-2 \
build/

docker run -it \
    -v $(pwd)/anki-deps:/home/$USER/.anki \
    -v $(pwd):$(pwd) \
    -v $(pwd)/build/cache:/home/$USER/.ccache \
    vic-standalone-builder-2 bash -c \
    "cd $(pwd) && \
    ./project/victor/scripts/victor_deploy.sh -c Release && \
    eval $(ssh-agent) && \
    ssh-add robot_sshkey && \
    echo "That appears to have been successful. Starting anki-robot.target..." \
    ./project/victor/scripts/victor_start.sh"

