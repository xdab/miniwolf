.PHONY: all build release run cal log test prof bench1 bench2 benchdz install clean

all: run

build:
	mkdir -p build
	cd build && cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" ..
	cd build && make

release:
	mkdir -p build
	cd build && cmake -G "Unix Makefiles" ..
	cd build && make

run: build
	./build/miniwolf -v -io -d plughw:CARD=Generic,DEV=0 -r 44100 --tcp-tnc2 8144 --udp-tnc2-addr 127.0.0.1 --udp-tnc2-port 18144 --udp-tnc2-listen 28144

cal: build
	./build/mw_cal -v -d plughw:CARD=Generic,DEV=0 -r 44100

log: build
	./build/mw_log -v --udp-port 18144

test: build
	./build/mw_test

prof: build
	valgrind --tool=callgrind ./build/mw_bench -v -F S16 -r 22050 -f tnctest01_22050_S16.raw

bench1: build
	./build/mw_bench -F S16 -r 22050 -f tnctest01_22050_S16.raw

bench2: build
	./build/mw_bench -F S16 -r 22050 -f tnctest02_22050_S16.raw -2 5.0

benchdz: build
	./build/mw_bench -F S16 -r 22050 -f tnctest_sr5dz.raw -2 3.0

install: release
	sudo make -C build install

clean:
	rm -rf build
