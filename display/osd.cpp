#include "../include.h"

#include "osd.h"
#include "pico_dsp.h"
#include "../pico64/keyboard.h"
#include "../pico64/c64.h"

bool osd_active = false;
u8 osd_key_pending = CK_NOKEY;
u8 osd_mods_pending = 0;

#define KEYS_ROW	17

static int kb_last_row = 4;
static int kb_last_col = 8;

static const u8 kb_codes[][KEYS_ROW] = {
	{
	CK_LEFTARR, CK_1, CK_2, CK_3, CK_4, CK_5, CK_6, CK_7, CK_8, CK_9,
		CK_0, CK_PLUS, CK_MINUS, CK_POUND, CK_HOME, CK_DELETE, CK_F1
	},
	{
	CK_CONTROL, CK_Q, CK_W, CK_E, CK_R, CK_T, CK_Y, CK_U, CK_I, CK_O, CK_P,
		CK_AT, CK_ASTERISK, CK_CARET, CK_RESTORE, CK_NOKEY, CK_F3
	},
	{
	CK_STOP, CK_A, CK_S, CK_D, CK_F, CK_G, CK_H, CK_J, CK_L, CK_M, CK_COLON,
		CK_SEMICOL, CK_EQUAL, CK_RETURN, CK_NOKEY, CK_NOKEY, CK_F5
	},
	{
	CK_CMDR, CK_LSHIFT, CK_Z, CK_X, CK_C, CK_V, CK_B, CK_N, CK_M, CK_COMMA,
		CK_PERIOD, CK_SLASH, CK_RSHIFT, CK_CRSR_DN, CK_CRSR_RT, CK_NOKEY, CK_F7
	},
};

/* indexed by CK_ codes */
static const char * const kb_labels[2*CK_MAX+2] = {
	"DEL", "RTRN", "RT", "F7", "F1", "F3", "F5", "DN",
	"3", "w", "a", "4", "z", "s", "e", "Sft",
	"5", "r", "d", "6", "c", "f", "t", "x",
	"7", "y", "g", "8", "b", "h", "u", "v",
	"9", "i", "j", "0", "m", "k", "o", "n",
	"+", "p", "l", "-", ".", ":", "@", ",",
	"Lb", "*", ";", "HOME", "Sft", "=", "^", "/",
	"1", "<-", "CTRL", "2", "SPACE", "C=", "q", "RN/SP",

	"INS", "RTRN", "LT", "F8", "F2", "F4", "F6", "UP",
	"#", "W", "A", "$", "Z", "S", "E", "Sft",
	"%", "R", "D", "&", "C", "F", "T", "X",
	"'", "Y", "G", "(", "B", "H", "U", "V",
	")", "I", "J", "0", "M", "K", "O", "N",
	"+", "P", "L", "-", ">", "[", "@", "<",
	"Lb", "*", "]", "CLR ", "Sft", "=", "^", "?",
	"!", "<-", "CTRL", "\"", "SPACE", "C=", "Q", "RN/SP",

	0, "REST",
};

/* indexed by CJ_ codes */
static const char * const joy_labels[CJ_MAX] = {
	"UP", "DOWN", "LEFT", "RIGHT", "FIRE"
};

static const char btn_labels[CONFIG_BTN_MAX] = {
	'A', 'B', 'X', 'U', 'L', 'R', 'D'
};

struct kb_state {
	int row;
	int col;
	u8 mods;
	bool with_mods;
};

struct osd_menu {
	void (*draw)(int selrow, void *_private);
	bool (*action)(int row, u8 key, void *_private);
	int rows;
};
static void osd_do(const struct osd_menu *menu, void *_private);

static int inc_limit(int val, int max)
{
	if (!(val == max - 1))
		val++;
	return val;
}

static int dec_limit(int val, int max)
{
	if (!(val == 0))
		val--;
	return val;
}

