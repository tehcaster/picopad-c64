#include "../include.h"

#include "osd.h"
#include "pico_dsp.h"
#include "../pico64/cpu.h"

bool osd_active = false;
u8 osd_key_pending = CK_NOKEY;
u8 osd_mods_pending = 0;

#define KEYS_ROW	17

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

struct kb_state {
	int row;
	int col;
	u8 mods;
};

static NOINLINE void osd_draw_kb_space(struct kb_state *kbs)
{
	int y = 20 + 4 * 16  + 4;
	bool selected = (kbs->row == 4);

	DrawTextBg(kb_labels[CK_SPACE], (320 - 5*8) / 2, y,
//		   COL_WHITE, COL_BLACK);
		   selected ? COL_BLACK : COL_WHITE,
		   selected ? COL_LTGREEN : COL_BLACK);
}

static NOINLINE void osd_draw_kb_row(int row, struct kb_state *kbs)
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

			fg = selected ? COL_BLACK : COL_WHITE;
			bg = selected ? COL_LTGREEN : COL_BLACK;
			if (mod_active) {
				COLTYPE tmp = fg;
				fg = bg;
				bg = tmp;
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

void stoprefresh(void);

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

/* return true if osd should exit */
static bool osd_start_kb(void)
{
	struct kb_state kbs = { };
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
			SelFont8x8();
//			stoprefresh();
			return false;
		case KEY_A:
			kc = osd_kb_get_code(&kbs);
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

			osd_key_pending = kc;
			osd_mods_pending = kbs.mods;
			return true;
		default:
			;
		}
	}
}

static void osd_draw_vol(void)
{
	int vol = ConfigGetVolume();
	char buf[50];

	snprintf(buf, sizeof(buf), "VOLUME: %3d %% (UP / DOWN) %s", vol * 10);

	DrawTextBg(buf, 0, 16, COL_WHITE, COL_BLACK);
}

static void osd_draw_joy(void)
{
	char buf[50];

	snprintf(buf, sizeof(buf), "JOYSTICK: %d (A to change)", cpu.swapJoysticks);

	DrawTextBg(buf, 0, 32, COL_WHITE, COL_BLACK);
}

static void osd_draw_all(void)
{
	DrawClear();
	DrawText("PAUSED", 0, 0, COL_WHITE);
	osd_draw_vol();
	osd_draw_joy();
}

static void osd_cleanup(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
}

void osd_start(void)
{
	osd_draw_all();
	KeyWaitNoPressed();
	KeyFlush();

	while(true) {
		char key = KeyGet();

		switch(key) {
		case KEY_UP:
			ConfigIncVolume();
			audio_vol_update();
			osd_draw_vol();
			break;
		case KEY_DOWN:
			ConfigDecVolume();
			audio_vol_update();
			osd_draw_vol();
			break;
		case KEY_A:
			cpu.swapJoysticks = !cpu.swapJoysticks;
			osd_draw_joy();
			break;
		case KEY_X:
			if (osd_start_kb()) {
				osd_cleanup();
				return;
			}
			osd_draw_all();
			break;
		case KEY_B:
			ResetToBootLoader();
		case KEY_Y:
			osd_cleanup();
			return;
		default:
			;
		}
	}
}
