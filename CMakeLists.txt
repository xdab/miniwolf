cmake_minimum_required(VERSION 3.10)
project(miniwolf C)
set(CMAKE_C_STANDARD 11)

# Option to enable debug builds
option(MINIWOLF_DEBUG "Enable debugging symbols and disable optimizations" OFF)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
find_package(ALSA REQUIRED)

include_directories(${ALSA_INCLUDE_DIRS} include libs/libtnc/include)

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

# Submodules
add_subdirectory(libs/libtnc)
add_subdirectory(libs/libcomm)

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

# Modem Library: High level signal processing and frame (de)modulation
set(MODEM_SOURCES
    src/bitclk.c
    src/demod.c
    src/demod_goertzel.c
    src/demod_quad.c
    src/mod.c
    src/modem.c
    src/squelch.c
)
add_library(mw_modem STATIC ${MODEM_SOURCES})
target_include_directories(mw_modem PUBLIC include)
target_link_libraries(mw_modem tnc)

# miniwolf
add_executable(miniwolf src/main.c src/audio.c src/miniwolf.c src/loop.c src/options.c src/options_args.c src/options_file.c src/ring.c)
target_link_libraries(miniwolf mw_modem comm tnc dsp ${ALSA_LIBRARIES} m)

# mw_test: unit tests
add_executable(mw_test test/main_test.c test/test.c src/ring.c)
target_link_libraries(mw_test mw_modem tnc dsp m)

# mw_bench: recording/file demodulation tool
add_executable(mw_bench src/main_bench.c src/ring.c)
target_link_libraries(mw_bench mw_modem tnc dsp m)

# mw_cal: AF spectrum analyzer
add_executable(mw_cal src/main_cal.c src/audio.c src/ring.c)
target_link_libraries(mw_cal tnc dsp ${ALSA_LIBRARIES} m)

# Strip symbols in Release builds
if(NOT MINIWOLF_DEBUG AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET miniwolf POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:miniwolf>)
    add_custom_command(TARGET mw_test POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_test>)
    add_custom_command(TARGET mw_bench POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_bench>)
    add_custom_command(TARGET mw_cal POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:mw_cal>)
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS miniwolf mw_cal
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install systemd service files
file(GLOB SYSTEMD_SERVICE_FILES "systemd/*.service")
install(FILES ${SYSTEMD_SERVICE_FILES}
    DESTINATION /etc/systemd/system
)

# Reload systemd daemon after installation
install(CODE "execute_process(COMMAND systemctl daemon-reload)")