void draw_key_hints()
{
	int x1 = 0;
	int x2 = 0;
	int y1 = 240 - 18;
	int y2 = 240 - 8;

	struct button_layout *layout;
	char buf[50];

	if (!config.show_keys)
		return;

	SelFont8x8();
	DrawRect(0, y1, 320, 18, COL_BLACK);

	layout = &config.layouts[config.button_layout];

	for (int btn = 0; btn < CONFIG_BTN_MAX; btn++) {
		struct button_config *cfg = &layout->buttons[btn];
		int x, y;

		if (btn <= CONFIG_BTN_X) {
			x = x2;
			y = y2;
			x2 += 320/3;
		} else {
			if (*cfg == default_button_layout.buttons[btn])
				continue;
			x = x1;
			y = y1;
			x1 += 320/4;
		}

		DrawChar(btn_labels[btn], x, y, COL_GRAY);
		DrawChar(':', x + 8, y, COL_GRAY);
		x += 3*8;

		if (cfg->mode == CONFIG_BTN_MODE_OFF) {
			DrawText("NONE", x, y, COL_GRAY);
		} else if (cfg->mode == CONFIG_BTN_MODE_KEY) {
			DrawText(kb_labels[cfg->key], x, y, COL_GRAY);
		} else if (cfg->mode == CONFIG_BTN_MODE_LAYOUT) {
			snprintf(buf, sizeof(buf), "SW.LT %d", cfg->layout + 1);
			DrawText(buf, x, y, COL_GRAY);
		} else {
			DrawText("J.", x, y, COL_GRAY);
			DrawText(joy_labels[cfg->key], x + 2*8, y, COL_GRAY);
		}
	}
}

void draw_fps(u32 lcd, u32 c64)
{
	char buf[50];

	SelFont8x8();
	DrawRect(0, 0, 320, 8, COL_BLACK);

	snprintf(buf, sizeof(buf), "LCD FPS: %3d", lcd);
	DrawText(buf, 0, 0, COL_GRAY);

	snprintf(buf, sizeof(buf), "C64 FPS: %3d", c64);
	DrawText(buf, 320-12*8, 0, COL_GRAY);
}

static void osd_draw_kb_space(struct kb_state *kbs)
{
	int x = 120;
	int endx;
	int y = 20 + 4 * 17;
	bool selected = (kbs->row == 4);

	DrawRect(x - 3, y, 1, 16, COL_LTGRAY);

	DrawTextBg(kb_labels[CK_SPACE], x, y,
		   selected ? COL_BLACK : COL_WHITE,
		   selected ? COL_LTGREEN : COL_BLACK);

	endx = x + strlen(kb_labels[CK_SPACE]) * 8;
	DrawRect(endx + 3, y, 1, 16, COL_LTGRAY);
	DrawRect(x - 3, y + 16, endx + 3 - (x - 3) + 1, 1, COL_LTGRAY);

}

static void osd_draw_kb_row(int row, struct kb_state *kbs)
{
	int x = 15;
	int y = 20 + row * 17;

	DrawRect(x - 3, y, 1, 16, COL_LTGRAY);
	for (int col = 0; col < KEYS_ROW; col++) {
		u8 ck = kb_codes[row][col];
		if (ck != CK_NOKEY) {
			bool selected = (kbs->row == row) && (kbs->col == col);
			bool mod_active = false;
			const char *label = "??";
			COLTYPE fg, bg;

			if ((ck == CK_LSHIFT && kbs->mods & CK_MOD_LSHIFT) ||
			    (ck == CK_RSHIFT && kbs->mods & CK_MOD_RSHIFT) ||
			    (ck == CK_CMDR && kbs->mods & CK_MOD_CMDR) ||
			    (ck == CK_CONTROL && kbs->mods & CK_MOD_CONTROL))
				mod_active = true;

			if (mod_active) {
				fg = selected ? COL_WHITE : COL_BLACK;
				bg = selected ? COL_LTGREEN : COL_WHITE;
			} else {
				fg = selected ? COL_BLACK : COL_WHITE;
				bg = selected ? COL_LTGREEN : COL_BLACK;
			}

			if (ck < CK_MAX && kbs->mods & (CK_MOD_LSHIFT | CK_MOD_RSHIFT))
				ck = CKSH(ck);
			if (ck < count_of(kb_labels))
				label = kb_labels[ck];

			DrawTextBg(label, x, y, fg, bg);
			x += strlen(label) * 8 + 2;
			/* line right of key */
			DrawRect(x, y, 1, 16, COL_LTGRAY);
			x += 3;
		}
		/* right-align the Fx column */
		if (col == KEYS_ROW - 2) {
			/* line under the row */
			DrawRect(15 - 3, y + 16, x - 2 - (15 - 3), 1, COL_LTGRAY);
			/* line above the row */
			DrawRect(15 - 3, y - 1, x - 2 - (15 - 3), 1, COL_LTGRAY);

			x = 320 - 15 - 2*8;
			/* line under the Fx */
			DrawRect(x - 3, y + 16, 2*8 + 2*3, 1, COL_LTGRAY);
			/* line above F1 */
			if (row == 0)
				DrawRect(x - 3, y - 1, 2*8 + 2*3, 1, COL_LTGRAY);
			/* line left of the Fx */
			DrawRect(x - 3, y, 1, 16, COL_LTGRAY);
		}
	}
}

