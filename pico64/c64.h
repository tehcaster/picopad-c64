
#ifndef _C64_H_
#define _C64_H_

extern void c64_Init(void);
extern void c64_Step(void);
extern void c64_Start(char * filename);
extern void c64_Input();

extern u32 nFramesC64;
extern u32 timeSWISR;

#define CONFIG_BTN_LAYOUT_MAX	10

#define CONFIG_BTN_A		0
#define CONFIG_BTN_B		1
#define CONFIG_BTN_X		2
#define CONFIG_BTN_UP		3
#define CONFIG_BTN_LEFT		4
#define CONFIG_BTN_RIGHT	5
#define CONFIG_BTN_DOWN		6
#define CONFIG_BTN_MAX		7

#define CONFIG_BTN_MODE_OFF	0
#define CONFIG_BTN_MODE_KEY	1
#define CONFIG_BTN_MODE_JOY	2
#define CONFIG_BTN_MODE_MAX	3

struct button_config {
	u8 mode;
	union {
		u8 key;
		u8 joy;
	};

	bool operator!=(const button_config &a) {
		if (mode != a.mode)
			return true;
		if (mode == CONFIG_BTN_MODE_KEY && key != a.key)
			return true;
		if (mode == CONFIG_BTN_MODE_JOY && joy != a.joy)
			return true;
		return false;
	}

	bool operator==(const button_config &a) {
		return !(*this != a);
	}

};

struct button_layout {
	button_config buttons[CONFIG_BTN_MAX];
};

struct emu_config {
	bool swap_joysticks;
	bool show_fps;
	bool show_keys;
	bool autorun;
	bool single_frame_mode;
	u8 button_layout;
	u8 initial_layout;
	struct button_layout layouts[CONFIG_BTN_LAYOUT_MAX];
};

extern const struct button_layout default_button_layout;
extern struct emu_config config;

void apply_button_config();
void config_global_save();
void config_game_save();

#endif
