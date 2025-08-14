
#
# Use android.py to locate Android NDK
#
#TOPLEVEL := ../../../..
#ANDROID_PY := $(TOPLEVEL)/tools/build/tools/ankibuild/android.py
#ANDROID_NDK_ROOT := $(shell $(ANDROID_PY))

#ANDROID_ARM_TOOLS_ROOT := $(TOPLEVEL)/generated/android/tools/arm
#ANDROID_ARM_TOOLS_BIN := $(ANDROID_ARM_TOOLS_ROOT)/bin

# Compilation Tools
#CC := $(ANDROID_ARM_TOOLS_BIN)/arm-linux-androideabi-g++
#CPP := $(ANDROID_ARM_TOOLS_BIN)/arm-linux-androideabi-g++
#AR := $(ANDROID_ARM_TOOLS_BIN)/arm-linux-androideabi-ar
CC := gcc
CPP := g++
AR := ar



# Compiler flags
# Specify all the flags below which you want to use for your compilation, For this simple example, we only need to specify the include directory path
CFLAGS          := -I.
CFLAGS          += -I./inc/

DEPS = inc/* 
SRCS = makebc.c
OBJ = makebc.o
APP = makebc

all: $(APP)

#$(ANDROID_ARM_TOOLS_ROOT):
#	$(ANDROID_NDK_ROOT)/build/tools/make_standalone_toolchain.py --arch arm --install-dir $(ANDROID_ARM_TOOLS_ROOT) --api 24

$(APP):  $(OBJ)
	$(CC) -pie $(OBJ) -o $(APP)

clean:
	rm -f $(APP) *.o src/*.o
