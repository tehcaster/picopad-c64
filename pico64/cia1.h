#ifndef Teensy64_cia1_h_
#define Teensy64_cia1_h_

struct cia_timer {
	union {
		uint16_t value;
		struct {
			uint8_t value_lo;
			uint8_t value_hi;
		};
	};
	union {
		uint16_t latch;
		struct {
			uint8_t latch_lo;
			uint8_t latch_hi;
		};
	};
	union {
		uint8_t control;
		// Timer A
		struct {
			uint8_t running: 1, portB_on: 1, portB_mode: 1, underflow_stop: 1,
				load_from_latch: 1, count_CNT: 1, SP_output: 1, RTC_Hz: 1;
		};
		// Timer B
		struct {
			uint8_t : 6, count_timerA: 1, TOD_set_alarm: 1;
		};
	};
};

struct data_port {
	uint8_t data;
	uint8_t direction;
};

struct ICR {
	union {
		uint8_t int_raw;
		struct {
			uint8_t int_timerA: 1, int_timerB: 1, int_TOD: 1,
				int_SDR: 1, int_FLAG: 1, : 2, IRQ: 1;
		};
		struct {
			uint8_t int_all: 5, : 3;
		};
	};
	union {
		uint8_t mask_raw;
		struct {
			uint8_t mask_timerA: 1, mask_timerB: 1, mask_TOD: 1,
				mask_SDR: 1, mask_FLAG: 1, : 3;
		};
		struct {
			uint8_t mask_all: 5, : 3;
		};
	};
};

struct RTC {
	uint32_t time_ms;
	uint32_t alarm_ms;

	uint8_t alarm_ds;
	uint8_t alarm_s;
	uint8_t alarm_min;
	uint8_t alarm_hr;

	bool stopped;
};

struct tcia {
	union {
		uint8_t R[0x10];
		uint16_t R16[0x10/2];
		uint32_t R32[0x10/4];
	};
	union {
		uint8_t W[0x10];
		uint16_t W16[0x10/2];
		uint32_t W32[0x10/4];
	};
	struct data_port portA;
	struct data_port portB;
	struct cia_timer timerA;
	struct cia_timer timerB;
	struct ICR ICR;
	struct RTC RTC;
	int32_t TOD;
	int32_t TODfrozenMillis;
	int32_t TODAlarm;
	uint8_t TODstopped;
	uint8_t TODfrozen;
	uint8_t SDR;
};


void cia1_write(uint32_t address, uint8_t value) __attribute__ ((hot));
void cia2_write(uint32_t address, uint8_t value) __attribute__ ((hot));

uint8_t cia1_read(uint32_t address) __attribute__ ((hot));
uint8_t cia2_read(uint32_t address) __attribute__ ((hot));

void cia_rtc_frame_update(void);
void resetCia1(void);
void resetCia2(void);




#endif
