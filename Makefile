.PHONY: all update build profile release run cal log test prof regression regression-perf file file1 file2 fmt fmt-check tidy compdb package install clean

all: run

help:
	@echo "update        pull repo and submodules"
	@echo "release       optimized build into build/"
	@echo "build         debug build (with -pg -O0 -DDEBUG)"
	@echo "run           debug build, then run against sample.conf"
	@echo "cal           run the spectrum analyzer (-C)"
	@echo "test          debug build, then run build/mw_test"
	@echo "regression    release build, then run the e2e regression suite (tools/regression.py)"
	@echo "regression-perf  the suite plus per-case real-time multiplier"
	@echo "file / file1 / file2  release build, then demodulate tnctest tracks"
	@echo "package       release build, then tar the binaries"
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

profile:
	mkdir -p build/prof
	cd build/prof && cmake -DCMAKE_BUILD_TYPE=Profile -G "Unix Makefiles" ../..
	cd build/prof && make -j miniwolf_file

release:
	mkdir -p build
	cd build && cmake -G "Unix Makefiles" ..
	cd build && make -j

ARCH := $(shell uname -m)
VERSION := $(shell git describe --tags --always --abbrev=8 2>/dev/null || echo "unknown")

package: release
	rm -rf miniwolf_package
	mkdir -p miniwolf_package
	cp build/miniwolf build/mw_cal build/mw_log build/mw_test build/miniwolf_file miniwolf_package/
	tar -czf miniwolf-$(ARCH)-$(VERSION).tar.gz -C miniwolf_package .
	rm -rf miniwolf_package

run: build
	./build/miniwolf -c sample.conf

cal: build
	./build/miniwolf -C -v -d default -r 44100

test: build
	./build/mw_test

regression: release
	python3 tools/regression.py

regression-perf: release
	python3 tools/regression_perf.py

prof: build
	valgrind --tool=callgrind ./build/miniwolf_file -v -F S16 -r 22050 -f data/tnctest01_22050Hz_mono_S16LE.raw

file: release
	./build/miniwolf_file -F S16 -r 22050 -f data/tnctest01_22050Hz_mono_S16LE.raw

file1: release
	./build/miniwolf_file -F S16 -r 22050 -f data/tnctest01_22050Hz_mono_S16LE.raw

file2: release
	./build/miniwolf_file -F S16 -r 22050 -f data/tnctest02_22050Hz_mono_S16LE.raw -2 5.0

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
