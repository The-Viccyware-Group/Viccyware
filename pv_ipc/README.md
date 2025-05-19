# Picovoice Server + Interface

- The Cortex-A7 lib for Porcupine (our wake-word engine) is hardfp-only.
- This cannot work normally in our softfp environment.
- I am getting around this by statically compiling Porcupine into a server program which vic-anim will comm with over IPC.
- You can build this with ./vic-build.sh on an x86_64 Linux machine, though you shouldn't ever really need to.
