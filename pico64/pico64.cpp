
#include "../include.h"
#include "../display/osd.h"
#include "c64.h"
#include "keyboard.h"
#include "../display/pico_dsp.h"
#include <string.h>

PICO_DSP tft;

static u32 fpsLast;
static u32 nFramesLast;
static u32 nFramesC64Last;
static u32 timeSWISRLast;
static u32 nFramesC64NextInput;

#define CONF_GLOB_PATH	"/C64/_GLOBAL.CFG"
#define LAST_GAME_PATH	"/C64/_LAST.CFG"
#define LAST_GAME_VER	100

static bool FileRecreate(sFile *file, const char *path)
{
	if (FileExist(path)) {
		if (!FileOpen(file, path)) {
			printf("FileRecreate() failed to open %s\n", path);
			return false;
		}
		if (!SetFileSize(file, 0)) {
			printf("FileRecreate() failed to truncate %s\n", path);
			FileClose(file);
			return false;
		}
	} else {
		if (!FileCreate(file, path)) {
			printf("FileRecreate() failed to create %s\n", path);
			return false;
		}
	}
	return true;
}

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
		} else if (!strcmp(name, "show_fps")) {
			config.show_fps = *value == '1' ? 1 : 0;
		} else if (!strcmp(name, "show_keys")) {
			config.show_keys = *value == '1' ? 1 : 0;
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

	if (!FileRecreate(&file, CONF_GLOB_PATH)) {
		printf("failed to recreate global config\n");
		goto out;
	}

	FilePrint(&file, "autorun=%d\n", config.autorun ? 1 : 0);
	FilePrint(&file, "show_fps=%d\n", config.show_fps ? 1 : 0);
	FilePrint(&file, "show_keys=%d\n", config.show_keys ? 1 : 0);

out_close:
	if (!FileClose(&file))
		printf("failure closing global config file\n");

out:
	DiskFlush();
	DiskUnmount();
}

static void config_load_button(char *val)
{
	char *saveptr;
	u8 idx, mode, key;
	char *p;


	p = strtok_r(val, ",", &saveptr);
	if (!p)
		return;
	idx = atoi(p);
	if (idx >= CONFIG_BTN_MAX)
		return;

	p = strtok_r(NULL, ",", &saveptr);
	if (!p)
		return;
	mode = atoi(p);
	if (mode >= CONFIG_BTN_MODE_MAX)
		return;

	p = strtok_r(NULL, ",", &saveptr);
	if (!p)
		return;
	key = atoi(p);

	struct button_config *btn = &config.buttons[idx];

	btn->mode = mode;
	if (mode == CONFIG_BTN_MODE_KEY)
		btn->key = key;
	else if (mode == CONFIG_BTN_MODE_JOY)
		btn->joy = key;
}

static void config_game_load()
{
	char buf[1024];
	int read;
	sFile file;
	char *name;

	snprintf(buf, sizeof(buf), "%s.%s", FileSelLastName, "CFG");
	printf("loading per-game config %s\n", buf);

	DiskAutoMount();
	SetDir(FileSelPath);
	if (!FileOpen(&file, buf)) {
		printf("could not open per-game config file\n");
		goto out;
	}

	read = FileRead(&file, buf, 1024);
	buf[read] = 0;
	name = strtok(buf, "=");
	while (name) {
		char *value = strtok(NULL, "\n");
		if (!value) {
			printf("per-game config option '%s' has no value\n", name);
			goto out;
		}
		printf("per-game config read name '%s' value '%s'\n", name, value);

		if (!strcmp(name, "joyswap")) {
			config.swap_joysticks = *value == '1' ? 1 : 0;
		} else if (!strcmp(name, "button")) {
			config_load_button(value);
		} else {
			printf("per-game config unknown name '%s'\n", name);
		}

		name = strtok(NULL, "=");
	}

	apply_button_config();

out:
	FileClose(&file);
	DiskFlush();
	DiskUnmount();
}

void config_game_save()
{
	char namebuf[8+1+3+1];
	sFile file;
	DiskAutoMount();
	SetDir(FileSelPath);

	snprintf(namebuf, sizeof(namebuf), "%s.%s", FileSelLastName, "CFG");
	printf("saving per-game config %s\n", namebuf);

	if (!FileRecreate(&file, namebuf)) {
		printf("failed to recreate per-game config\n");
		goto out;
	}

	FilePrint(&file, "joyswap=%d\n", config.swap_joysticks ? 1 : 0);
	for (int i = 0; i < CONFIG_BTN_MAX; i++) {
		struct button_config *btn = &config.buttons[i];
		u8 val = 0;

		if (btn->mode == CONFIG_BTN_MODE_KEY)
			val = btn->key;
		else if (btn->mode == CONFIG_BTN_MODE_JOY)
			val = btn->joy;

		FilePrint(&file, "button=%d,%d,%d\n", i, btn->mode, val);
	}

out_close:
	if (!FileClose(&file))
		printf("failure closing per-game config file\n");

out:
	DiskFlush();
	DiskUnmount();
}

