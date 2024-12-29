
#ifndef _OSD_H_
#define _OSD_H_

extern bool osd_active;
extern u8 osd_key_pending;
extern u8 osd_mods_pending;

void osd_start(void);
void draw_key_hints(void);
void draw_fps(u32 lcd, u32 c64);

#endif
