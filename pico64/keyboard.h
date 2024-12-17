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

#ifndef Teensy64_keyboard_h_
#define Teensy64_keyboard_h_

#define CK(port1, port2)	((port2) | (port1) << 3)
#define CKSH(ck)		(BIT(6)|ck)
#define CK_MAX			64

#define CK_GET_PORT_A(ck)	BIT(((ck >> 3) & 0x07))
#define CK_GET_PORT_B(ck)	BIT((ck & 0x07))
#define CK_HAS_SHIFT(ck)	(ck & BIT(6))

#define CK_MOD_LSHIFT	BIT(0)
#define CK_MOD_RSHIFT	BIT(1)
#define CK_MOD_CMDR	BIT(2)
#define CK_MOD_CONTROL	BIT(3)

#define CK_LSHIFT_PORTA		BIT(1)
#define CK_LSHIFT_PORTB		BIT(7)

#define CK_RSHIFT_PORTA		BIT(6)
#define CK_RSHIFT_PORTB		BIT(7)

#define CK_CMDR_PORTA		BIT(7)
#define CK_CMDR_PORTB		BIT(5)

#define CK_CONTROL_PORTA	BIT(7)
#define CK_CONTROL_PORTB	BIT(2)

#define CK_DELETE	CK(0, 0)
#define CK_RETURN	CK(0, 1)
#define CK_CRSR_RT	CK(0, 2)
#define CK_F7		CK(0, 3)
#define CK_F1		CK(0, 4)
#define CK_F3		CK(0, 5)
#define CK_F5		CK(0, 6)
#define CK_CRSR_DN	CK(0, 7)
#define CK_3		CK(1, 0)
#define CK_W		CK(1, 1)
#define CK_A		CK(1, 2)
#define CK_4		CK(1, 3)
#define CK_Z		CK(1, 4)
#define CK_S		CK(1, 5)
#define CK_E		CK(1, 6)
#define CK_LSHIFT	CK(1, 7)
#define CK_5		CK(2, 0)
#define CK_R		CK(2, 1)
#define CK_D		CK(2, 2)
#define CK_6		CK(2, 3)
#define CK_C		CK(2, 4)
#define CK_F		CK(2, 5)
#define CK_T		CK(2, 6)
#define CK_X		CK(2, 7)
#define CK_7		CK(3, 0)
#define CK_Y		CK(3, 1)
#define CK_G		CK(3, 2)
#define CK_8		CK(3, 3)
#define CK_B		CK(3, 4)
#define CK_H		CK(3, 5)
#define CK_U		CK(3, 6)
#define CK_V		CK(3, 7)
#define CK_9		CK(4, 0)
#define CK_I		CK(4, 1)
#define CK_J		CK(4, 2)
#define CK_0		CK(4, 3)
#define CK_M		CK(4, 4)
#define CK_K		CK(4, 5)
#define CK_O		CK(4, 6)
#define CK_N		CK(4, 7)
#define CK_PLUS		CK(5, 0)
#define CK_P		CK(5, 1)
#define CK_L		CK(5, 2)
#define CK_MINUS	CK(5, 3)
#define CK_PERIOD	CK(5, 4)
#define CK_COLON	CK(5, 5)
#define CK_AT		CK(5, 6)
#define CK_COMMA	CK(5, 7)
#define CK_POUND	CK(6, 0)
#define CK_ASTERISK	CK(6, 1)
#define CK_SEMICOL	CK(6, 2)
#define CK_HOME		CK(6, 3)
#define CK_RSHIFT	CK(6, 4)
#define CK_EQUAL	CK(6, 5)
#define CK_CARET	CK(6, 6)
#define CK_SLASH	CK(6, 7)
#define CK_1		CK(7, 0)
#define CK_LEFTARR	CK(7, 1)
#define CK_CONTROL	CK(7, 2)
#define CK_2		CK(7, 3)
#define CK_SPACE	CK(7, 4)
#define CK_CMDR		CK(7, 5)
#define CK_Q		CK(7, 6)
#define CK_STOP		CK(7, 7)

// this can't be 0 as that's CK_DELETE
#define CK_NOKEY	BIT(7)
// this should be triggering NMI
#define CK_RESTORE	(BIT(7)|BIT(0))

void initKeyboard();
void initJoysticks();

void sendKey(char key);
void sendString(const char * p);
void do_sendString();//call in yield()

uint8_t cia1PORTA(void);
uint8_t cia1PORTB(void);

#endif

