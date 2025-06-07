#!/usr/bin/env bash

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

chmod 600 robot_sshkey

if [[ "$(uname -a)" == *"Darwin"* ]]; then
    ssh-add robot_sshkey && \
    ./project/victor/scripts/victor_deploy.sh -c Release -b && \
    ./project/victor/scripts/victor_start.sh
else

docker run --rm -it \
    -v $(pwd)/anki-deps:/home/$USER/.anki \
    -v $(pwd):$(pwd) \
    -v $(pwd)/build/cache:/home/$USER/.ccache \
    -v /home/$USER/.ssh:/home/$USER/.ssh \
    vic-standalone-builder-5 bash -c \
    "cd $(pwd) && \
    eval \$(ssh-agent) && \
    ssh-add robot_sshkey && \
    ./project/victor/scripts/victor_deploy.sh -c Release -b && \
    ./project/victor/scripts/victor_start.sh"

fi
