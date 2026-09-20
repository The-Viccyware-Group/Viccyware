# Viccyware-tester

Welcome to the unstable branch of `Viccyware`. This is the home of a modified copy of the Vector source
code. Original README: [README-orig.md](/README-orig.md)

Learn a little more about the project at [vicw.froggitti.net](https://vicw.froggitti.net/)

## Branch info
This branch of the Vector source code will attempt to reimpliment Cozmo from old versions of Cozmoware into the modern os. Unlike the main branch this branch will be updated more frequently compared to the stable branch. This can mean the code may be more buggy so if you need reliability switch to the main branch please.

## Changes

- The wiki includes a list of changes that were made by a fellow community member, Wire: [Changes Wire Made](https://github.com/kercre123/victor/wiki/Changes-I-Made)

## Building

`Viccyware` can be built standalone on most Linux distros (arm64 or amd64), and on macOS (arm64 only, for now).

For Linux, the Docker method is recommended for now (especially if you have a weird or old Linux distro installed), though bare metal works nicely too.

Note that if you have built in Docker before and want to build on bare metal now (or vice-versa), you should do a [clean](#cleaning) build.

Click an option below for instructions.

<details>
<summary><strong>Docker: x86_64 or arm64 Linux</strong></summary>
<br />

- Prerequisites: Make sure you have `docker` and `git` installed.

1. Clone the repo and `cd` into it:

```
cd ~
git clone --recurse-submodules https://github.com/The-Viccyware-Group/Viccyware -b Viccyware-tester
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
./build/build-v.sh
```

</details>

<details>
<summary><strong>Bare Metal: x86_64 or arm64 Linux</strong></summary>
<br \>

- Prerequisites:
  - glibc 2.35 or above - this means anything Debian Bookworm-era and newer will work.
  - The following packages need to be installed: `git wget curl openssl ninja g++ gcc pkg-config ccache`
```
# Arch Linux:
sudo pacman -S git wget curl openssl ninja gcc pkgconf ccache
# Ubuntu/Debian:
sudo apt-get update && sudo apt-get install -y git wget curl openssl ninja-build gcc g++ pkg-config ccache
# Fedora
sudo dnf install -y git wget curl openssl ninja-build gcc gcc-c++ pkgconf-pkg-config ccache
```

1. Clone the repo and `cd` into it:

```
cd ~
git clone --recurse-submodules https://github.com/The-Viccyware-Group/Viccyware -b Viccyware-tester
cd Viccyware
```

2. Source `setenv.sh`:
```
source setenv.sh
```

3. (OPTIONAL) Run this so you don't have to perform step 2 every time:
```
echo "source \"$(pwd)/setenv.sh\"" >> $HOME/.bashrc
```

4. Build:
```
vbuild
```

</details>

<details>

<summary><strong>macOS (M-series only)</strong></summary>
<br />

- Prereqs: Make sure you have [brew](https://brew.sh/) installed.
  -  Then: `brew install ccache gcc ninja pkg-config upx wget`

1. Clone the repo and cd into it:

```
cd ~
git clone --recurse-submodules https://github.com/The-Viccyware-Group/Viccyware -b Viccyware-tester
cd Viccyware
```

2. Run the build script:
```
cd ~/Viccyware
./build/build-v.sh
```

</details>

## Deploying

1. Install Viccyware on your robot.
2. Get your robot's IP through CCIS:
  - 1. Place your robot on the charger
  - 2. Double click the button
  - 3. Lift the lift up then down
  - 4. Write down the IP address somewhere
  - 5. Lift the lift up then down again to exit CCIS
3. One of the following:

<details>
<summary><strong>(Docker: x86_64 or arm64 Linux) or (macOS M-series)</strong></summary>
<br \>

- Run:

```
./build/deploy-v.sh
```
</details>

<details>
<summary><strong>Bare Metal: x86_64 or arm64 Linux</strong></summary>
<br \>

- Run:

```
vdeploy
```
</details>

## Simulator

`wire-os-victor` can be run in WeBots. A mostly fully-featured Vector experience without needing a real robot to deploy to.

This only works with a relatively beefy x86_64 Linux machine.

To do this: 

1. Follow the prereq instructions in "Bare Metal: x86_64 or arm64 Linux" in the above section

2. Install qemu-static-arm and libasound2-dev, make sure systemd-binfmt is set up to handle ARM programs

```
# Arch:
sudo pacman -S qemu-user-static qemu-user-static-binfmt alsa-lib opus
sudo systemctl enable --now systemd-binfmt.service
# if already enabled
sudo systemctl restart systemd-binfmt.service

# Debian / Ubuntu:
sudo apt install qemu-user-static qemu-user-binfmt libasound2-dev libopus-dev
sudo systemctl restart systemd-binfmt.service

# Fedora
sudo dnf install qemu-user-static qemu-user-binfmt alsa-lib opus
sudo systemctl restart systemd-binfmt.service
```

3. Run:

```
./sim.sh
```

That will eventually open up `mprocs`, a program for managing multiple processes. Press `q` in that terminal window to exit, and click "Close without saving" in WeBots if asked.

## Cozmo

You can run the victor processes on your computer and use Cozmo as the vessel.

If you have a beefy x86_64 Linux machine with a Wi-Fi card, go for it!

Follow the simulator instructions, but instead of `./sim.sh`, run:

```
./sim.sh -cozmo
```

Make sure you're connected to the internet while the script is running, as it downloads dependencies.

If you have more than one network adapter, you can have one connected to the internet and one connected to Cozmo at the same time throughout the whole process.

If you only have one, you can just run the script while connected to the internet, and it will tell you when you should change your network over to Cozmo. Also note that if you have run it once, you don't need the internet to run it a second time.

## Cleaning

99% of the time, if you're working on a behavior or something, you don't need to clean any build directories. The CMakeLists are correctly setup to properly rebuild the code which needs to be rebuilt upon a file change.

If you do want to clean anyway:

<details>
<summary><strong>(Docker: x86_64 or arm64 Linux) or (macOS M-series)</strong></summary>
<br \>

- Run:

```
./build/clean.sh
```
</details>

<details>
<summary><strong>Bare Metal: x86_64 or arm64 Linux</strong></summary>
<br \>

- Run:

```
vclean
```
</details>

## VSCode Code Completion

- After you build for the first time, two files will be generated and placed in the root of the source directory:
  - `compile_commands.json`
  - `.clangd`
- If you install the [`clangd`](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd) extension for VSCode then relaunch VSCode after a build, it will index the code and you will have speedy code completion, error underlining+explanations, function descriptions, and such for the entire codebase.
