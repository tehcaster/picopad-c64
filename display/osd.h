
#ifndef _OSD_H_
#define _OSD_H_

extern bool osd_active;
extern u8 osd_key_pending;
extern u8 osd_mods_pending;

#define FILES_PER_PAGE 12

struct osd_filelist {
	int nr_files;
	int selected;
	int page;
	int pages;
	u8 action;
	char tape_name[25];
	char names[FILES_PER_PAGE][17];
};

void osd_start(void);
bool osd_tape_file_select_start(struct osd_filelist *osd_filelist);
void draw_key_hints(void);
void draw_fps(u32 lcd, u32 c64);

#endif