static void osd_draw_kb(struct kb_state *kbs)
{
	for (int i = 0; i < 4; i++)
		osd_draw_kb_row(i, kbs);
	osd_draw_kb_space(kbs);
}

static u8 osd_kb_get_code(struct kb_state *kbs)
{
	if (kbs->row == 4)
		return CK_SPACE;
	else
		return kb_codes[kbs->row][kbs->col];
}

static void osd_kb_fixup_col(struct kb_state *kbs, int prev_row)
{
	int x = 0, x_prev = 0;
	u8 ck;
	const char *label;

	if (prev_row == kbs->row || prev_row == 4 || kbs->col == KEYS_ROW - 1)
		goto out;

	/*
	 * ideally we'd have this pre-computed, but it would have to be
	 * compile-time to be in flash, otherwise let's not waste RAM
	 */
	for (int col = 0; col < kbs->col; col++) {
		ck = kb_codes[prev_row][col];
		label = kb_labels[ck];

		x_prev += strlen(label) * 8 + 5;
	}

	ck = kb_codes[prev_row][kbs->col];
	label = kb_labels[ck];
	x_prev += strlen(label) * 8 / 2;

	for (int col = 0; col < KEYS_ROW - 1; col++) {
		ck = kb_codes[kbs->row][col];
		label = kb_labels[ck];
		x += strlen(label) * 8 + 2;
		if (x >= x_prev) {
			kbs->col = col;
			break;
		}
		x += 3;
	}
out:
	while (kb_codes[kbs->row][kbs->col] == CK_NOKEY)
		kbs->col--;
}

/* return true if key was selected */
static bool osd_start_kb(bool with_mods, u8 *ret_kc, u8 *ret_mods)
{
	struct kb_state kbs = {
		.row = kb_last_row,
		.col = kb_last_col,
		.with_mods = with_mods,
	};
	bool redraw = true;
	SelFont8x16();
	DrawClear();
	int prev_row = kbs.row;
	while(true) {
		if (redraw)
			osd_draw_kb(&kbs);
		redraw = true;

		char key = KeyGet();
		u8 kc;
		if (kbs.row != 4)
			prev_row = kbs.row;
		switch(key) {
		case NOKEY:
			redraw = false;
			break;
		case KEY_RIGHT:
			if (kbs.row == 4)
				break;
			if (++kbs.col == KEYS_ROW)
				kbs.col = 0;
			else if (kb_codes[kbs.row][kbs.col] == CK_NOKEY)
				kbs.col = KEYS_ROW - 1;
			break;
		case KEY_LEFT:
			if (kbs.row == 4)
				break;
			if (--kbs.col < 0)
				kbs.col = KEYS_ROW - 1;
			osd_kb_fixup_col(&kbs, kbs.row);
			break;
		case KEY_DOWN:
			if (++kbs.row > 4)
				kbs.row = 0;
			if (kbs.row < 4)
				osd_kb_fixup_col(&kbs, prev_row);
			break;
		case KEY_UP:
			if (--kbs.row < 0)
				kbs.row = 4;
			if (kbs.row < 4)
				osd_kb_fixup_col(&kbs, prev_row);
			break;
		case KEY_X:
			return false;
		case KEY_A:
			kc = osd_kb_get_code(&kbs);
			if (with_mods) {
				if (kc == CK_LSHIFT) {
					kbs.mods ^= CK_MOD_LSHIFT;
					break;
				} else if (kc == CK_RSHIFT) {
					kbs.mods ^= CK_MOD_RSHIFT;
					break;
				} else if (kc == CK_CMDR) {
					kbs.mods ^= CK_MOD_CMDR;
					break;
				} else if (kc == CK_CONTROL) {
					kbs.mods ^= CK_MOD_CONTROL;
					break;
				}
			}

			*ret_kc = kc;
			*ret_mods = kbs.mods;
			kb_last_row = kbs.row;
			kb_last_col = kbs.col;
			return true;
		default:
			;
		}
	}
}

