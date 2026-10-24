.PHONY: all update build release run cal log test prof bench bench1 bench2 fmt fmt-check tidy compdb install clean

all: run

help:
	@echo "update        pull repo and submodules"
	@echo "release       optimized build into build/"
	@echo "build         debug build (with -pg -O0 -DDEBUG)"
	@echo "run           debug build, then run against sample.conf"
	@echo "cal           run the spectrum analyzer (-C)"
	@echo "test          debug build, then run build/mw_test"
	@echo "bench         release build, then bench.raw demodulation"
	@echo "fmt / fmt-check  reformat / verify formatting (clang-format)"
	@echo "tidy          static analysis gate (clang-tidy, needs build/)"
	@echo "install       release build, then install binary and systemd units"
	@echo "clean         remove build/"

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

C_SOURCES_ONLY := $(wildcard src/*.c test/*.c)

tidy:
	@command -v clang-tidy >/dev/null 2>&1 || { \
		echo "ERROR: clang-tidy not installed (Arch: sudo pacman -S clang)"; exit 1; }
	clang-tidy -p build $(C_SOURCES_ONLY) --warnings-as-errors=*

compdb:
	cmake -S . -B build -G "Unix Makefiles"

install: release
	sudo make -C build install

clean:
	rm -rf build
