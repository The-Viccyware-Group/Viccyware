import random
import anki_vector
import time

def main():
    # Give the robot a few seconds to finish booting
    time.sleep(5)
    
    # 1 in 100 chance
    if random.randint(1, 100) == 1:
        try:
            with anki_vector.Robot() as robot:
                robot.audio.stream_wav_file("/home/root/rickroll.wav")
        except Exception:
            # Exit quietly if something goes wrong
            pass

if __name__ == "__main__":
    main()
