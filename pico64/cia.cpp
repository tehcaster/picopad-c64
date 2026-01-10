/*
	Copyright Frank Bösing, 2017

	This file is part of Teensy64.

    Teensy64 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Teensy64 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Teensy64.  If not, see <http://www.gnu.org/licenses/>.

    Diese Datei ist Teil von Teensy64.

    Teensy64 ist Freie Software: Sie können es unter den Bedingungen
    der GNU General Public License, wie von der Free Software Foundation,
    Version 3 der Lizenz oder (nach Ihrer Wahl) jeder späteren
    veröffentlichten Version, weiterverbreiten und/oder modifizieren.

    Teensy64 wird in der Hoffnung, dass es nützlich sein wird, aber
    OHNE JEDE GEWÄHRLEISTUNG, bereitgestellt; sogar ohne die implizite
    Gewährleistung der MARKTFÄHIGKEIT oder EIGNUNG FÜR EINEN BESTIMMTEN ZWECK.
    Siehe die GNU General Public License für weitere Details.

    Sie sollten eine Kopie der GNU General Public License zusammen mit diesem
    Programm erhalten haben. Wenn nicht, siehe <http://www.gnu.org/licenses/>.

*/

#include "../include.h"

#include "cpu.h"
#include "cia.h"
//#include <string.h>
#include "c64.h"

#define DEBUGCIA1 0
#define RTCDEBUG 0

#define decToBcd(x)	( ( (uint8_t) (x) / 10 * 16) | ((uint8_t) (x) % 10) )
#define bcdToDec(x) ( ( (uint8_t) (x) / 16 * 10) | ((uint8_t) (x) % 16) )

enum rtc_write_what {
	DS,
	S,
	MIN,
	HR,
};

static inline uint32_t rtc_parts_to_ms(uint8_t hr, uint8_t min, uint8_t s,
					uint8_t ds)
{
	uint32_t res = hr;

	res *= 60;
	res += min;

	res *= 60;
	res += s;

	res *= 10;
	res += ds;

	res *= 100;
	return res;
}

static void rtc_alarm_update(struct RTC &RTC)
{
	RTC.alarm_ms = rtc_parts_to_ms(RTC.alarm_hr, RTC.alarm_min, RTC.alarm_s,
			RTC.alarm_ds);
}

static void rtc_time_write(struct RTC &RTC, enum rtc_write_what what, uint8_t val)
{
	uint32_t old = RTC.time_ms;
	uint8_t ds, s, min, hr;

	old /= 100;

	ds = old % 10;
	old /= 10;

	s = old % 60;
	old /= 60;

	min = old % 60;
	old /= 60;

	hr = old;

	switch (what) {
	case DS:
		ds = val;
		break;
	case S:
		s = val;
		break;
	case MIN:
		min = val;
		break;
	case HR:
		hr = val;
		break;
	};

	RTC.time_ms = rtc_parts_to_ms(hr, min, s, ds);
}

static inline void cia_irq(int cia_num)
{
	if (cia_num == 1)
		cpu_irq();
	else
		cpu_nmi();
}

