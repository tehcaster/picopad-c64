//#include "pico.h"
//#include "pico/stdlib.h"

#include "../include.h"

extern "C" {
  #include "../config/iopins.h"  
  #include "../display/emuapi.h"
}
#include "../display/osd.h"
#include "keyboard_osd.h"
#include "c64.h"

//#include <stdio.h>
#include "../display/pico_dsp.h"

volatile bool vbl=true;

bool repeating_timer_callback(struct repeating_timer *t) {
    if (osd_active)
	    return true;
    uint16_t bClick = emu_DebounceLocalKeys();
    emu_Input(bClick);     
    if (vbl) {
        vbl = false;
    } else {
        vbl = true;
    }   
    return true;
}

PICO_DSP tft;

//#include "hardware/clocks.h"
//#include "hardware/vreg.h"

static u32 fpsLast;
static u32 nFramesLast;
static u32 nFramesC64Last;
static u32 timeSWISRLast;
char fpsBuf[16];

int main(void) {
//	DrawPrintStart();
	UartPrintStart();
	DrawPrintStop();
//    vreg_set_voltage(VREG_VOLTAGE_1_05);
//    set_sys_clock_khz(125000, true);    
//    set_sys_clock_khz(150000, true);    
//    set_sys_clock_khz(133000, true);    
//    set_sys_clock_khz(200000, true);    
//    set_sys_clock_khz(210000, true);    
//    set_sys_clock_khz(230000, true);    
//    set_sys_clock_khz(225000, true);    
//    set_sys_clock_khz(250000, true);  
    stdio_init_all();

    emu_init();
    FileSelInit("/C64", "Select game", "PRG", &FileSelColBlue);
    if (!FileSel())
	    ResetToBootLoader();
    emu_start();
    emu_Init();
    tft.startRefresh();
    struct repeating_timer timer;
    add_repeating_timer_ms(25, repeating_timer_callback, NULL, &timer);    

    printf("display baud: %u want %u\n", SPI_GetBaudrate(DISP_SPI), DISP_SPI_BAUD);
    printf("peri clock: %u\n", CurrentFreq[CLK_PERI]);
    fpsLast = Time();
    nFramesLast = nFrames;
    nFramesC64Last = nFramesC64;
    SelFont8x8();
    while (true) {
        //uint16_t bClick = emu_DebounceLocalKeys();
        //emu_Input(bClick);  
        emu_Step();

	if (osd_active) {
		audio_paused = true;
		osd_start();
		osd_active = false;
		audio_paused = false;
		fpsLast = Time();
	}

	if (Time() - fpsLast > 1000000) {
		fpsLast = Time();
		printf("display FPS: %u\n", nFrames - nFramesLast);
		snprintf(fpsBuf, sizeof(fpsBuf), "LCD FPS: %3d", nFrames - nFramesLast);
		DrawTextBg(fpsBuf, 0, 0, COL_GRAY, COL_BLACK);
		nFramesLast = nFrames;
		printf("c64 FPS: %u\n", nFramesC64 - nFramesC64Last);
		snprintf(fpsBuf, sizeof(fpsBuf), "C64 FPS: %3d", nFramesC64 - nFramesC64Last);
		DrawTextBg(fpsBuf, 320-12*8, 0, COL_GRAY, COL_BLACK);
		nFramesC64Last = nFramesC64;
		printf("SWISR: %u ms\n", (timeSWISR - timeSWISRLast) / 1024);
		timeSWISRLast = timeSWISR;
	}
    }
}

void emu_DrawLine16(unsigned short * VBuf, int width, int height, int line)
{
    tft.writeLine(width,height,line, VBuf);
}

#ifdef HAS_SND

void emu_sndInit() {
  tft.begin_audio();
}

#endif

