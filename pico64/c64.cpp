#include "../include.h"
#include "../display/osd.h"
#include "c64.h"
#include "Teensy64.h"

#ifdef HAS_SND
#include "reSID.h"
AudioPlaySID playSID;
#endif

using namespace std;

/* IRAM_ATTR */
static void oneRasterLine(void) {
  static unsigned short lc = 1;

  while (true) {

    cpu.lineStartTime = fbmicros(); //get_ccount();
    cpu.lineCycles = cpu.lineCyclesAbs = 0;

    if (!cpu.exactTiming) {
      vic_do();
    } else {
      vic_do_simple();
    }

    if (--lc == 0) {
      lc = LINEFREQ / 10; // 10Hz
      cia1_checkRTCAlarm();
      cia2_checkRTCAlarm();
    }

    //Switch "ExactTiming" Mode off after a while:
    if (!cpu.exactTiming) break;
    if ( (fbmicros() - cpu.exactTimingStartTime)*1000 >= EXACTTIMINGDURATION ) {
      cpu_disableExactTiming();
      break;
    }
  };

}

const u8 ascii2scan[] = {
 //0 1 2 3 4 5 6 7 8 9 A B C D E F
   0,0,0,0,0,0,0,0,0,0,0,0,0,CK_RETURN,0,0, // return
 //     17:down                                                     29:right
   0,CK_CRSR_DN,0,0,0,0,0,0,0,0,0,0,0,CK_CRSR_RT,0,0,
   //sp           !         "           #           $          %            &           '          (          )            *            +        ,        -         .          /
   CK_SPACE, CKSH(CK_1), CKSH(CK_2), CKSH(CK_3), CKSH(CK_4), CKSH(CK_5), CKSH(CK_6), CKSH(CK_7), CKSH(CK_8), CKSH(CK_9), CK_ASTERISK, CK_PLUS, CK_COMMA, CK_MINUS, CK_PERIOD, CK_SLASH,
   //0    1      2     3     4     5     6     7     8     9       :           ;         <            =          >                 ?
   CK_0, CK_1, CK_2, CK_3, CK_4, CK_5, CK_6, CK_7, CK_8, CK_9, CK_COLON, CK_SEMICOL, CKSH(CK_COMMA), CK_EQUAL, CKSH(CK_PERIOD), CKSH(CK_SLASH),
   //@      A     B     C     D    E     F     G     H     I     J     K     L     M     N     O
   CK_AT, CK_A, CK_B, CK_C, CK_D, CK_E, CK_F, CK_G, CK_H, CK_I, CK_J, CK_K, CK_L, CK_M, CK_N, CK_O,
   //P     Q     R     S     T     U     V     W     X     Y     Z       [           \       ]              ^       _
   CK_P, CK_Q, CK_R, CK_S, CK_T, CK_U, CK_V, CK_W, CK_X, CK_Y, CK_Z, CKSH(CK_COLON), 0, CKSH(CK_SEMICOL), CK_CARET, 0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // ' a b c d e f g h i j k l m n o
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,CK_DELETE, // p q r s t u v w x y z { | } ~ DEL
 //? ?      133:f1      f2        f3      f4          f5        f6        f7     f8
   0,0,0,0,0,CK_F1, CKSH(CK_F1), CK_F3, CKSH(CK_F3), CK_F5, CKSH(CK_F5), CK_F7, CKSH(CK_F7), 0,0,0,  // 128-143
 //    145:up                                                                  157:left
   0,CKSH(CK_CRSR_DN),0,0,0,0,0,0,0,0,0,0,0,CKSH(CK_CRSR_RT),0,0   // 144-159
};

static struct {
  u8 portA;
  u8 portB;
  u8 mods;
} kbdData = {0, 0, 0};

const struct button_layout default_button_layout = {
	.buttons = {
		{
			.mode = CONFIG_BTN_MODE_JOY,
			.joy = CJ_FIRE,
		},
		{
			.mode = CONFIG_BTN_MODE_KEY,
			.key = CK_SPACE,
		},
		{
			.mode = CONFIG_BTN_MODE_OFF
		},
		{
			.mode = CONFIG_BTN_MODE_JOY,
			.joy = CJ_UP,
		},
		{
			.mode = CONFIG_BTN_MODE_JOY,
			.joy = CJ_LEFT,
		},
		{
			.mode = CONFIG_BTN_MODE_JOY,
			.joy = CJ_RIGHT,
		},
		{
			.mode = CONFIG_BTN_MODE_JOY,
			.joy = CJ_DOWN,
		},
	},
};

