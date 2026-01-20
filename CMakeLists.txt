cmake_minimum_required(VERSION 3.10)
project(miniwolf C)
set(CMAKE_C_STANDARD 11)

# Option to enable debug builds
option(MINIWOLF_DEBUG "Enable debugging symbols and disable optimizations" OFF)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
find_package(ALSA REQUIRED)

include_directories(${ALSA_INCLUDE_DIRS} include)

# Set compiler flags based on debug option or build type
if(MINIWOLF_DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DEBUG_FLAGS "-pg -O0 -DDEBUG")
    set(RELEASE_FLAGS "")
    set(RELEASE_LINK_FLAGS "")
else()
    set(DEBUG_FLAGS "")
    set(RELEASE_FLAGS "-O3 -DNDEBUG -flto")
    set(RELEASE_LINK_FLAGS "-Wl,--gc-sections")
endif()

# Apply flags to all targets
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${RELEASE_LINK_FLAGS}")

# AX25 Library: Protocol stack modules
set(AX25_SOURCES
    src/buffer.c
    src/ax25.c
    src/kiss.c
    src/tnc2.c
    src/hldc.c
    src/crc.c
)
add_library(ax25 STATIC ${AX25_SOURCES})
target_include_directories(ax25 PUBLIC include)

# DSP Library: Signal processing modules
set(DSP_SOURCES
    src/agc.c
    src/fft.c
    src/filter.c
    src/goertzel.c
    src/mavg.c
    src/synth.c
)
add_library(dsp STATIC ${DSP_SOURCES})
target_include_directories(dsp PUBLIC include)

# Core Library: Shared infrastructure modules
set(CORE_SOURCES
    src/buffer.c
    src/line.c
    src/common.c
    src/ring.c
    src/tcp.c
    src/udp.c
    src/conf.c
)
add_library(mw_core STATIC ${CORE_SOURCES})
target_include_directories(mw_core PUBLIC include)

# Modem Library: High level signal processing and frame (de)modulation
set(MODEM_SOURCES
    src/bitclk.c
    src/demod.c
    src/demod_goertzel.c
    src/demod_quad.c
    src/demod_rrc.c
    src/demod_split.c
    src/mod.c
    src/modem.c
    src/squelch.c
    src/dedupe.c
)
add_library(mw_modem STATIC ${MODEM_SOURCES})
target_include_directories(mw_modem PUBLIC include)
target_link_libraries(mw_modem mw_core)

# miniwolf
add_executable(miniwolf src/main.c src/audio.c src/miniwolf.c src/loop.c src/options.c src/options_args.c src/options_file.c)
target_link_libraries(miniwolf mw_core mw_modem ax25 dsp ${ALSA_LIBRARIES} m)

# mw_test: unit tests
add_executable(mw_test test/main_test.c test/test.c)
target_link_libraries(mw_test mw_core mw_modem ax25 dsp m)

# mw_bench: recording/file demodulation tool
add_executable(mw_bench src/main_bench.c)
target_link_libraries(mw_bench mw_core mw_modem ax25 dsp m)

# mw_cal: AF spectrum analyzer
add_executable(mw_cal src/main_cal.c src/audio.c)
target_link_libraries(mw_cal mw_core dsp ${ALSA_LIBRARIES} m)

# mw_log: RF traffic logger
add_executable(mw_log src/main_log.c src/dedupe.c)
target_link_libraries(mw_log mw_core ax25)

# Strip symbols in Release builds
if(NOT MINIWOLF_DEBUG AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET miniwolf POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:miniwolf>)
    add_custom_command(TARGET mw_test POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_test>)
    add_custom_command(TARGET mw_bench POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_bench>)
    add_custom_command(TARGET mw_cal POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_cal>)
    add_custom_command(TARGET mw_log POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_log>)
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS miniwolf mw_cal mw_log
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install systemd service files
file(GLOB SYSTEMD_SERVICE_FILES "systemd/*.service")
install(FILES ${SYSTEMD_SERVICE_FILES}
    DESTINATION /etc/systemd/system
)

# Reload systemd daemon after installation
install(CODE "execute_process(COMMAND systemctl daemon-reload)")
