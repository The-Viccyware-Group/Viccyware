#!/bin/bash

set -e

if [[ ! -d EXTERNALS/ ]]; then
    echo "Getting EXTERNALS"
    wget https://modder.my.to/cozmo-externals.tar.gz
    tar xzf old-externals.tar.gz
    rm old-externals.tar.gz
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR

docker build -t victor .
docker build -t victor-${USER} -f Dockerfile.dev --build-arg USER=${USER} --build-arg UID=$(id -u $USER) --build-arg VIC_DIR=${DIR}/../ .
docker run -t \
       -u ${USER} \
       -v ~/:/home/${USER}/:delegated \
       --privileged victor-${USER}
