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
#include "cia1.h"
//#include <string.h>
#include "c64.h"

#define DEBUGCIA1 0
#define RTCDEBUG 0

#define decToBcd(x)	( ( (uint8_t) (x) / 10 * 16) | ((uint8_t) (x) % 10) )
#define bcdToDec(x) ( ( (uint8_t) (x) / 16 * 10) | ((uint8_t) (x) % 16) )
#define tod()		(cpu.cia1.TODfrozen?cpu.cia1.TODfrozenMillis:(int)((millis() - cpu.cia1.TOD) % 86400000l))

void cia1_setAlarmTime() {
	cpu.cia1.TODAlarm = (cpu.cia1.W[0x08] + cpu.cia1.W[0x09] * 10l + cpu.cia1.W[0x0A] * 600l + cpu.cia1.W[0x0B] * 36000l);
}

void cia1_write(uint32_t address, uint8_t value)
{
	struct tcia &cia1 = cpu.cia1;
	struct cia_timer &timerA = cia1.timerA;
	struct cia_timer &timerB = cia1.timerB;

	address &= 0x0F;

	switch (address) {
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
		//RTC
		case 0x08 : {
					if (timerB.TOD_set_alarm) {
						value &= 0x0f;
						cpu.cia1.W[address] = value;
						cia1_setAlarmTime();

						#if RTCDEBUG
						Serial.print("CIA 1 Set Alarm TENTH:");
						Serial.println(value,HEX);
						#endif

					} else {
						value &= 0x0f;
						cpu.cia1.TODstopped=0;
						//Translate set Time to TOD:
						cpu.cia1.TOD = (int)(millis() % 86400000l) - (value * 100 + cpu.cia1.R[0x09] * 1000l + cpu.cia1.R[0x0A] * 60000l + cpu.cia1.R[0x0B] * 3600000l);
						#if RTCDEBUG
						Serial.print("CIA 1 Set TENTH:");
						Serial.println(value,HEX);
						Serial.print("CIA 1 TOD (millis):");
						Serial.println(cpu.cia1.TOD);
						#endif
					}
					};
					break; //TOD-Tenth
		case 0x09 : {
					if (timerB.TOD_set_alarm) {
						cpu.cia1.W[address] = bcdToDec(value);
						cia1_setAlarmTime();
						#if RTCDEBUG
						Serial.print("CIA 1 Set Alarm SEC:");
						Serial.println(value,HEX);
						#endif

					} else {
						cpu.cia1.R[address] = bcdToDec(value);
						#if RTCDEBUG
						Serial.print("CIA 1 Set SEC:");
						Serial.println(value,HEX);
						#endif

					}
					};
					break; //TOD-Secs
		case 0x0A : {
					if (timerB.TOD_set_alarm) {
						cpu.cia1.W[address] = bcdToDec(value);
						cia1_setAlarmTime();
						#if RTCDEBUG
						Serial.print("CIA 1 Set Alarm MIN:");
						Serial.println(value,HEX);
						#endif

					} else {
						cpu.cia1.R[address] = bcdToDec(value);
						#if RTCDEBUG
						Serial.print("CIA 1 Set MIN:");
						Serial.println(value,HEX);
						#endif

					}
					};break; //TOD-Minutes
		case 0x0B : {
					if (timerB.TOD_set_alarm) {
						cpu.cia1.W[address] = bcdToDec(value & 0x1f) + (value & 0x80?12:0);
						cia1_setAlarmTime();
						#if RTCDEBUG
						Serial.print("CIA 1 Set Alarm HRS:");
						Serial.println(value,HEX);
						#endif

					} else {
						cpu.cia1.R[address] = bcdToDec(value & 0x1f) + (value & 0x80?12:0);
						cpu.cia1.TODstopped=1;
						#if RTCDEBUG
						Serial.print("CIA 1 Set HRS:");
						Serial.println(value,HEX);
						#endif
					}
					};break; //TOD-Hours

		case 0x0C : {
					cpu.cia1.R[address] = value;
					//Fake IRQ
					cpu.cia1.R[0x0d] |= 8 | ((cpu.cia1.W[0x0d] & 0x08) << 4);
					if (cpu.cia1.R[0x0d] | 0x80)
						cpu_irq();
					}
					;break;
		case 0x0D : {
					if ((value & 0x80)>0) {
						cpu.cia1.W[address] |= value & 0x1f;
						//ggf IRQ triggern
						if (cpu.cia1.R[address] & cpu.cia1.W[address] & 0x1f) {
							cpu.cia1.R[address] |= 0x80;
							cpu_irq();
						};
					} else {
						cpu.cia1.W[address] &= ~value;
					}

					};
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
		cpu.cia1.R[address] = value;
		/*if (address ==0) {Serial.print(value);Serial.print(" ");}*/
		break;
	}

	//printf("CIA1: W %x %x\n", address, value);
#if DEBUGCIA1
	if (cpu.pc < 0xa000) Serial.printf("%x CIA1: W %x %x\n", cpu.pc, address, value);
#endif
}

