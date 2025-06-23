# Model Creation

*shhhhh*

`./create-model.sh "wake word"`

- description of what that script does:

1. [downloads this bundle](https://archive.org/download/github.com-Picovoice-Porcupine_-_2018-10-07_02-31-11/Picovoice-Porcupine_-_2018-10-07_02-31-11.bundle)
2. `git clone Picovoice-Porcupine_-_2018-10-07_02-31-11.bundle oldpork`
3. `./pv_porcupine_optimizer -r $(pwd)/oldpork/resources -o $(pwd) -p linux -w "wake word"`

-# the secret sauce is that pv_porcupine_optimizer has been modified to create a model for raspberrypi (lib i am using in Vector)

