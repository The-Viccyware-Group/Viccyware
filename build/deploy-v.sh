#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/.."
echo $DIR
cd $DIR

if [[ ! -f robot_ip.txt ]]; then
    echo "You must echo your robot's IP to robot_ip.txt in the root of this repo before running this script."
    exit 1
fi

if [[ ! -f robot_sshkey ]]; then
    echo "You must copy your robot's SSH key the root of this repo with the name robot_sshkey before running this script."
    exit 1
fi
if [[ "$(uname -a)" == *"Darwin"* ]]; then
    ssh-add robot_sshkey && \
    ./project/victor/scripts/victor_deploy.sh -c Release -b && \
    ./project/victor/scripts/victor_start.sh
else

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
    -v /home/$USER/.ssh:/home/$USER/.ssh \
    vic-standalone-builder-2 bash -c \
    "cd $(pwd) && \
    eval \$(ssh-agent) && \
    ssh-add robot_sshkey && \
    ./project/victor/scripts/victor_deploy.sh -c Release -b && \
    ./project/victor/scripts/victor_start.sh"

fi
