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
	'A', 'B', 'X'
};

struct kb_state {
	int row;
	int col;
	u8 mods;
	bool with_mods;
};

static void osd_draw_kb_space(struct kb_state *kbs)
{
	int y = 20 + 4 * 16  + 4;
	bool selected = (kbs->row == 4);

	DrawTextBg(kb_labels[CK_SPACE], 120, y,
		   selected ? COL_BLACK : COL_WHITE,
		   selected ? COL_LTGREEN : COL_BLACK);
}

static void osd_draw_kb_row(int row, struct kb_state *kbs)
{
	int x = 20;
	int y = 20 + row * 16  + 4;

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
			x += strlen(label) * 8 + 4;
		}
		/* right-align the Fx column */
		if (col == KEYS_ROW - 2) {
			x = 320 - 20 - 2*8;
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

static void osd_kb_fixup_col(struct kb_state *kbs)
{
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
	while(true) {
		if (redraw)
			osd_draw_kb(&kbs);
		redraw = true;

		char key = KeyGet();
		u8 kc;
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
			osd_kb_fixup_col(&kbs);
			break;
		case KEY_DOWN:
			if (++kbs.row > 4)
				kbs.row = 0;
			if (kbs.row < 4)
				osd_kb_fixup_col(&kbs);
			break;
		case KEY_UP:
			if (--kbs.row < 0)
				kbs.row = 4;
			if (kbs.row < 4)
				osd_kb_fixup_col(&kbs);
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

	snprintf(buf, sizeof(buf), "Reassigning button %c", btn_labels[btn]);
	DrawClear();
	DrawText(buf, 0, 0, COL_WHITE);
	DrawText("Press:", 0, 16, COL_WHITE);
	DrawText("UP/DOWN/LEFT/RIGHT for joystick direction", 0, 32, COL_WHITE);
	DrawText("A for joystick FIRE", 0, 48, COL_WHITE);
	DrawText("B for no action", 0, 64, COL_WHITE);
	DrawText("X for keyboard key", 0, 80, COL_WHITE);
	DrawText("Y to cancel", 0, 96, COL_WHITE);

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
	DrawTextBg(text, 8*10, 16*(row+1), COL_WHITE, COL_BLACK);
}

static void osd_menu_val_char(char ch, int row)
{
	DrawCharBg(ch, 8*10, 16*(row+1), COL_WHITE, COL_BLACK);
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

static void osd_draw_autorun(int row, int selrow)
{
	osd_menu_name("AUTORUN:", row, selrow);

	osd_menu_val(config.autorun ? "ON" : "OFF", row);
}

static void osd_draw_save_config_game(int row, int selrow)
{
	osd_menu_name("SAVE PER-GAME CONFIG", row, selrow);
}

static void osd_draw_save_config_global(int row, int selrow)
{
	osd_menu_name("SAVE GLOBAL CONFIG", row, selrow);
}

static void osd_draw_single_frame_mode(int row, int selrow)
{
	osd_menu_name("FRM STEP", row, selrow);

	osd_menu_val_char(config.single_frame_mode ? '1' : '0', row);
}

static void osd_action(int row, u8 key)
{
	/* joystick */
	if (row == 0) {
		config.swap_joysticks = !config.swap_joysticks;
		return;
	}
	/* volume */
	if (row == 1) {
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
		return;
	}
	if (row < 5) {
		osd_config_button(row - 2);
		return;
	}
	/* autorun */
	if (row == 5) {
		config.autorun = !config.autorun;
		return;
	}
	/* save game config */
	if (row == 6) {
		config_game_save();
		return;
	}
	/* save global config */
	if (row == 7) {
		config_global_save();
		return;
	}
	if (row == 8) {
		config.single_frame_mode = !config.single_frame_mode;
		return;
	}
}

#define OSD_MENU_MAXROW	8
static void osd_draw_all(int selrow)
{
	DrawClear();
	DrawText("PAUSED: Y - back, X - kbd, B - reboot", 0, 0, COL_WHITE);
	osd_draw_joy(0, selrow);
	osd_draw_vol(1, selrow);
	for (int i = 0; i < CONFIG_BTN_MAX; i++)
		osd_draw_button(2+i, selrow, i);
	osd_draw_autorun(5, selrow);
	osd_draw_save_config_game(6, selrow);
	osd_draw_save_config_global(7, selrow);
	osd_draw_single_frame_mode(8, selrow);
}

static void osd_cleanup(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
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
