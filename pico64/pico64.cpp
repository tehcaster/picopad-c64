
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
		} else if (!strcmp(name, "volume")) {
			int vol = atoi(value);
			ConfigSetVolume(vol);
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
	FilePrint(&file, "volume=%d\n", ConfigGetVolume());

out_close:
	if (!FileClose(&file))
		printf("failure closing global config file\n");

out:
	DiskFlush();
	DiskUnmount();
}

static void config_load_button(u8 layout, char *val)
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

	struct button_config *btn = &config.layouts[layout].buttons[idx];

	btn->mode = mode;
	if (mode == CONFIG_BTN_MODE_KEY)
		btn->key = key;
	else if (mode == CONFIG_BTN_MODE_JOY)
		btn->joy = key;
	else if (mode == CONFIG_BTN_MODE_LAYOUT)
		btn->layout = key;
}

static void config_game_load()
{
	char buf[1024];
	int read;
	sFile file;
	char *name;
	int layout = 0;
	u32 off = 0;
	bool read_all;

	snprintf(buf, sizeof(buf), "%s.%s", FileSelLastName, "CFG");
	printf("loading per-game config %s\n", buf);

	DiskAutoMount();
	SetDir(FileSelPath);
	if (!FileOpen(&file, buf)) {
		printf("could not open per-game config file\n");
		goto out;
	}

restart:
	read = FileRead(&file, buf, 1023);
	buf[read] = 0;
	if (read == 1023)
		read_all = false;
	else
		read_all = true;
	name = strtok(buf, "=");
	while (name) {

		if (!read_all) {
			u32 pos = name - &buf[0];

			if (pos > 512) {
				printf("seeking to restart at pos %u off %u\n", pos, off + pos);

				off = off + pos;
				if (!FileSeek(&file, off)) {
					printf("seek failed");
					goto out;
				}
				goto restart;
			}
		}

		char *value = strtok(NULL, "\n");
		if (!value) {
			printf("per-game config option '%s' has no value\n", name);
			goto out;
		}
		printf("per-game config read name '%s' value '%s'\n", name, value);

		if (!strcmp(name, "joyswap")) {
			config.swap_joysticks = *value == '1' ? 1 : 0;
		} else if (!strcmp(name, "initial_layout")) {
			int ilayout = atoi(value);
			if (ilayout >= 0 && ilayout < CONFIG_BTN_LAYOUT_MAX) {
				config.initial_layout = ilayout;
				config.button_layout = ilayout;
			}
		} else if (!strcmp(name, "layout_buttons")) {
			layout = atoi(value);
			if (layout < 0 || layout >= CONFIG_BTN_LAYOUT_MAX)
				layout = 0;
		} else if (!strcmp(name, "button")) {
			config_load_button(layout, value);
		} else if (!strcmp(name, "t64_entry")) {
			int val = atoi(value);

			if (val >= 0) {
				config.t64_entry = val;
			}
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

	FilePrint(&file, "t64_entry=%d\n", config.t64_entry);
	FilePrint(&file, "joyswap=%d\n", config.swap_joysticks ? 1 : 0);
	FilePrint(&file, "initial_layout=%d\n", config.initial_layout);
	for (int lay = 0; lay < CONFIG_BTN_LAYOUT_MAX; lay++) {
		struct button_layout *layout = &config.layouts[lay];
		bool all_default = true;

		for (int i = 0; i < CONFIG_BTN_MAX; i++) {
			if (layout->buttons[i] != default_button_layout.buttons[i]) {
				all_default = false;
				break;
			}
		}

		if (all_default)
			continue;

		printf("layout_buttons=%d\n", lay);
		FilePrint(&file, "layout_buttons=%d\n", lay);

		for (int i = 0; i < CONFIG_BTN_MAX; i++) {
			struct button_config *btn = &layout->buttons[i];
			u8 val = 0;

			if (*btn == default_button_layout.buttons[i])
				continue;

			if (btn->mode == CONFIG_BTN_MODE_KEY)
				val = btn->key;
			else if (btn->mode == CONFIG_BTN_MODE_JOY)
				val = btn->joy;
			else if (btn->mode == CONFIG_BTN_MODE_LAYOUT)
				val = btn->layout;

			printf("button=%d,%d,%d\n", i, btn->mode, val);
			FilePrint(&file, "button=%d,%d,%d\n", i, btn->mode, val);
		}
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

static bool select_t64(const char **error)
{
	sFile file;
	unsigned fsize;
	char buf[33];
	buf[32] = 0;
	unsigned entries_max, entries_used;
	struct osd_filelist osd_filelist = { };
	int entry_selected = 0;

	*error = NULL;

	DiskAutoMount();
	SetDir(FileSelPath);

	if (!FileOpen(&file, FileSelTempBuf)) {
		*error = "Cannot open file";
		return false;
	}

	fsize = GetFileSize(FileSelTempBuf);

	if (FileRead(&file, buf, 32) != 32) {
		*error = "Read failed";
		return false;
	}

	printf("t64 header: %s\n", buf);

	if (strncmp(buf, "C64", 3)) {
		*error = "T64 wrong header";
		return false;
	}

	if (FileRead(&file, buf, 32) != 32) {
		*error = "Read failed";
		return false;
	}

	unsigned ver = (unsigned) buf[0] + (unsigned) buf[1] * 256;
	printf("t64 version: %x\n", ver);

	entries_max = (unsigned) buf[2] + (unsigned) buf[3] * 256;
	entries_used = (unsigned) buf[4] + (unsigned) buf[5] * 256;

	printf("dir entries: %u / %u\n", entries_used, entries_max);

	printf("tape name: %s\n", buf + 8);
	memcpy(&osd_filelist.tape_name, buf + 8, 24);

	if (entries_used == 0) {
		*error = "No tape entries";
		return false;
	}

	if (entries_used == 1)
		goto selected;

	if (config.t64_entry >= 0 && config.t64_entry < entries_used)
		entry_selected = config.t64_entry;

	osd_filelist.pages = (entries_used + FILES_PER_PAGE - 1) / FILES_PER_PAGE;
	osd_filelist.page = entry_selected / FILES_PER_PAGE;
	osd_filelist.selected = entry_selected % FILES_PER_PAGE;

show_page:
	osd_filelist.nr_files = (osd_filelist.page == osd_filelist.pages - 1) ?
				entries_used - (osd_filelist.pages - 1) * FILES_PER_PAGE : FILES_PER_PAGE;

	if (osd_filelist.selected > osd_filelist.nr_files - 1)
		osd_filelist.selected = osd_filelist.nr_files - 1;

	printf("page %d of %d selected row %d of %d\n", osd_filelist.page,
			 osd_filelist.pages, osd_filelist.selected,
			 osd_filelist.nr_files);

	FileSeek(&file, 64 + osd_filelist.page * FILES_PER_PAGE * 32);

	for (int i = 0; i < osd_filelist.nr_files; i++) {
		if (FileRead(&file, buf, 32) != 32) {
			FileSelDispBigErr("Read failed");
			return false;
		}

		printf("directory entry %d type %x ftype %x\n", i, buf[0], buf[1]);

		printf("filename: %s\n", buf + 16);

		memcpy(&osd_filelist.names[i][0], buf + 16, 16);
	}

	if (!osd_tape_file_select_start(&osd_filelist))
		return false;

	if (osd_filelist.action == KEY_LEFT) {
		osd_filelist.page--;
		goto show_page;
	} else if (osd_filelist.action == KEY_RIGHT) {
		osd_filelist.page++;
		goto show_page;
	}

	entry_selected = osd_filelist.page * FILES_PER_PAGE + osd_filelist.selected;

	printf("selected entry %d\n", entry_selected);

	config.t64_entry = entry_selected;

	FileSeek(&file, 64 + entry_selected * 32);

selected:

	if (FileRead(&file, buf, 32) != 32) {
		*error = "Read failed";
		return false;
	}

	unsigned start = (unsigned) buf[2] + (unsigned) buf[3] * 256;
	unsigned end = (unsigned) buf[4] + (unsigned) buf[5] * 256;

	unsigned offset = (unsigned) buf[8] + (unsigned) buf[9] * 256
			+ (unsigned) buf[10] * 256 * 256
			+ (unsigned) buf[11] * 256 * 256 * 256;

	printf("start_addr %x end_addr %x offset %u size %u\n", start, end, offset, end - start);

	if (start == 0 || end == 0 || end - start == 0 || offset >= fsize ||
			offset + (end - start)  > fsize) {
		if (entries_used == 1) {
			*error = "Invalid tape entry";
			return false;
		}

		tft.stopRefresh();
		FileSelDispBigErr("Invalid tape entry");
		tft.startRefresh();
		goto show_page;
	}

	prg_offset = offset;
	prg_size = end - start;
	prg_addr = start;

	FileClose(&file);

	return true;
}

static void config_init_defaults()
{
	for (int i = 0; i < CONFIG_BTN_LAYOUT_MAX; i++)
		config.layouts[i] = default_button_layout;
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

    FileSelInit2("/C64", "Select game", "PRG", "T64", &FileSelColBlue);
    last_game_load();
    if (!FileSel())
	    ResetToBootLoader();

    last_game_save();

    /* in case the tape file selector is needed */
    SelFont8x8();
    tft.startRefresh();

    config_game_load();

    if (FileSelLastNameExt == 1) {
	const char * error;

        if (!select_t64(&error)) {
		tft.stopRefresh();
		if (error)
			FileSelDispBigErr(error);
		ResetToBootLoader();
	}

	config_game_save();
    }

    c64_Init();
    tft.begin_audio();

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