struct emu_config config = { };

static void setKey(u8 ck, u8 mods)
{
	kbdData.portA = CK_GET_PORT_A(ck);
	kbdData.portB = CK_GET_PORT_B(ck);

	if (CK_HAS_SHIFT(ck))
		mods |= CK_MOD_LSHIFT;
	kbdData.mods = mods;
}

static void unsetKey()
{
	kbdData.portA = 0;
	kbdData.portB = 0;
	kbdData.mods = 0;
}

struct button_kbd_data {
	bool joy;
	union {
		u8 portA;
		u8 portJoy;
	};
	u8 portB;
	u8 pico_key;
	u8 pico_gpio;
};

struct button_joy_data {
	u8 portJoy;
	u8 pico_key;
	u8 pico_gpio;
};

static struct button_kbd_data bkd[CONFIG_BTN_MAX];
static u8 bkd_used = 0;

static struct button_kbd_data bjd[CONFIG_BTN_MAX];
static u8 bjd_used = 0;

static const u8 btn_idx_to_key[CONFIG_BTN_MAX] =
	{ KEY_A, KEY_B, KEY_X, KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_DOWN};

static const u8 btn_idx_to_gpio[CONFIG_BTN_MAX] =
	{ BTN_A_PIN, BTN_B_PIN, BTN_X_PIN, BTN_UP_PIN, BTN_LEFT_PIN,
	  BTN_RIGHT_PIN, BTN_DOWN_PIN };

void apply_button_config() {
	struct button_layout *layout = &config.layouts[config.button_layout];

	bkd_used = 0;
	bjd_used = 0;

	for (int i = 0; i < CONFIG_BTN_MAX; i++) {
		struct button_config *cfg = &layout->buttons[i];

		if (cfg->mode == CONFIG_BTN_MODE_OFF)
			continue;

		if (cfg->mode == CONFIG_BTN_MODE_JOY) {
			bjd[bjd_used].portJoy = ~CJ_GET_PORT(cfg->joy);
			bjd[bjd_used].pico_key = btn_idx_to_key[i];
			bjd[bjd_used].pico_gpio = btn_idx_to_gpio[i];
			bjd_used++;
		} else {
			u8 ck = cfg->key;
			bkd[bkd_used].portA = CK_GET_PORT_A(ck);
			bkd[bkd_used].portB = CK_GET_PORT_B(ck);
			bkd[bkd_used].pico_key = btn_idx_to_key[i];
			bkd[bkd_used].pico_gpio = btn_idx_to_gpio[i];
			bkd_used++;
		}
	}
}

static bool input_blocked = false;

uint8_t cia1PORTA(void) {

	uint8_t v;
	uint8_t filter;

	v = ~cpu.cia1.R[0x02] | (cpu.cia1.R[0x00] & cpu.cia1.R[0x02]);
	filter = ~cpu.cia1.R[0x01] & cpu.cia1.R[0x03];

	if (input_blocked)
		goto nobuttons;

	if (!config.swap_joysticks)
		goto nojoy;

	for (int i = 0; i < bjd_used; i++) {
		if (!GPIO_In(bjd[i].pico_gpio))
			v &= bjd[i].portJoy;
	}

nojoy:
	for (int i = 0; i < bkd_used; i++) {
		if (GPIO_In(bkd[i].pico_gpio))
			continue;

		if (bkd[i].portB & filter)
			v &= ~bkd[i].portA;
	}

nobuttons:

	if (kbdData.portB & filter)
		v &= ~kbdData.portA;

	if (!kbdData.mods)
		return v;

	if ((kbdData.mods & CK_MOD_LSHIFT) && (CK_LSHIFT_PORTB & filter))
		v &= ~CK_LSHIFT_PORTA;
	if ((kbdData.mods & CK_MOD_RSHIFT) && (CK_RSHIFT_PORTB & filter))
		v &= ~CK_RSHIFT_PORTA;
	if ((kbdData.mods & CK_MOD_CMDR) && (CK_CMDR_PORTB & filter))
		v &= ~CK_CMDR_PORTA;
	if ((kbdData.mods & CK_MOD_CONTROL) && (CK_CONTROL_PORTB & filter))
		v &= ~CK_CONTROL_PORTA;

	return v;
}


