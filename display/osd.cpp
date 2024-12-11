#include "../include.h"

#include "osd.h"
#include "pico_dsp.h"

bool osd_active = false;

static void osd_draw_vol(void)
{
	int vol = ConfigGetVolume();
	char buf[50];

	snprintf(buf, sizeof(buf), "VOLUME: %3d %% (UP / DOWN)", vol * 10);

	DrawTextBg(buf, 0, 16, COL_WHITE, COL_BLACK);
}

void osd_start(void)
{
	KeyWaitNoPressed();
	KeyFlush();
	DrawClear();
	DrawText("PAUSED", 0, 0, COL_WHITE);
	osd_draw_vol();

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
