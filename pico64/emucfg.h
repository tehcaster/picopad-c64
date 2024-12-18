#ifndef EMUCFG_H
#define EMUCFG_H

#define PALETTE_SIZE         256
#define TFT_VBUFFER_YCROP    0
#define SINGLELINE_RENDERING 1
#define CUSTOM_SND           1
//#define TIMER_REND           1
#define EXTRA_HEAP           0x10
#define FILEBROWSER

// Title:     <                        >
#define TITLE "    C64 Emulator "
#define ROMSDIR "C64"

#define emu_Init() { c64_Init(); }
#define emu_Step(x) { c64_Step(); }
#define emu_Input() { c64_Input(); }

#endif
