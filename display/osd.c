#include "../include.h"

#include "osd.h"

bool osd_active = false;

void osd_start(void)
{
	KeyFlush();
	DrawClear();
	DrawText("PAUSED", 0, 0, COL_WHITE);

	while(true) {
		char key = KeyGet();

		if (key == KEY_Y)
			return;

		if (key == KEY_B)
			ResetToBootLoader();
	}
}