static u8 osd_config_button_joy(u8 btn, int lay)
{
	char buf[50];
	u8 key;

	snprintf(buf, sizeof(buf), "JOYSTICK ACTION FOR BTN %c IN LAYOUT %d",
			btn_labels[btn], lay + 1);
	DrawClear();
	DrawText(buf, 0, 0, COL_WHITE);
	DrawText("PRESS BUTTON TO ASSIGN:", 0, 16, COL_WHITE);
	DrawText("UP/DOWN/LEFT/RIGHT: JOYSTICK DIRECTION", 0, 32, COL_WHITE);
	DrawText("A: JOYSTICK FIRE", 0, 48, COL_WHITE);
	DrawText("Y: CANCEL", 0, 64, COL_WHITE);

	KeyWaitNoPressed();
	KeyFlush();

	while (true) {
		char key = KeyGet();

		switch (key) {
		case KEY_Y:
			return CJ_MAX;
		case KEY_A:
			return CJ_FIRE;
		case KEY_UP:
			return CJ_UP;
		case KEY_DOWN:
			return CJ_DOWN;
		case KEY_LEFT:
			return CJ_LEFT;
		case KEY_RIGHT:
			return CJ_RIGHT;
		default:
			;
		}
	}
}

static void osd_menu_name(const char *text, int row, int selrow)
{
	bool selected = (row == selrow);
	DrawTextBg(text, 0, 16*(row+1), selected ? COL_BLACK : COL_WHITE,
				selected ? COL_LTGREEN : COL_BLACK);
}

static void osd_menu_val(const char *text, int row)
{
	DrawTextBg(text, 8*12, 16*(row+1), COL_WHITE, COL_BLACK);
}

static void osd_menu_val_char(char ch, int row)
{
	DrawCharBg(ch, 8*12, 16*(row+1), COL_WHITE, COL_BLACK);
}

static void osd_draw_vol(int row, int selrow)
{
	int vol = ConfigGetVolume();
	char buf[50];

	osd_menu_name("VOLUME:", row, selrow);

	snprintf(buf, sizeof(buf), "%3d %% (A - mute/100%)", vol * 10);
	osd_menu_val(buf, row);
}

static void osd_draw_joy(int row, int selrow)
{
	osd_menu_name("JOYSTICK:", row, selrow);

	osd_menu_val_char(config.swap_joysticks ? '2' : '1', row);
}

static void osd_draw_btn_layout(int row, int selrow)
{
	char buf[50];

	osd_menu_name("BTN LAYOUT:", row, selrow);

	snprintf(buf, sizeof(buf), "%d (L/R - change, A - edit)",
			config.button_layout + 1);
	osd_menu_val(buf, row);
}

static void osd_draw_button(int row, int selrow, int lay, u8 btn)
{
	struct button_layout *layout = &config.layouts[lay];
	struct button_config *cfg = &layout->buttons[btn];
	char buf[50];

	snprintf(buf, sizeof(buf), "BUTTON %c:", btn_labels[btn]);
	osd_menu_name(buf, row, selrow);

	if (cfg->mode == CONFIG_BTN_MODE_OFF) {
osd_menu_val("NO ACTION", row);
	} else if (cfg->mode == CONFIG_BTN_MODE_KEY) {
		snprintf(buf, sizeof(buf), "KEY %s", kb_labels[cfg->key]);
		osd_menu_val(buf, row);
	} else if (cfg->mode == CONFIG_BTN_MODE_LAYOUT) {
		snprintf(buf, sizeof(buf), "SWITCH TO LAYOUT %d", cfg->layout + 1);
		osd_menu_val(buf, row);
	} else {
		snprintf(buf, sizeof(buf), "JOY %s", joy_labels[cfg->key]);
		osd_menu_val(buf, row);
	}
}

