.PHONY: all update build release run cal log test prof bench bench1 bench2 fmt fmt-check install clean

all: run

update:
	git pull
	git submodule update --init --recursive

build:
	mkdir -p build
	cd build && cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" ..
	cd build && make -j

release:
	mkdir -p build
	cd build && cmake -G "Unix Makefiles" ..
	cd build && make -j

run: build
	./build/miniwolf -c sample.conf

cal: build
	./build/miniwolf -C -v -d default -r 44100

test: build
	./build/mw_test

prof: build
	valgrind --tool=callgrind ./build/mw_bench -v -F S16 -r 22050 -f tnctest01_22050_S16.raw

bench: release
	./build/mw_bench -F F32 -r 22050 -f bench.raw -2 3.0 

bench1: release
	./build/mw_bench -F S16 -r 22050 -f tnctest01_22050_S16.raw

bench2: release
	./build/mw_bench -F S16 -r 22050 -f tnctest02_22050_S16.raw -2 5.0

C_SOURCES := $(wildcard src/*.c include/*.h test/*.c test/*.h)

fmt:
	@command -v clang-format >/dev/null 2>&1 || { \
		echo "ERROR: clang-format not installed (Arch: sudo pacman -S clang)"; exit 1; }
	clang-format -i $(C_SOURCES)

fmt-check:
	@command -v clang-format >/dev/null 2>&1 || { \
		echo "ERROR: clang-format not installed (Arch: sudo pacman -S clang)"; exit 1; }
	clang-format --dry-run --Werror $(C_SOURCES)

install: release
	sudo make -C build install

clean:
	rm -rf build
