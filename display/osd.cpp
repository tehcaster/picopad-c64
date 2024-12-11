#include "../include.h"

#include "osd.h"
#include "pico_dsp.h"
#include "../pico64/cpu.h"

bool osd_active = false;

static void osd_draw_vol(void)
{
	int vol = ConfigGetVolume();
	char buf[50];

	snprintf(buf, sizeof(buf), "VOLUME: %3d %% (UP / DOWN)", vol * 10);

	DrawTextBg(buf, 0, 16, COL_WHITE, COL_BLACK);
}

static void osd_draw_joy(void)
{
	char buf[50];

	snprintf(buf, sizeof(buf), "JOYSTICK: %d (A to change)", cpu.swapJoysticks);

	DrawTextBg(buf, 0, 32, COL_WHITE, COL_BLACK);
}

void osd_start(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
	DrawText("PAUSED", 0, 0, COL_WHITE);
	osd_draw_vol();
	osd_draw_joy();

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
		case KEY_B:
			ResetToBootLoader();
		case KEY_Y:
			DrawClear();
			return;
		default:
			;
		}
	}
}