static void osd_draw_bool(int row, int selrow, const char *name, bool *val)
{
	osd_menu_name(name, row, selrow);

	osd_menu_val(*val ? "ON" : "OFF", row);
}

struct btn_assignment_info {
	int layout;
	u8 btn;
	int switch_layout;
};

static void osd_btn_assign_draw(int selrow, void *_private)
{
	struct btn_assignment_info *info = (struct btn_assignment_info *)_private;
	char buf[50];

	DrawClear();
	snprintf(buf, sizeof(buf), "ASSIGN BUTTON %c IN LAYOUT %d",
			btn_labels[info->btn], info->layout + 1);
	DrawText(buf, 0, 0, COL_WHITE);
	osd_menu_name("JOYSTICK ACTION", 0, selrow);
	osd_menu_name("KEYBOARD KEY", 1, selrow);
	osd_menu_name("NO ACTION", 2, selrow);
	osd_menu_name("SW. LAYOUT", 3, selrow);

	snprintf(buf, sizeof(buf), "%d (L/R to change)", info->switch_layout + 1);
	osd_menu_val(buf, 3);
}

static bool osd_btn_assign_action(int row, u8 key, void *_private)
{
	struct btn_assignment_info *info = (struct btn_assignment_info *)_private;
	struct button_layout *layout = &config.layouts[info->layout];
	struct button_config *cfg = &layout->buttons[info->btn];
	u8 kc, jc, mods;

	switch (row) {
	case 0:
		jc = osd_config_button_joy(info->btn, info->layout);
		if (jc < CJ_MAX) {
			cfg->mode = CONFIG_BTN_MODE_JOY;
			cfg->joy = jc;
		}
		break;
	case 1:
		if (osd_start_kb(false, &kc, &mods)) {
			cfg->mode = CONFIG_BTN_MODE_KEY;
			cfg->key = kc;
		}
		break;
	case 2:
		cfg->mode = CONFIG_BTN_MODE_OFF;
		break;
	case 3:
		if (key == KEY_LEFT) {
			info->switch_layout = dec_limit(info->switch_layout, CONFIG_BTN_LAYOUT_MAX);
			return false;
		} else if (key == KEY_RIGHT) {
			info->switch_layout = inc_limit(info->switch_layout, CONFIG_BTN_LAYOUT_MAX);
			return false;
		} else {
			cfg->mode = CONFIG_BTN_MODE_LAYOUT;
			cfg->layout = info->switch_layout;
		}
		break;
	}
	apply_button_config();
	return true;
}

static void osd_btn_assignment_start(u8 button, int layout)
{
	const struct osd_menu osd_btn_assign = {
		.draw = osd_btn_assign_draw,
		.action = osd_btn_assign_action,
		.rows = 4,
	};
	struct btn_assignment_info info = {
		.layout = layout,
		.btn = button,
	};
	osd_do(&osd_btn_assign, (void *)&info);
}

static void osd_btn_layout_draw(int selrow, void *_private)
{
	int layout = *(int *)_private;
	char buf[50];

	DrawClear();
	snprintf(buf, sizeof(buf), "EDITING LAYOUT %d (L/R: switch, Y: back)",
			layout + 1);

	DrawText(buf, 0, 0, COL_WHITE);
	for (int i = 0; i < CONFIG_BTN_MAX; i++)
		osd_draw_button(i, selrow, layout, i);

	osd_menu_name("MAKE THIS LAYOUT INITIAL (IN GAME CFG)", CONFIG_BTN_MAX, selrow);
	osd_menu_name("CURRENT:", CONFIG_BTN_MAX + 1, selrow);
	snprintf(buf, sizeof(buf), "%d", config.initial_layout + 1);
	osd_menu_val(buf, CONFIG_BTN_MAX + 1);
}

static bool osd_btn_layout_action(int row, u8 key, void *_private)
{
	int layout = *(int *)_private;

	if (key == KEY_LEFT) {
		layout = dec_limit(layout, CONFIG_BTN_LAYOUT_MAX);
		*(int *)_private = layout;
		return false;
	} else if (key == KEY_RIGHT) {
		layout = inc_limit(layout, CONFIG_BTN_LAYOUT_MAX);
		*(int *)_private = layout;
		return false;
	}

	if (row < CONFIG_BTN_MAX) {
		osd_btn_assignment_start(row, layout);
	} else {
		config.initial_layout = layout;
	}
	return false;
}

