
# ASM source files
ASRC +=

# C source files
CSRC +=

# C++ source files
SRC += pico64/c64.cpp
SRC += pico64/cia1.cpp
SRC += pico64/cia2.cpp
SRC += pico64/cpu.cpp
SRC += pico64/patches.cpp
SRC += pico64/pla.cpp
SRC += pico64/roms.cpp
SRC += pico64/sid.cpp
SRC += pico64/timerutil.cpp
SRC += pico64/vic.cpp
SRC += pico64/reSID.cpp
SRC += pico64/pico64.cpp

SRC += display/pico_dsp.cpp
SRC += display/vga.cpp
SRC += display/vga_vmode.cpp
SRC += display/emuapi.cpp
SRC += display/AudioPlaySystem.cpp

# Makefile includes
include ../../../Makefile.inc
