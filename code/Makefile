CC=g++

FLAGS+= -std=c++17
FLAGS+=-F /System/Library/Frameworks/CoreFoundation.framework/ -framework CoreFoundation
FLAGS+=-F /System/Library/Frameworks/CoreAudio.framework/ -framework CoreAudio
FLAGS+=-F /System/Library/Frameworks/AudioToolBox.framework/ -framework AudioToolbox
FLAGS+=-F /System/Library/Frameworks/AudioUnit.framework/ -framework AudioUnit

all:
	$(CC) benchmark.cpp $(FLAGS) -o benchmark
	$(CC) run.cpp -o run
