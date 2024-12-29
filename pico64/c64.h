
#ifndef _C64_H_
#define _C64_H_

extern void c64_Init(void);
extern void c64_Step(void);
extern void c64_Start(char * filename);
extern void c64_Input();

extern u32 nFramesC64;
extern u32 timeSWISR;

#define CONFIG_BTN_A	0
#define CONFIG_BTN_B	1
#define CONFIG_BTN_X	2
#define CONFIG_BTN_MAX	3

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
};

struct emu_config {
	bool swap_joysticks;
	bool show_fps;
	bool show_keys;
	bool autorun;
	button_config buttons[CONFIG_BTN_MAX];
	bool single_frame_mode;
};

extern struct emu_config config;

void apply_button_config();
void config_global_save();
void config_game_save();

#endif
