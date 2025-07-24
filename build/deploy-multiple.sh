#!/usr/bin/env bash

clear

echo Staging /anki files...
./project/victor/scripts/stage.sh

clear

if [ ! -f "_build/staging/Release/anki/bin/vic-anim" ]; then
echo Build victor first, then run this script.
exit 0
fi

read -p "How many robots are you deploying to? (max is 3): " robocount
export ROBOT_COUNT="$robocount"

if [ "$ROBOT_COUNT" == "1" ]; then
    if [ ! -f "robot_ip.txt" ]; then
        read -p "Enter robot IP: " robotip
        export ROBOT_IP="$robotip"
        echo "$ROBOT_IP" >> robot_ip.txt
    else
        export ROBOT_IP=$(cat robot_ip.txt)
    fi
elif [ "$ROBOT_COUNT" == "2" ]; then
        if [ ! -f "robot_ip.txt" ]; then
        read -p "Enter robot 1's IP: " robotip
        export ROBOT_1_IP="$robotip"
        echo "$ROBOT_IP" >> robot_ip.txt
    else
        export ROBOT_IP=$(cat robot_ip.txt)
    fi
        if [ ! -f "robot_ip_2.txt" ]; then
        read -p "Enter robot 2's IP: " robot2ip
        export ROBOT_2_IP="$robot2ip"
        echo "$ROBOT_2_IP" >> robot_ip_2.txt
    else
        export ROBOT_2_IP=$(cat robot_ip_2.txt)
    fi
elif [ "$ROBOT_COUNT" == "3" ]; then
            if [ ! -f "robot_ip.txt" ]; then
        read -p "Enter robot 1's IP: " robotip
        export ROBOT_1_IP="$robotip"
        echo $ROBOT_1_IP >> robot_ip.txt
    else
        export ROBOT_IP=$(cat robot_ip.txt)
    fi
        if [ ! -f "robot_ip_2.txt" ]; then
        read -p "Enter robot 2's IP: " robot2ip
        export ROBOT_2_IP="$robot2ip"
        echo $ROBOT_2_IP >> robot_ip_2.txt
    else
        export ROBOT_2_IP=$(cat robot_ip_2.txt)
    fi
            if [ ! -f "robot_ip_3.txt" ]; then
        read -p "Enter robot 3's IP: " robot3ip
        export ROBOT_3_IP="$robot3ip"
        echo $ROBOT_3_IP >> robot_ip_3.txt
    else
        export ROBOT_3_IP=$(cat robot_ip_3.txt)
    fi
else
echo Not a valid number. Exiting.
exit 0
fi

eval `ssh-agent`
chmod 600 robot_sshkey

echo Staging /anki files...
./project/victor/scripts/stage.sh

if [ "$ROBOT_COUNT" == "1" ]; then
    echo Deploying to "$ROBOT_IP"
    ssh -i robot_sshkey root@"$ROBOT_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_IP":/
    echo Deployment seems to have been successful, starting anki-robot
    ssh -i robot_sshkey root@"$ROBOT_IP" 'systemctl start anki-robot.target'
elif [ "$ROBOT_COUNT" == 2 ]; then
    echo Deploying to "$ROBOT_IP" first, then deploying to "$ROBOT_2_IP"
    ssh -i robot_sshkey root@"$ROBOT_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_IP":/
    ssh -i robot_sshkey root@"$ROBOT_2_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_2_IP":/
    echo Deployment seems to have been successful, starting anki-robot
    ssh -i robot_sshkey root@"$ROBOT_IP" 'systemctl start anki-robot.target'
    ssh -i robot_sshkey root@"$ROBOT_2_IP" 'systemctl start anki-robot.target'
elif [ "$ROBOT_COUNT" == "3" ]; then
    echo Deploying to "$ROBOT_IP" first, then deploying to "$ROBOT_2_IP", then deploying to "$ROBOT_3_IP"
    ssh -i robot_sshkey root@"$ROBOT_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_IP":/
    ssh -i robot_sshkey root@"$ROBOT_2_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_2_IP":/
    ssh -i robot_sshkey root@"$ROBOT_3_IP" 'mount -o rw,remount / && systemctl stop anki-robot.target && rm -rf /anki'
    rsync -e 'ssh -i robot_sshkey' -avr _build/staging/Release/anki root@"$ROBOT_3_IP":/
    echo Deployment seems to have been successful, starting anki-robot
    ssh -i robot_sshkey root@"$ROBOT_IP" 'systemctl start anki-robot.target'
    ssh -i robot_sshkey root@"$ROBOT_2_IP" 'systemctl start anki-robot.target'
    ssh -i robot_sshkey root@"$ROBOT_3_IP" 'systemctl start anki-robot.target'
fi

exit 0
