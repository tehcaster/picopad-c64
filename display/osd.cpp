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

void draw_key_hints()
{
	int x = 0;
	int y = 240 - 8;

	if (!config.show_keys)
		return;

	SelFont8x8();
	DrawRect(0, y, 320, 8, COL_BLACK);

	for (int btn = 0; btn < CONFIG_BTN_MAX; btn++) {
		int x2 = x;

		DrawChar(btn_labels[btn], x2, y, COL_GRAY);
		DrawChar(':', x2 + 8, y, COL_GRAY);
		x2 += 3*8;

		struct button_config *cfg = &config.buttons[btn];
		if (cfg->mode == CONFIG_BTN_MODE_OFF) {
			DrawText("NONE", x2, y, COL_GRAY);
		} else if (cfg->mode == CONFIG_BTN_MODE_KEY) {
			DrawText(kb_labels[cfg->key], x2, y, COL_GRAY);
		} else {
			DrawText("J.", x2, y, COL_GRAY);
			DrawText(joy_labels[cfg->key], x2 + 2*8, y, COL_GRAY);
		}

		x += 107;
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

static void osd_config_button(u8 btn)
{
	struct button_config *cfg = &config.buttons[btn];
	char buf[50];
	u8 key;
	u8 kc, mods;

	snprintf(buf, sizeof(buf), "Reassigning button %c in layout %d",
			btn_labels[btn], config.button_layout);
	DrawClear();
	DrawText(buf, 0, 0, COL_WHITE);
	DrawText("Press button to assign:", 0, 16, COL_WHITE);
	DrawText("UP/DOWN/LEFT/RIGHT: joystick direction", 0, 32, COL_WHITE);
	DrawText("A: joystick FIRE", 0, 48, COL_WHITE);
	DrawText("B: no action", 0, 64, COL_WHITE);
	DrawText("X: keyboard key", 0, 80, COL_WHITE);
	DrawText("Y: cancel assignment", 0, 96, COL_WHITE);

	KeyWaitNoPressed();
	KeyFlush();

	while((key = KeyGet()) == NOKEY);

	switch (key) {
	case KEY_Y:
		return;
	case KEY_B:
		cfg->mode = CONFIG_BTN_MODE_OFF;
		break;
	case KEY_A:
		cfg->mode = CONFIG_BTN_MODE_JOY;
		cfg->joy = CJ_FIRE;
		break;
	case KEY_UP:
		cfg->mode = CONFIG_BTN_MODE_JOY;
		cfg->joy = CJ_UP;
		break;
	case KEY_DOWN:
		cfg->mode = CONFIG_BTN_MODE_JOY;
		cfg->joy = CJ_DOWN;
		break;
	case KEY_LEFT:
		cfg->mode = CONFIG_BTN_MODE_JOY;
		cfg->joy = CJ_LEFT;
		break;
	case KEY_RIGHT:
		cfg->mode = CONFIG_BTN_MODE_JOY;
		cfg->joy = CJ_RIGHT;
		break;
	case KEY_X:
		if (osd_start_kb(false, &kc, &mods)) {
			cfg->mode = CONFIG_BTN_MODE_KEY;
			cfg->key = kc;
		}
		break;
	}
	apply_button_config();
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

static void osd_draw_button(int row, int selrow, u8 btn)
{
	struct button_config *cfg = &config.buttons[btn];
	char buf[50];

	snprintf(buf, sizeof(buf), "BUTTON %c:", btn_labels[btn]);
	osd_menu_name(buf, row, selrow);

	if (cfg->mode == CONFIG_BTN_MODE_OFF) {
		osd_menu_val("NO ACTION", row);
	} else if (cfg->mode == CONFIG_BTN_MODE_KEY) {
		snprintf(buf, sizeof(buf), "KEY %s", kb_labels[cfg->key]);
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

#define OSD_MENU_BTN_MAXROW	CONFIG_BTN_MAX - 1
static void osd_draw_btn_layout_all(int selrow)
{
	char buf[50];

	DrawClear();
	snprintf(buf, sizeof(buf), "EDITING LAYOUT %d: Y - back",
			config.button_layout + 1);

	DrawText(buf, 0, 0, COL_WHITE);
	for (int i = 0; i < CONFIG_BTN_MAX; i++)
		osd_draw_button(i, selrow, i);
}

static void osd_btn_layout_action(int row, u8 key)
{
	osd_config_button(row);
}

static void osd_cleanup(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
}

static void osd_start_btn_layout()
{
	int selrow = 0;
	bool redraw = true;

	SelFont8x16();
	osd_draw_btn_layout_all(selrow);

	KeyWaitNoPressed();
	KeyFlush();

	while(true) {
		char key;
		if (redraw)
			osd_draw_btn_layout_all(selrow);

		redraw = true;
		key = KeyGet();

		switch(key) {
		case KEY_UP:
			if (selrow != 0)
				selrow--;
			break;
		case KEY_DOWN:
			if (selrow < OSD_MENU_BTN_MAXROW)
				selrow++;
			break;
		case KEY_Y:
			osd_cleanup();
			return;
		case KEY_A:
		case KEY_LEFT:
		case KEY_RIGHT:
			osd_btn_layout_action(selrow, key);
			break;
		default:
			redraw = false;
		}
	}
}

static void osd_action(int row, u8 key)
{
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
			if (config.button_layout == 0)
				config.button_layout = CONFIG_BTN_LAYOUT_MAX - 1;
			else
				config.button_layout--;
		} else if (key == KEY_RIGHT) {
			if (config.button_layout == CONFIG_BTN_LAYOUT_MAX - 1)
				config.button_layout = 0;
			else
				config.button_layout++;
		} else if (key == KEY_A) {
			osd_start_btn_layout();
		}
		apply_button_config();
		break;
	case 5: /* autorun */
		config.autorun = !config.autorun;
		break;
	case 6: /* fps */
		config.show_fps = !config.show_fps;
		break;
	case 7: /* key hints */
		config.show_keys = !config.show_keys;
		break;
	case 8:
		config_game_save();
		break;
	case 9:
		config_global_save();
		break;
	case 10:
		config.single_frame_mode = !config.single_frame_mode;
		break;
	default:
		break;
	}
}

#define OSD_MENU_MAXROW	8
static void osd_draw_all(int selrow)
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

void osd_start(void)
{
	int selrow = 0;
	bool redraw = true;

	SelFont8x16();
	osd_draw_all(selrow);

	KeyWaitNoPressed();
	KeyFlush();

	while(true) {
		char key;
		if (redraw)
			osd_draw_all(selrow);

		redraw = true;
		key = KeyGet();

		switch(key) {
		case KEY_UP:
			if (selrow != 0)
				selrow--;
			break;
		case KEY_DOWN:
			if (selrow < OSD_MENU_MAXROW)
				selrow++;
			break;
		case KEY_X:
			if (osd_start_kb(true, &osd_key_pending,
						&osd_mods_pending)) {
				osd_cleanup();
				SelFont8x16();
				return;
			}
			break;
		case KEY_B:
			ResetToBootLoader();
			break;
		case KEY_Y:
			osd_cleanup();
			return;
		case KEY_A:
		case KEY_LEFT:
		case KEY_RIGHT:
			osd_action(selrow, key);
			break;
		default:
			redraw = false;
		}
	}
}
