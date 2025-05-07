# Viccyware-tester

Welcome to the unstable branch of `Viccyware`. This is the home of a modified copy of the Vector source
code. Original README: [README-orig.md](/README-orig.md)

Check the [wiki](https://github.com/kercre123/victor/wiki) for more information about the leak, what we can do with this, and general Vector info.

## Branch info
This branch of the Vector source code will attempt to reimpliment Cozmo from old versions of Cozmoware into the modern os. Unlike the main branch this branch will be updated more frequently compared to the stable branch. This can mean the code may be more buggy so if you need reliability switch to the main branch please.

## Changes

- The wiki includes a list of changes that were made by a fellow community member, Wire: [Changes Wire Made](https://github.com/kercre123/victor/wiki/Changes-I-Made)

## Building (Linux or WSL)

 - Prereqs: Make sure you have `docker` and `git-lfs` installed.

1. Clone the repo and cd into it:

```
cd ~
git clone --recurse-submodules https://github.com/Switch-modder/Viccyware -b Viccyware-tester
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
./wire/build-v.sh
```

3. It should just work! The output will be in `./_build/vicos/Release/`

## Building (ARM64 macOS)

# only works on M1-M4 Macs at the moment, not Intel

 - Prereqs: Make sure you have [brew](https://brew.sh/) installed.
   -  Then: `brew install pyenv git-lfs ccache wget`

1. Clone the repo and cd into it:

```
cd ~
git clone --recurse-submodules https://github.com/Switch-modder/Viccyware -b Viccyware-tester
cd Viccyware
git lfs install
git lfs pull
```

2. Set up Python 2:

```
pyenv install 2.7.18
pyenv init
```

- Add the following to both ~/.zshrc and ~/.zprofile. After doing so, run the commands in your terminal session:
```
export PYENV_ROOT="$HOME/.pyenv"
[[ -d $PYENV_ROOT/bin ]] && export PATH="$PYENV_ROOT/bin:$PATH"
eval "$(pyenv init -)"
pyenv shell 2.7.18
```

3. Disable security:

```
sudo spctl --master-disable
sudo spctl --global-disable
```
- You will have to head to `System Settings -> Security & Privacy -> Allow applications from` and select "Anywhere".


4. Run the build script:
```
cd ~/Viccyware
./build/build-v.sh
```

5. It should just work! The output will be in `./_build/vicos/Release/`

## Deploying

1. Echo your robot's IP address to robot_ip.txt (in the root of the victor repo):

```
echo 192.168.1.150 > robot_ip.txt
```

2. Copy your bot's SSH key to a file called `robot_sshkey` in the root of this repo.

3. Run:

```
./build/deploy-d.sh
```