uint8_t cia1PORTB(void) {

	uint8_t v;
	uint8_t filter;

	v = ~cpu.cia1.R[0x03] | (cpu.cia1.R[0x00] & cpu.cia1.R[0x02]);
	filter = ~cpu.cia1.R[0x00] & cpu.cia1.R[0x02];

	if (input_blocked)
		goto nobuttons;

	if (config.swap_joysticks)
		goto nojoy;

	for (int i = 0; i < bjd_used; i++) {
		if (!GPIO_In(bjd[i].pico_gpio))
			v &= bjd[i].portJoy;
	}

nojoy:
	for (int i = 0; i < bkd_used; i++) {
		if (GPIO_In(bkd[i].pico_gpio))
			continue;

		if (bkd[i].portA & filter)
			v &= ~bkd[i].portB;
	}

nobuttons:

	if (kbdData.portA & filter)
		v &= ~kbdData.portB;

	if (!kbdData.mods)
		return v;

	if ((kbdData.mods & CK_MOD_LSHIFT) && (CK_LSHIFT_PORTA & filter))
		v &= ~CK_LSHIFT_PORTB;
	if ((kbdData.mods & CK_MOD_RSHIFT) && (CK_RSHIFT_PORTA & filter))
		v &= ~CK_RSHIFT_PORTB;
	if ((kbdData.mods & CK_MOD_CMDR) && (CK_CMDR_PORTA & filter))
		v &= ~CK_CMDR_PORTB;
	if ((kbdData.mods & CK_MOD_CONTROL) && (CK_CONTROL_PORTA & filter))
		v &= ~CK_CONTROL_PORTB;

	return v;
}


void c64_Init(void)
{
  disableEventResponder();
  resetPLA();
  resetCia1();
  resetCia2();
  resetVic();
  cpu_reset();
#ifdef HAS_SND  
  playSID.begin();  
#endif  
}


void c64_Step(void)
{
	oneRasterLine();
}

static const char * textload = "\r\t\tLOAD\"\"\r\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tRUN\r";

//#define DEBUG 1

#ifdef DEBUG
static const char * digits = "0123456789ABCDEF";
static char buf[5] = {0,0,0,0,0};
#endif

bool load_run_pending = true;

void c64_Input()
{
	static bool toggle = true;
	static const char * textseq = NULL;
	static int osd_key_timeout = 0;
	struct button_layout *layout = &config.layouts[config.button_layout];

	if (load_run_pending && config.autorun) {
		load_run_pending = false;
		textseq = textload;
		input_blocked = true;
		return;
	}

	if (osd_key_timeout) {
		osd_key_timeout--;
		if (!osd_key_timeout)
			unsetKey();
		return;
	}

	if (osd_key_pending != CK_NOKEY) {
		setKey(osd_key_pending, osd_mods_pending);
		osd_key_timeout = 2;
		osd_key_pending = CK_NOKEY;
		return;
	}

	if (textseq) {
		const char k = *textseq;
		if (k != '\t') {
			if (toggle) {
				setKey(ascii2scan[k], 0);
			} else {
				unsetKey();
			}
		}
		if (!toggle) {
			textseq++;
			if (!*textseq) {
				textseq = NULL;
				input_blocked = false;
			}
			toggle = true;
		} else {
			toggle = false;
		}
		return;
	}

	if (KeyPressed(KEY_B)) {
		if (load_run_pending) {
			load_run_pending = false;
			textseq = textload;
			input_blocked = true;
			draw_key_hints();
		}
		return;
        }

	for (int i = 0; i < CONFIG_BTN_MAX; i++) {
		struct button_config *cfg = &layout->buttons[i];

		if (cfg->mode == CONFIG_BTN_MODE_LAYOUT) {
			if (KeyPressed(btn_idx_to_key[i])) {
				config.button_layout = cfg->layout;
				apply_button_config();
				draw_key_hints();
			}
		}
	}

	if (KeyPressed(KEY_Y)) {
		osd_active = true;
		return;
	}
}

