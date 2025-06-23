#!/usr/bin/env bash

if [[ ! -d oldpork ]]; then
	wget https://archive.org/download/github.com-Picovoice-Porcupine_-_2018-10-07_02-31-11/Picovoice-Porcupine_-_2018-10-07_02-31-11.bundle
	git clone Picovoice-Porcupine_-_2018-10-07_02-31-11.bundle oldpork
	rm Picovoice-Porcupine_-_2018-10-07_02-31-11.bundle
fi

./pv_porcupine_optimizer -r $(pwd)/oldpork/resources -o $(pwd) -p linux -w "$1"
