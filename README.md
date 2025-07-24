# 1.6-rebuild (DO NOT USE)

## Branch info
THIS BRANCH WAS NOT 1.6, IT WAS 1.7. IF YOU WANT TO COMPILE 1.6 PLEASE USE THIS REPO:
https://github.com/Switch-modder/victor-1.6-rebuild

## Building (linux)

1. Clone the repo and cd into it:

```
cd ~
git clone --recurse-submodules https://github.com/The-Viccyware-Group/Viccyware -b 1.6-rebuild
cd Viccyware
```

2. Make sure you can run Docker as a normal user. This will probably involve:

```
sudo groupadd docker
sudo gpasswd -a $USER docker
newgrp docker
sudo chown root:docker /var/run/docker.sock
sudo chmod 660 /var/run/docker.sock
```

3. Run the build script:
```
cd ~/Viccyware
./wire/build-d.sh
```

3. It should just work! The output will be in `./_build/vicos/Release/`

## Deploying

1. Echo your robot's IP address to robot_ip.txt (in the root of the victor repo):

```
echo 192.168.1.150 > robot_ip.txt
```

2. Copy your bot's SSH key to a file called `robot_sshkey` in the root of this repo.

3. Run:

```
./build/deploy-v.sh
```
