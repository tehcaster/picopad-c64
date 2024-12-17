extern void c64_Init(void);
extern void c64_Step(void);
extern void c64_Start(char * filename);
extern void c64_Input();

extern u32 nFramesC64;
extern u32 timeSWISR;

struct emu_config {
	bool swap_joysticks;
};

extern struct emu_config config;
