#include "../include.h"

#include "osd.h"
#include "pico_dsp.h"
#include "../pico64/cpu.h"

bool osd_active = false;
u8 osd_key_pending = CK_NOKEY;

#define KEYS_ROW	17

/*
static const char * const kb_text[][KEYS_ROW] = {
	{
	"<-", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "+", "-", "Lb", "HOME", "DEL", "F1"
	},
	{
	"CTRL", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "@", "*", "^", "REST", NULL, "F3"
	},
	{
	"RN/SP", "a", "s", "d", "f", "g", "h", "j", "k", "l", ":", ";", "=", "RTRN", NULL, NULL, "F5",
	},
	{
	"C=", "Sft", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "Sft", "DN", "RT", NULL, "F7"
	},
};
*/

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
static const char * const kb_labels[CK_MAX] = {
	"DEL", "RTRN", "RT", "F7", "F1", "F3", "F5", "DN",
	"3", "w", "a", "4", "z", "s", "e", "Sft",
	"5", "r", "d", "6", "c", "f", "t", "x",
	"7", "y", "g", "8", "b", "h", "u", "v",
	"9", "i", "j", "0", "m", "k", "o", "n",
	"+", "p", "l", "-", ".", ":", "@", ",",
	"Lb", "*", ";", "HOME", "Sft", "=", "^", "/",
	"1", "<-", "CTRL", "2", "SPACE", "C=", "q", "RN/SP"
};

static NOINLINE void osd_draw_kb_space(bool selected)
{
	int y = 20 + 4 * 16  + 4;

	DrawTextBg(kb_labels[CK_SPACE], (320 - 5*8) / 2, y,
//		   COL_WHITE, COL_BLACK);
		   selected ? COL_BLACK : COL_WHITE,
		   selected ? COL_LTGREEN : COL_BLACK);
}

static NOINLINE void osd_draw_kb_row(int row, int select)
{
	int x = 20;
	int y = 20 + row * 16  + 4;

	for (int key = 0; key < KEYS_ROW; key++) {
		u8 ck = kb_codes[row][key];
		if (ck != CK_NOKEY) {
			const char *label;
			if (ck < CK_MAX) {
				label = kb_labels[ck];
			} else if (ck == CK_RESTORE) {
				label = "REST";
			} else {
				label = "??";
			}
			DrawTextBg(label, x, y,
				   key == select ? COL_BLACK : COL_WHITE,
				   key == select ? COL_LTGREEN : COL_BLACK);
			x += strlen(label) * 8 + 4;
		}
		/* right-align the Fx column */
		if (key == KEYS_ROW - 2)
			x = 320 - 20 - 2*8;
	}
}

static void osd_draw_kb(int row, int col)
{
	for (int i = 0; i < 4; i++)
		osd_draw_kb_row(i, i == row ? col : -1);
	osd_draw_kb_space(row == 4);
}

void stoprefresh(void);

static u8 osd_kb_getcode(int row, int col)
{
	if (row == 4)
		return CK_SPACE;
	else
		return kb_codes[row][col];
}

/* return true if osd should exit */
static bool osd_start_kb(void)
{
	int row = 0;
	int col = 0;
	bool redraw = true;
	SelFont8x16();
	DrawClear();
	while(true) {
		if (redraw)
			osd_draw_kb(row, col);
		redraw = true;

		char key = KeyGet();
		switch(key) {
		case NOKEY:
			redraw = false;
			break;
		case KEY_RIGHT:
			if (row == 4)
				break;
			col++;
			if (col == KEYS_ROW)
				col = 0;
			else if (kb_codes[row][col] == CK_NOKEY)
				col = KEYS_ROW - 1;
			break;
		case KEY_LEFT:
			if (row == 4)
				break;
			col--;
			if (col < 0)
				col = KEYS_ROW - 1;
			while (kb_codes[row][col] == CK_NOKEY)
				col--;
			break;
		case KEY_DOWN:
			row++;
			if (row > 4)
				row = 0;
			if (row < 4) {
				while (kb_codes[row][col] == CK_NOKEY)
					col--;
			}
			break;
		case KEY_UP:
			row--;
			if (row < 0)
				row = 4;
			if (row < 4) {
				while (kb_codes[row][col] == CK_NOKEY)
					col--;
			}
			break;
		case KEY_X:
			SelFont8x8();
//			stoprefresh();
			return false;
		case KEY_A:
			osd_key_pending = osd_kb_getcode(row, col);
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