static void last_game_save()
{
	sFile file;
	DiskAutoMount();
	u16 version = LAST_GAME_VER;

	if (!FileRecreate(&file, LAST_GAME_PATH)) {
		printf("failed to recreate last game config\n");
		goto out;
	}

	FileWrite(&file, &version, sizeof(version));
	FileWrite(&file, &FileSelPathLen, sizeof(FileSelPathLen));
	FileWrite(&file, &FileSelPath[0], FileSelPathLen);
	FileWrite(&file, &FileSelLastNameLen, sizeof(FileSelLastNameLen));
	FileWrite(&file, &FileSelLastName[0], FileSelLastNameLen);
	FileWrite(&file, &FileSelLastNameTop, sizeof(FileSelLastNameTop));
	FileWrite(&file, &FileSelLastNameAttr, sizeof(FileSelLastNameAttr));
	FileWrite(&file, &FileSelLastNameExt, sizeof(FileSelLastNameExt));

out_close:
	if (!FileClose(&file))
		printf("failure closing last game save\n");

out:
	DiskFlush();
	DiskUnmount();
}

static void last_game_load()
{
	sFile file;
	u16 version = 0;
	int read;

	DiskAutoMount();
	if (!FileOpen(&file, LAST_GAME_PATH)) {
		printf("failed to open last game config\n");
		goto out;
	}

	read = FileRead(&file, &version, sizeof(version));
	if (read != sizeof(version)) {
		printf("could not read last game config version\n");
		goto out;
	}
	if (version != LAST_GAME_VER) {
		printf("last game config version doesn't match, skipping\n");
		goto out;
	}
	// YOLO
	FileRead(&file, &FileSelPathLen, sizeof(FileSelPathLen));
	FileRead(&file, &FileSelPath[0], FileSelPathLen);
	FileRead(&file, &FileSelLastNameLen, sizeof(FileSelLastNameLen));
	FileRead(&file, &FileSelLastName[0], FileSelLastNameLen);
	FileRead(&file, &FileSelLastNameTop, sizeof(FileSelLastNameTop));
	FileRead(&file, &FileSelLastNameAttr, sizeof(FileSelLastNameAttr));
	FileRead(&file, &FileSelLastNameExt, sizeof(FileSelLastNameExt));

out:
	FileClose(&file);
	DiskFlush();
	DiskUnmount();
}

static void config_init_defaults()
{
	config.buttons[CONFIG_BTN_A] = {
		.mode = CONFIG_BTN_MODE_JOY,
		.joy = CJ_FIRE,
	};
	config.buttons[CONFIG_BTN_UP] = {
		.mode = CONFIG_BTN_MODE_JOY,
		.joy = CJ_UP,
	};
	config.buttons[CONFIG_BTN_LEFT] = {
		.mode = CONFIG_BTN_MODE_JOY,
		.joy = CJ_LEFT,
	};
	config.buttons[CONFIG_BTN_RIGHT] = {
		.mode = CONFIG_BTN_MODE_JOY,
		.joy = CJ_RIGHT,
	};
	config.buttons[CONFIG_BTN_DOWN] = {
		.mode = CONFIG_BTN_MODE_JOY,
		.joy = CJ_DOWN,
	};
	apply_button_config();
}

int main(void) {
//	DrawPrintStart();
	UartPrintStart();
	DrawPrintStop();
//    vreg_set_voltage(VREG_VOLTAGE_1_05);
//    set_sys_clock_khz(250000, true);  
    stdio_init_all();
    config_init_defaults();
    config_global_load();

    tft.begin();

    FileSelInit("/C64", "Select game", "PRG", &FileSelColBlue);
    last_game_load();
    if (!FileSel())
	    ResetToBootLoader();
    last_game_save();

    config_game_load();

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

    draw_key_hints();

    while (true) {
	if (nFramesC64 == nFramesC64NextInput) {
		nFramesC64NextInput = nFramesC64 + 1;
		c64_Input();
	}

	if (config.single_frame_mode && nFramesC64Last != nFramesC64) {
		char key;

		audio_paused = true;
		KeyWaitNoPressed();
		KeyFlush();
		while ((key = KeyGet()) == NOKEY);
		if (key == KEY_Y)
			osd_active = true;
		nFramesC64Last = nFramesC64;
	}

	if (osd_active) {
		audio_paused = true;
		osd_start();
		osd_active = false;
		audio_paused = false;

		fpsLast = Time();
		nFramesLast = nFrames;
		nFramesC64Last = nFramesC64;
		draw_key_hints();
	}

	c64_Step();

	if (Time() - fpsLast > 1000000) {
		fpsLast = Time();

		if (config.show_fps)
			draw_fps(nFrames - nFramesLast, nFramesC64 - nFramesC64Last);

		nFramesLast = nFrames;
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


