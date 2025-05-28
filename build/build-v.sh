#!/bin/bash

set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/.."
cd $DIR
echo $DIR

if [[ "$(uname -a)" == *"x86_64"* && "$(uname -a)" == *"Linux"* ]]; then
	HOST="amd64-linux"
	ADEPS="anki-deps"
elif [[ "$(uname -a)" == *"arm64"* && "$(uname -a)" == *"Darwin"* ]]; then
	HOST="arm64-macos"
	ADEPS="$HOME/.anki"
else
    echo "This can only be run on x86_64 Linux or amd64 macOS systems at the moment."
    echo "This will be fixed once I compile the new toolchain for more platforms."
    exit 1
fi

echo $HOST

if [[ ! -d "$ADEPS/vicos-sdk/dist/4.0.0-r05/prebuilt" ]]; then
	mkdir -p "$ADEPS/vicos-sdk/dist/4.0.0-r05"
	cd "$ADEPS/vicos-sdk/dist/4.0.0-r05"
	wget -q --show-progress https://github.com/os-vector/wire-os-externals/releases/download/4.0.0-r05/vicos-sdk_4.0.0-r05_$HOST.tar.gz
	tar -zxf vicos-sdk_4.0.0-r05_$HOST.tar.gz
	rm -f vicos-sdk_4.0.0-r05_$HOST.tar.gz
fi

cd "$DIR"

if [[ ! -d "$ADEPS/wwise" ]]; then
	mkdir -p "$ADEPS/wwise/versions/2017.2.7_a"
	cd "$ADEPS/wwise/versions/2017.2.7_a"
	wget -q --show-progress https://github.com/os-vector/wire-os-externals/releases/download/4.0.0-r05/wwise-2017.2.7_a.tar.gz
	tar -zxf wwise-2017.2.7_a.tar.gz
	rm -f wwise-2017.2.7_a.tar.gz
fi

cd "$DIR"

if [[ "$(uname -a)" == *"Darwin"* ]]; then
	./project/victor/scripts/victor_build_release.sh "$@"
else
	mkdir -p build/cache
	if [[ ! -z $(docker images -q vic-standalone-builder-2) ]]; then
		echo "Purging old vic-standalone-builder-2 container..."
		docker ps -a --filter "ancestor=vic-standalone-builder-2" -q | xargs -r docker rm -f
		echo
		echo -e "\033[5m\033[1m\033[31mOld Docker builder detected on system. If you have built victor or wire-os many times, it is recommended you run:\033[0m"
		echo
		echo -e "\033[1m\033[36mdocker system prune -a --volumes\033[0m"
		echo
		echo -e "\033[32mContinuing in 5 seconds... (you will only see this message once)\033[0m"
		sleep 5
	fi
	if [[ -z $(docker images -q vic-standalone-builder-4) ]]; then
		docker build \
		--build-arg DIR_PATH="$(pwd)" \
		--build-arg USER_NAME=$USER \
		--build-arg UID=$(id -u $USER) \
		--build-arg GID=$(id -u $USER) \
		-t vic-standalone-builder-4 \
		build/
	else
		echo "Reusing vic-standalone-builder-3"
	fi
	docker run -it \
		-v $(pwd)/anki-deps:/home/$USER/.anki \
		-v $(pwd):$(pwd) \
		-v $(pwd)/build/cache:/home/$USER/.ccache \
		vic-standalone-builder-4 bash -c \
		"cd $(pwd) && \
		./project/victor/scripts/victor_build_release.sh $@"
fi

echo "Copying vic-cloud and vic-gateway..."
cp -a bin/* _build/vicos/Release/bin/
echo "Copying libopus..."
cp -a 3rd/opus/vicos/lib/libopus.so.0.7.0 _build/vicos/Release/lib/libopus.so.0
#cp -a patch-libs/* _build/vicos/Release/lib/

echo
echo "Complete."
echo