uint8_t cia1_read(uint32_t address) {
	struct tcia &cia1 = cpu.cia1;
	struct cia_timer &timerA = cia1.timerA;
	struct cia_timer &timerB = cia1.timerB;

	uint8_t ret;

	address &= 0x0F;

	switch (address) {
		case 0x00: {ret = cia1PORTA();};break;
		case 0x01: {ret = cia1PORTB();};break;
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
		//RTC
		case 0x08: {
					ret = tod() % 1000 / 10;
					cpu.cia1.TODfrozen = 0;
					};

					#if RTCDEBUG
					Serial.print("CIA 1 Read TENTH:");
					Serial.println(ret,HEX);
					#endif

					break;	//Bit 0..3: Zehntelsekunden im BCD-Format ($0-$9) Bit 4..7: immer 0
		case 0x09: {
					ret = decToBcd(tod() / 1000 % 60);
					};
					//Serial.println( tod() / 100);
					#if RTCDEBUG
					Serial.print("CIA 1 Read SEC:");
					Serial.println(ret,HEX);
					#endif

					break;				//Bit 0..3: Einersekunden im BCD-Format ($0-$9) Bit 4..6: Zehnersekunden im BCD-Format ($0-$5) Bit 7: immer 0
		case 0x0A: {
					ret = decToBcd(tod() / (1000 * 60) % 60);
					};
					#if RTCDEBUG
					Serial.print("CIA 1 Read MIN:");
					Serial.println(ret,HEX);
					#endif

					break;		//Bit 0..3: Einerminuten im BCD-Format( $0-$9) Bit 4..6: Zehnerminuten im BCD-Format ($0-$5) Bit 7: immer 0
		case 0x0B: {
					//Bit 0..3: Einerstunden im BCD-Format ($0-$9) Bit 4: Zehnerstunden im BCD-Format ($0-$1) // Bit 7: Unterscheidung AM/PM, 0=AM, 1=PM
					//Lesen aus diesem Register friert alle TOD-Register ein (TOD läuft aber weiter), bis Register 8 (TOD 10THS) gelesen wird.
					cpu.cia1.TODfrozen = 0;
					cpu.cia1.TODfrozenMillis = tod();
					cpu.cia1.TODfrozen = 1;
				   	#if RTCDEBUG
					Serial.print("CIA 1 FrozenMillis:");
					Serial.println(cpu.cia1.TODfrozenMillis);
					#endif
					ret = cpu.cia1.TODfrozenMillis / (1000 * 3600) % 24;
					if (ret>=12)
						ret = 128 | decToBcd(ret - 12);
					else
						ret = decToBcd(ret);
				    };
				   	#if RTCDEBUG
					Serial.print("CIA 1 Read HRS:");
					Serial.println(ret,HEX);
					#endif

				   break;

		case 0x0D: {ret = cpu.cia1.R[address] & 0x9f;
					cpu.cia1.R[address]=0;
					cpu_irq_clear();
					};
					break;
	case 0x0E:
		ret = timerA.control;
		break;
	case 0x0F:
		ret = timerB.control;
		break;
	default:
		ret = cpu.cia1.R[address];
		break;
	}

	//printf("CIA1: R %x %x\n", address, ret);
#if DEBUGCIA1
	if (cpu.pc < 0xa000) Serial.printf("%x CIA1: R %x %x\n", cpu.pc, address, ret);
#endif
	return ret;
}

void cia1_clock(int clk)
{
	struct tcia &cia1 = cpu.cia1;
	struct cia_timer &timerA = cia1.timerA;
	struct cia_timer &timerB = cia1.timerB;

	int32_t t;
	uint8_t ICR = cia1.R[0xD];
	static int tape_clk = 0;
	bool timerA_underflow = false;


tape_retry:
	if (tape_running) {
		tape_clk -= clk;
		if (tape_clk < 0) {
			ICR |= 0x10;
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
			ICR |= 0x01;
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
			ICR |= 0x02;

			if (timerB.underflow_stop)
				timerB.running = 0;
		} else {
			t -= timerB_clk;
		}
		timerB.value = t;
	}

tend:
	// INTERRUPT ?
	if (ICR & 0x1f & cia1.W[0x0D]) {
		ICR |= 0x80;
		cia1.R[0x0D] = ICR;
		cpu_irq();
	} else {
		cia1.R[0x0D] = ICR;
	}
}

void cia1_checkRTCAlarm() { // call @ 1/10 sec interval minimum

	if ((int)(millis() - cpu.cia1.TOD) % 86400000l/100 == cpu.cia1.TODAlarm) {
		//Serial.print("CIA1 RTC interrupt");
		cpu.cia1.R[13] |= 0x4 | ((cpu.cia1.W[13] & 4) << 5);
		if (cpu.cia1.R[0x0d] | 0x80)
			cpu_irq();
	}
}
/*
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