static void osd_cleanup(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
}

static void osd_start_btn_layout()
{
	const struct osd_menu osd_btn_layout = {
		.draw = osd_btn_layout_draw,
		.action = osd_btn_layout_action,
		.rows = CONFIG_BTN_MAX + 1,
	};
	int layout = config.button_layout;
	osd_do(&osd_btn_layout, (void *)&layout);
}

static bool osd_main_action(int row, u8 key, void *_private)
{
	if (key == KEY_X) {
		return osd_start_kb(true, &osd_key_pending, &osd_mods_pending);
	} else if (key == KEY_B) {
		ResetToBootLoader();
	}

	switch (row) {
	case 0: /* joystick */
		config.swap_joysticks = !config.swap_joysticks;
		break;
	case 1: /* volume */
		if (key == KEY_LEFT)
			ConfigDecVolume();
		else if (key == KEY_RIGHT)
			ConfigIncVolume();
		else if (key == KEY_A) {
			if (ConfigGetVolume() != 0)
				ConfigSetVolume(0);
			else
				ConfigSetVolume(CONFIG_VOLUME_FULL);
		}
		audio_vol_update();
		break;
	case 2: /* button layout */
		if (key == KEY_LEFT) {
			config.button_layout = dec_limit(config.button_layout,
					CONFIG_BTN_LAYOUT_MAX);
		} else if (key == KEY_RIGHT) {
			config.button_layout = inc_limit(config.button_layout,
					CONFIG_BTN_LAYOUT_MAX);
		} else if (key == KEY_A) {
			osd_start_btn_layout();
		}
		apply_button_config();
		break;
	case 3: /* autorun */
		config.autorun = !config.autorun;
		break;
	case 4: /* fps */
		config.show_fps = !config.show_fps;
		break;
	case 5: /* key hints */
		config.show_keys = !config.show_keys;
		break;
	case 6:
		config_game_save();
		break;
	case 7:
		config_global_save();
		break;
	case 8:
		config.single_frame_mode = !config.single_frame_mode;
		break;
	default:
		break;
	}
	return false;
}

static void osd_main_draw(int selrow, void *_private)
{
	DrawClear();
	DrawText("PAUSED: Y - back, X - kbd, B - reboot", 0, 0, COL_WHITE);
	osd_draw_joy(0, selrow);
	osd_draw_vol(1, selrow);
	osd_draw_btn_layout(2, selrow);
	osd_draw_bool(3, selrow, "AUTORUN:", &config.autorun);
	osd_draw_bool(4, selrow, "SHOW FPS:", &config.show_fps);
	osd_draw_bool(5, selrow, "BTN HINTS:", &config.show_keys);
	osd_menu_name("SAVE PER-GAME CONFIG", 6, selrow);
	osd_menu_name("SAVE GLOBAL CONFIG", 7, selrow);
	osd_draw_bool(8, selrow, "FRM STEP:", &config.single_frame_mode);
}

static void osd_do(const struct osd_menu *menu, void *_private)
{
	int selrow = 0;
	bool redraw = true;

	SelFont8x16();
	menu->draw(selrow, _private);

	KeyWaitNoPressed();
	KeyFlush();

	while(true) {
		char key;
		if (redraw)
			menu->draw(selrow, _private);

		redraw = true;
		key = KeyGet();

		switch(key) {
		case KEY_UP:
			selrow = dec_limit(selrow, menu->rows);
			break;
		case KEY_DOWN:
			selrow = inc_limit(selrow, menu->rows);
			break;
		case KEY_Y:
			osd_cleanup();
			return;
		case KEY_X:
		case KEY_B:
		case KEY_A:
		case KEY_LEFT:
		case KEY_RIGHT:
			if (menu->action(selrow, key, _private)) {
				osd_cleanup();
				SelFont8x16();
				return;
			}
			break;
		default:
			redraw = false;
		}
	}
}

void osd_start(void)
{
	const struct osd_menu osd_main = {
		.draw = osd_main_draw,
		.action = osd_main_action,
		.rows = 9,
	};

	osd_do(&osd_main, NULL);
}