static void cia_write(struct tcia &cia, uint32_t address, uint8_t value, int cia_num)
{
	struct data_port &portA = cia.portA;
	struct data_port &portB = cia.portB;
	struct cia_timer &timerA = cia.timerA;
	struct cia_timer &timerB = cia.timerB;
	struct RTC &RTC = cia.RTC;
	uint8_t tmp;

	address &= 0x0F;

	switch (address) {
	case 0x00:
		portA.data = value;
		if (cia_num == 2) {
		      cpu.vic.bank = ((~value) & 0x03) * 16384;
		      vic_adrchange();
		}
		break;
	case 0x01:
		portB.data = value;
		break;
	case 0x02:
		portA.direction = value;
		break;
	case 0x03:
		portB.direction = value;
		break;
	case 0x04:
		timerA.latch_lo = value;
		break;
	case 0x05:
		timerA.latch_hi = value;
		if (!timerA.running)
			timerA.value_hi = value;
		break;
	case 0x06:
		timerB.latch_lo = value;
		break;
	case 0x07:
		timerB.latch_hi = value;
		if (!timerB.running)
			timerB.value_hi = value;
		break;
	case 0x08: // RTC 10THS
		value &= 0x0f;
		if (value > 9)
			value = 9;

		if (timerB.TOD_set_alarm) {
			RTC.alarm_ds = value;
			rtc_alarm_update(RTC);
		} else {
			rtc_time_write(RTC, DS, value);
			//TODO: wiki says it's only reading that resumes?
			RTC.stopped = false;
		}
		break;
	case 0x09: // RTC SEC
		value &= 0x7f;
		value = bcdToDec(value);
		if (value > 59)
			value = 59;
		if (timerB.TOD_set_alarm) {
			RTC.alarm_s = value;
			rtc_alarm_update(RTC);
		} else {
			rtc_time_write(RTC, S, value);
		}
		break;
	case 0x0A: // RTC MIN
		value &= 0x7f;
		value = bcdToDec(value);
		if (value > 59)
			value = 59;
		if (timerB.TOD_set_alarm) {
			RTC.alarm_min = value;
			rtc_alarm_update(RTC);
		} else {
			rtc_time_write(RTC, MIN, value);
		}
		break;
	case 0x0B: // RTC HR
		tmp = bcdToDec(value & 0x7f);
		if (tmp > 11)
			tmp = 1;
		if (value & 0x80)
			tmp += 12;
		if (timerB.TOD_set_alarm) {
			RTC.alarm_hr = value;
			rtc_alarm_update(RTC);
		} else {
			rtc_time_write(RTC, HR, value);
			RTC.stopped = true;;
		}
		break;
	case 0x0C:
		cia.SDR = value;
		//Fake IRQ (TODO: find documentation about this)
		printf("CIA %d SDR IRQ after setting value %x\n", cia_num, value);
		cia.ICR.int_SDR = 1;
		if (cia.ICR.mask_SDR) {
			cia.ICR.IRQ = 1;
			cia_irq(cia_num);
		}
		break;
	case 0x0D:
		if (value & 0x80) {
			cia.ICR.mask_all |= value;
			if (cia.ICR.int_all & cia.ICR.mask_all) {
				cia.ICR.IRQ = 1;
				cia_irq(cia_num);
			};
		} else {
			cia.ICR.mask_all &= ~value;
		}

		break;
	case 0x0E:
		timerA.control = value;
		if (timerA.load_from_latch) {
			timerA.value = timerA.latch;
			timerA.load_from_latch = 0;
		}
		if (timerA.portB_on)
			printf("UNIMPLEMENTED: cia1 timer A port B enabled");
		break;
	case 0x0F:
		timerB.control = value;
		if (timerB.load_from_latch) {
			timerB.value = timerB.latch;
			timerB.load_from_latch = 0;
		}
		if (timerB.portB_on)
			printf("UNIMPLEMENTED: cia1 timer A port B enabled");
		break;
	default:
		printf("cia_write() unhandled address %x value %x\n", address, value);
		break;
	}

	//printf("CIA1: W %x %x\n", address, value);
#if DEBUGCIA1
	if (cpu.pc < 0xa000) Serial.printf("%x CIA1: W %x %x\n", cpu.pc, address, value);
#endif
}

void cia1_write(uint32_t address, uint8_t value)
{
	cia_write(cpu.cia1, address, value, 1);
}

void cia2_write(uint32_t address, uint8_t value)
{
	cia_write(cpu.cia2, address, value, 2);
}

static uint8_t cia_read(struct tcia &cia, uint32_t address, int cia_num)
{
	struct cia_timer &timerA = cia.timerA;
	struct cia_timer &timerB = cia.timerB;
	struct data_port &portA = cia.portA;
	struct data_port &portB = cia.portB;
	struct RTC &RTC = cia.RTC;

	uint8_t ret;

	address &= 0x0F;

	switch (address) {
	case 0x00:
		if (cia_num == 1)
			ret = cia1PORTA();
		else
			ret = ~portA.direction | (portA.data & portA.direction);
		break;
	case 0x01:
		if (cia_num == 1)
			ret = cia1PORTB();
		else
			ret = ~portB.direction | (portB.data & portB.direction);
		break;
	case 0x02:
		ret = portA.direction;
		break;
	case 0x03:
		ret = portB.direction;
		break;
	case 0x04:
		ret = timerA.value_lo;
		break;
	case 0x05:
		ret = timerA.value_hi;
		break;
	case 0x06:
		ret = timerB.value_lo;
		break;
	case 0x07:
		ret = timerB.value_hi;
		break;
	case 0x08:
		ret = (RTC.time_ms / 100) % 10;
		RTC.stopped = false;
		break;
	case 0x09:
		ret = (RTC.time_ms / 1000) % 60;
		ret = decToBcd(ret);
		break;
	case 0x0A:
		ret = (RTC.time_ms / 60000) % 60;
		ret = decToBcd(ret);
		break;
	case 0x0B:
		ret = RTC.time_ms / 3600000;
		if (ret >= 12)
			ret = decToBcd(ret - 12) | 0x80;
		else
			ret = decToBcd(ret);
		break;
	case 0x0C:
		ret = cia.SDR;
		break;
	case 0x0D:
		ret = cia.ICR.int_raw;
		cia.ICR.int_raw = 0;
		if (cia_num == 1)
			cpu_irq_clear();
		else
			cpu_nmi_clear();
		break;
	case 0x0E:
		ret = timerA.control;
		break;
	case 0x0F:
		ret = timerB.control;
		break;
	default:
		printf("cia_read() unhandled address %x\n", address);
		break;
	}

	//printf("CIA1: R %x %x\n", address, ret);
#if DEBUGCIA1
	if (cpu.pc < 0xa000) Serial.printf("%x CIA1: R %x %x\n", cpu.pc, address, ret);
#endif
	return ret;
}

uint8_t cia1_read(uint32_t address)
{
	return cia_read(cpu.cia1, address, 1);
}

uint8_t cia2_read(uint32_t address)
{
	return cia_read(cpu.cia2, address, 2);
}

