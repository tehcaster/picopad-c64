
#include "../include.h"
#include "../display/osd.h"
#include "c64.h"
#include "../display/pico_dsp.h"
#include <string.h>

PICO_DSP tft;

static u32 fpsLast;
static u32 nFramesLast;
static u32 nFramesC64Last;
static u32 timeSWISRLast;
static u32 nFramesC64NextInput;

#define CONF_GLOB_PATH	"/C64/_GLOBAL.CFG"

static void config_global_load()
{
	char buf[1024];
	int read;
	sFile file;
	char *name;

	DiskAutoMount();
	if (!FileOpen(&file, CONF_GLOB_PATH)) {
		printf("could not open global config file\n");
		goto out;
	}

	read = FileRead(&file, buf, 1024);
	buf[read] = 0;
	name = strtok(buf, "=");
	while (name) {
		char *value = strtok(NULL, "\n");
		if (!value) {
			printf("global config option '%s' has no value\n", name);
			goto out;
		}
		printf("global config read name '%s' value '%s'\n", name, value);

		if (!strcmp(name, "autorun")) {
			config.autorun = *value == '1' ? 1 : 0;
		} else {
			printf("global config unknown name '%s'\n", name);
		}

		name = strtok(NULL, "=");
	}

out:
	FileClose(&file);
	DiskFlush();
	DiskUnmount();
}

void config_global_save()
{
	sFile file;
	DiskAutoMount();

	if (FileExist(CONF_GLOB_PATH)) {
		printf("global config exists, will overwrite\n");
		if (!FileOpen(&file, CONF_GLOB_PATH)) {
			printf("failed to open existing global config\n");
			goto out;
		}
		if (!SetFileSize(&file, 0)) {
			printf("failed to truncate existing global config\n");
			goto out;
		}
	} else {
		printf("creating new global config\n");
		if (!FileCreate(&file, CONF_GLOB_PATH)) {
			printf("failed to create new global config\n");
			goto out;
		}
	}

	FilePrint(&file, "autorun=%d\n", config.autorun ? 1 : 0);

out_close:
	if (!FileClose(&file))
		printf("failure closing global config file\n");

out:
	DiskFlush();
	DiskUnmount();
}

int main(void) {
//	DrawPrintStart();
	UartPrintStart();
	DrawPrintStop();
//    vreg_set_voltage(VREG_VOLTAGE_1_05);
//    set_sys_clock_khz(250000, true);  
    stdio_init_all();
    config_global_load();

    tft.begin();
    FileSelInit("/C64", "Select game", "PRG", &FileSelColBlue);
    if (!FileSel())
	    ResetToBootLoader();
    c64_Init();
    tft.begin_audio();
    SelFont8x8();
    tft.startRefresh();

    printf("display baud: %u want %u\n", SPI_GetBaudrate(DISP_SPI), DISP_SPI_BAUD);
    printf("peri clock: %u\n", CurrentFreq[CLK_PERI]);

    fpsLast = Time();
    nFramesLast = nFrames;
    nFramesC64Last = nFramesC64;
    nFramesC64NextInput = nFramesC64 + 100;

    while (true) {
	if (nFramesC64 == nFramesC64NextInput) {
		nFramesC64NextInput = nFramesC64 + 1;
		c64_Input();
	}

	if (osd_active) {
		audio_paused = true;
		osd_start();
		osd_active = false;
		audio_paused = false;

		fpsLast = Time();
		nFramesLast = nFrames;
		nFramesC64Last = nFramesC64;
	}

	c64_Step();

	if (Time() - fpsLast > 1000000) {
		char fpsBuf[16];
		fpsLast = Time();
//		printf("display FPS: %u\n", nFrames - nFramesLast);
		snprintf(fpsBuf, sizeof(fpsBuf), "LCD FPS: %3d", nFrames - nFramesLast);
		DrawTextBg(fpsBuf, 0, 0, COL_GRAY, COL_BLACK);
		nFramesLast = nFrames;
//		printf("c64 FPS: %u\n", nFramesC64 - nFramesC64Last);
		snprintf(fpsBuf, sizeof(fpsBuf), "C64 FPS: %3d", nFramesC64 - nFramesC64Last);
		DrawTextBg(fpsBuf, 320-12*8, 0, COL_GRAY, COL_BLACK);
		nFramesC64Last = nFramesC64;
//		printf("SWISR: %u ms\n", (timeSWISR - timeSWISRLast) / 1024);
		timeSWISRLast = timeSWISR;
	}
    }
}

void stoprefresh(void)
{
	tft.stopRefresh();
}