static void __cia_clock(struct tcia &cia, int clk, int cia_num)
{
	struct cia_timer &timerA = cia.timerA;
	struct cia_timer &timerB = cia.timerB;

	int32_t t;
	static int tape_clk = 0;
	bool timerA_underflow = false;

tape_retry:
	if (tape_running && cia_num == 1) {
		tape_clk -= clk;
		if (tape_clk < 0) {
			cia.ICR.int_FLAG = 1;
			tape_clk += tape_next_pulse();
			if (tape_clk < 0 && tape_running) {
				printf("tape clk underflow: %d\n", tape_clk);
				goto tape_retry;
			}
		}
	}

	// TIMER A
	if (timerA.running && !timerA.count_CNT) {
		t = timerA.value;

		if (clk > t) { //underflow ?
			/*
			 * TODO: should we reset to latch if there's one-shot? if not,
			 * should the value be 0xffff?
			 */
			t = timerA.latch - (clk - t);
			cia.ICR.int_timerA = 1;
			timerA_underflow = true;

			if (timerA.underflow_stop)
				timerA.running = 0;
		} else {
			t -= clk;
		}

		timerA.value = t;
	}


	// TIMER B
	if (timerB.running && !timerB.count_CNT) {
		int timerB_clk = clk;

		if (timerB.count_timerA) {
			if (timerA_underflow)
				timerB_clk = 1;
			else
				goto tend;
		}

		t = timerB.value;

		if (timerB_clk > t) { //underflow ?
			/* TODO: same as for timerA above */
			t = timerB.latch - (timerB_clk - t);
			cia.ICR.int_timerB = 1;

			if (timerB.underflow_stop)
				timerB.running = 0;
		} else {
			t -= timerB_clk;
		}
		timerB.value = t;
	}

tend:
	// INTERRUPT ?
	if (cia.ICR.int_all & cia.ICR.mask_all) {
		cia.ICR.IRQ = 1;
		cia_irq(cia_num);
	}
}

void cia_clock(void)
{
	__cia_clock(cpu.cia1, 1, 1);
	__cia_clock(cpu.cia2, 1, 2);
}

void cia_clockt(int ticks)
{
	__cia_clock(cpu.cia1, ticks, 1);
	__cia_clock(cpu.cia2, ticks, 2);
}


static void __cia_rtc_frame_update(struct tcia &cia, int cia_num)
{
	struct RTC &RTC = cia.RTC;

	if (RTC.stopped)
		return;

	/* TODO: make it more precise */

	if (RTC.time_ms < RTC.alarm_ms && RTC.time_ms + 20 >= RTC.alarm_ms) {
alarm:
		printf("CIA %d RTC interrupt\n", cia_num);
		RTC.time_ms = RTC.alarm_ms;
		cia.ICR.int_TOD = 1;
		if (cia.ICR.mask_TOD) {
			cia.ICR.IRQ = 1;
			cia_irq(cia_num);
		}
		return;
	}

	RTC.time_ms += 20;
	if (RTC.time_ms >= 24 * 3600 * 1000) {
		RTC.time_ms = 0;
		if (RTC.alarm_ms == 0)
			goto alarm;
	}

}

void cia_rtc_frame_update(void)
{
	__cia_rtc_frame_update(cpu.cia1, 1);
	__cia_rtc_frame_update(cpu.cia2, 2);
}

/*
void cia1_checkRTCAlarm() { // call @ 1/10 sec interval minimum

	if ((int)(millis() - cpu.cia1.TOD) % 86400000l/100 == cpu.cia1.TODAlarm) {
		printf("CIA1 RTC interrupt\n");
		cpu.cia1.ICR.int_TOD = 1;
		if (cpu.cia1.ICR.mask_TOD) {
			cpu.cia1.ICR.IRQ = 1;
			cpu_irq();
		}
	}
}

void cia1FLAG(void) {
	//Serial.println("CIA1 FLAG interrupt");
	cpu.cia1.R[13] |= 0x10 | ((cpu.cia1.W[13] & 0x10) << 3);
	if (cpu.cia1.R[0x0d] | 0x80)
		cpu_irq();
}
*/
void resetCia1(void) {
	memset(&cpu.cia1, 0, sizeof(cpu.cia1));
	cpu.cia1.timerA.value = cpu.cia1.timerA.latch = 0xffff;
	cpu.cia1.timerB.value = cpu.cia1.timerB.latch = 0xffff;

	//FLAG pin CIA1 - Serial SRQ (input only)
    //pinMode(PIN_SERIAL_SRQ, OUTPUT_OPENDRAIN);
	//digitalWriteFast(PIN_SERIAL_SRQ, 1);
	//attachInterrupt(digitalPinToInterrupt(PIN_SERIAL_SRQ), cia1FLAG, FALLING);
}

void resetCia2(void) {
	memset(&cpu.cia2, 0, sizeof(cpu.cia2));
	cpu.cia2.timerA.value = cpu.cia2.timerA.latch = 0xffff;
	cpu.cia2.timerB.value = cpu.cia2.timerB.latch = 0xffff;
}

