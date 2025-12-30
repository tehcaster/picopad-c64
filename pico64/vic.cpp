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

/*
  TODOs:
  - Fix Bugs..
  - FLD  - (OK 08/17) test this more..
  - Sprite Stretching (requires "MOBcounter")
  - BA Signal -> CPU
  - xFLI
  - ...
  - DMA Delay (?) - needs partial rewrite (idle - > badline in middle of line. Is the 3.6 fast enough??)
  - optimize more
*/

#include "../include.h"

#include "Teensy64.h"
#include "vic.h"
//#include <string.h>
//#include <math.h>
//#include <stdlib.h>

#include "c64.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b)) 

#define PALETTE(r,g,b) (RGBVAL16(r,g,b))  //(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
#include "vic_palette.h"

#define SCREEN_WIDTH		320
#define SCREEN_HEIGHT		240
#define BORDER			(SCREEN_HEIGHT-200)/2
#define SCREEN_ROW_OFFSET	(51 - BORDER)
/* do not display top/bottom border */
#define FIRSTDISPLAYLINE	51
#define LASTDISPLAYLINE		250

typedef uint16_t tpixel;

#define MAXCYCLESSPRITES0_2       3
#define MAXCYCLESSPRITES3_7       5
#define MAXCYCLESSPRITES    (MAXCYCLESSPRITES0_2 + MAXCYCLESSPRITES3_7)

u32 nFramesC64 = 0;

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

static inline void badline_cycle(int x)
{
	if (cpu.vic.badline) {
		cpu.vic.lineMemChr[x] = cpu.RAM[cpu.vic.videomatrix + cpu.vic.vc + x];
		cpu.vic.lineMemCol[x] = cpu.vic.COLORRAM[cpu.vic.vc + x];
	}
	cpu_clock(1);
}

static inline void update_charsetPtr(void)
{
	cpu.vic.charsetPtr = cpu.vic.charsetPtrBase + cpu.vic.rc;
}

static inline int is_badline(int r)
{
	return cpu.vic.badline = (cpu.vic.denLatch && (r >= 0x30) && (r <= 0xf7)
				 && ((r & 0x07) == cpu.vic.YSCROLL));
}

static void trigger_fgcol(uint8_t fgcol)
{
	if (cpu.vic.MD == 0) {
		cpu.vic.IMBC = 1;
		if (cpu.vic.EMBC)
			cpu.vic.IRQ = 1;
	}
	cpu.vic.MD |= fgcol;
}

static void trigger_sprcol(uint8_t sprcol)
{
	if (cpu.vic.MM == 0) {
		cpu.vic.IMMC = 1;
		if (cpu.vic.EMMC)
			cpu.vic.IRQ = 1;
	}
	cpu.vic.MM |= sprcol;
}

static void sprite_collisions_clock(const sprite_data_t *spl)
{
	uint8_t sprite_collision = 0;

	for (int j = 0; j < 8; j++) {
		if (spl->collision)
			sprite_collision |= spl->active_mask;
		spl++;
	}

	if (sprite_collision)
		trigger_sprcol(sprite_collision);
}

static void char_sprites(tpixel *p, sprite_data_t *spl, uint8_t chr, uint16_t fgcol,
			 uint16_t bgcol)
{
	uint8_t fg_collision = 0;
	uint8_t sprite_collision = 0;

	for (unsigned i = 0; i < 8; i++) {
		uint16_t pixel;
		sprite_data_t sprite = *spl++;

		if (sprite.raw) {
			pixel = sprite.color;

			if (chr & 0x80) {
				fg_collision |= sprite.active_mask;
				if (sprite.MDP)
					pixel = fgcol;
			}
			if (sprite.collision)
				sprite_collision |= sprite.active_mask;
		} else {
			pixel = (chr & 0x80) ? fgcol : bgcol;
		}

		*p++ = cpu.vic.palette[pixel];
		chr <<= 1;
	}

	if (fg_collision)
		trigger_fgcol(fg_collision);
	if (sprite_collision)
		trigger_sprcol(sprite_collision);
}

static void char_sprites_mc(tpixel *p, sprite_data_t *spl, uint8_t chr, uint16_t *colors)
{
	uint8_t fg_collision = 0;
	uint8_t sprite_collision = 0;

	for (int i = 0; i < 4; i++) {
		uint8_t c = (chr >> 6) & 0x03;
		chr <<= 2;

		for (int j = 0; j < 2; j++) {
			uint16_t pixel;
			sprite_data_t sprite = *spl++;

			if (sprite.raw) {
				pixel = sprite.color;

				/*
				 * "It is interesting to note that not only the
				 * bit combination "00" but also "01" is
				 * regarded as "background" for the purposes of
				 * sprite priority and collision detection."
				 */
				if (c > 1) {
					fg_collision |= sprite.active_mask;
					if (sprite.MDP)
						pixel = colors[c];
				}

				if (sprite.collision)
					sprite_collision |= sprite.active_mask;
			} else {
				pixel = colors[c];
			}
			*p++ = cpu.vic.palette[pixel];
		}
	}

	if (fg_collision)
		trigger_fgcol(fg_collision);
	if (sprite_collision)
		trigger_sprcol(sprite_collision);
}

static void fastFillLineNoCycles(tpixel * p, const tpixel * pe, const uint16_t col)
{
	while (p < pe)
		*p++ = col;
}

static void fastFillLineNoSprites(tpixel * p, const tpixel * pe, const uint16_t col)
{
	while (p < pe) {
		for (int i = 0; i < 8; i++) {
			*p++ = col;
		}

		cpu_clock(1);
	}
}

static void fastFillLine(tpixel * p, const tpixel * pe, const uint16_t col, sprite_data_t * spl)
{
	if (cpu.vic.lineHasSprites &&
	    (!cpu.vic.borderFlag || cpu.vic.lineHasSpriteCollisions)) {
		while (p < pe) {
			uint8_t sprite_collision = 0;

			for (int i = 0; i < 8; i++) {
				sprite_data_t sprite = *spl++;

				if (sprite.raw)
					*p++ = cpu.vic.palette[sprite.color];
					if (sprite.collision)
						sprite_collision |= sprite.active_mask;
				else
					*p++ = col;
			}

			if (sprite_collision)
				trigger_sprcol(sprite_collision);

			cpu_clock(1);
		}
	} else {
		fastFillLineNoSprites(p, pe, col);
	}
}

/*****************************************************************************************************/
static void mode0 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.1. Standard text mode (ECM/BMM/MCM=0/0/0)
	-----------------------------------------------

	In this mode (as in all text modes), the VIC reads 8-bit character pointers
	from the video matrix that specify the address of the dot matrix of the
	character within the character generator. A character set of 256 characters
	is available, each consisting of 8×8 pixels which are stored in 8 successive
	bytes in the character generator. Both video matrix and character generator
	can be moved in memory with the bits VM10-VM13 and CB11-CB13 of register
	$d018.

	In standard text mode, each bit in the character generator directly
	corresponds to one pixel on the screen. The foreground color is given by the
	color nybble from the video matrix for each character, the background color
	is set globally with register $d021.

	+----+----+----+----+----+----+----+----+
	|  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	+----+----+----+----+----+----+----+----+
	|         8 pixels (1 bit/pixel)        |
	|                                       |
	| "0": Background color 0 ($d021)       |
	| "1": Color from bits 8-11 of c-data   |
	+---------------------------------------+
	*/

	uint8_t chr, pixel;
	uint16_t fgcol;
	uint16_t bgcol;
	uint16_t x = 0;

	update_charsetPtr();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		chr = cpu.vic.charsetPtr[cpu.vic.lineMemChr[x] * 8];
		fgcol = cpu.vic.lineMemCol[x];
		x++;

		char_sprites(p, spl, chr, fgcol, cpu.vic.B0C);
		p += 8;
		spl += 8;
	}

	return;

nosprites:

	while (p < pe) {

		badline_cycle(x);

		chr = cpu.vic.charsetPtr[cpu.vic.lineMemChr[x] * 8];
		fgcol = cpu.vic.palette[cpu.vic.lineMemCol[x]];
		bgcol = cpu.vic.colors[1];
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = (chr & 0x80) ? fgcol : bgcol;
			chr <<= 1;
		}
	}
};

/*****************************************************************************************************/
static void mode1 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.2. Multicolor text mode (ECM/BMM/MCM=0/0/1)
	-------------------------------------------------

	This mode allows displaying four-colored characters at the expense of
	horizontal resolution. If bit 11 of the c-data is zero, the character is
	displayed as in standard text mode with only the colors 0-7 available for
	the foreground. If bit 11 is set, two adjacent bits each of the dot matrix
	form one pixel. In this way, the resolution of a character is effectively
	reduced to 4×8 pixels, with each pixel being twice as wide so the total
	width of the characters remains the same.

	It is interesting to note that not only the bit combination "00" but also
	"01" is regarded as "background" for the purposes of sprite priority and
	collision detection.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         8 pixels (1 bit/pixel)        |
	 |                                       | MC flag = 0
	 | "0": Background color 0 ($d021)       |
	 | "1": Color from bits 8-10 of c-data   |
	 +---------------------------------------+
	 |         4 pixels (2 bits/pixel)       |
	 |                                       |
	 | "00": Background color 0 ($d021)      | MC flag = 1
	 | "01": Background color 1 ($d022)      |
	 | "10": Background color 2 ($d023)      |
	 | "11": Color from bits 8-10 of c-data  |
	 +---------------------------------------+
	 */
	uint16_t bgcol, fgcol;
	uint16_t colors[4];
	uint8_t chr;
	uint8_t x = 0;

	update_charsetPtr();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	colors[0] = cpu.vic.B0C;

	while (p < pe) {

		if (cpu.vic.idle) {
			cpu_clock(1);
			fgcol = colors[1] = colors[2] = colors[3] = 0;
			chr = cpu.RAM[cpu.vic.bank + 0x3fff];
		} else {
			badline_cycle(x);
			fgcol = cpu.vic.lineMemCol[x];
			colors[1] = cpu.vic.R[0x22];
			colors[2] = cpu.vic.R[0x23];
			colors[3] = fgcol & 0x07;
			chr = cpu.vic.charsetPtr[cpu.vic.lineMemChr[x] * 8];
		}

		x++;

		/* MC flag 0 */
		if ((fgcol & 0x08) == 0)
			char_sprites(p, spl, chr, colors[3], colors[0]);
		else
			char_sprites_mc(p, spl, chr, &colors[0]);

		p += 8;
		spl += 8;
	}

	return;

nosprites:

	while (p < pe) {

		int c;

		bgcol = cpu.vic.colors[1];
		colors[0] = bgcol;

		if (cpu.vic.idle) {
			cpu_clock(1);
			c = colors[1] = colors[2] = colors[3] = 0;
			chr = cpu.RAM[cpu.vic.bank + 0x3fff];
		} else {
			badline_cycle(x);

			colors[1] = cpu.vic.colors[2];
			colors[2] = cpu.vic.colors[3];

			chr = cpu.vic.charsetPtr[cpu.vic.lineMemChr[x] * 8];
			c = cpu.vic.lineMemCol[x];
		}

		x++;

		/* MC flag 0 */
		if ((c & 0x08) == 0) {

			fgcol = cpu.vic.palette[c & 0x07];

			for (unsigned i = 0; i < 8; i++) {
				*p++ = (chr & 0x80) ? fgcol : bgcol;
				chr <<= 1;
			}
		} else {
			colors[3] = cpu.vic.palette[c & 0x07];

			for (unsigned i = 0; i < 4; i++) {
				uint8_t col = (chr >> 6) & 0x03;
				chr <<= 2;

				*p++ = colors[col];
				*p++ = colors[col];
			}
		}
	}
}

/*****************************************************************************************************/
static void mode2 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.3. Standard bitmap mode (ECM/BMM/MCM=0/1/0)
	-------------------------------------------------

	In this mode, the VIC reads the graphics data from a 320×200 pixel bitmap in
	which each bit corresponds to one pixel on the screen. The data from the
	video matrix is used for color information. As the video matrix is still
	only a 40×25 matrix, you can only specify the colors for blocks of 8×8
	pixels individually (sort of a YC 8:1 format). As the designers of the VIC
	wanted to realize the bitmap mode with as little additional circuitry as
	possible (the VIC-I didn't have a bitmap mode), the arrangement of the
	bitmap in memory is somewhat weird: In contrast to modern video chips that
	read the bitmap in a linear fashion from memory, the VIC forms an 8×8 pixel
	block on the screen from 8 successive bytes of the bitmap. The video matrix
	and the bitmap can be moved in memory with the bits VM10-VM13 and CB13 of
	register $d018.

	In standard bitmap mode, each bit in the bitmap directly corresponds to one
	pixel on the screen. Foreground and background color can be arbitrarily set
	for each 8×8 pixel block.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         8 pixels (1 bit/pixel)        |
	 |                                       |
	 | "0": Color from bits 0-3 of c-data    |
	 | "1": Color from bits 4-7 of c-data    |
	 +---------------------------------------+
	 */

	uint8_t chr;
	uint16_t fgcol;
	uint16_t bgcol;
	uint8_t x = 0;
	uint8_t * bP = cpu.vic.bitmapPtr + cpu.vic.vc * 8 + cpu.vic.rc;

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		uint8_t t = cpu.vic.lineMemChr[x];
		fgcol = t >> 4;
		bgcol = t & 0x0f;
		chr = bP[x * 8];

		x++;

		char_sprites(p, spl, chr, fgcol, bgcol);
		p += 8;
		spl += 8;

	} while (p < pe);

	return;

nosprites:

	while (p < pe) {
		//color-ram not used!
		badline_cycle(x);

		uint8_t t = cpu.vic.lineMemChr[x];
		fgcol = cpu.vic.palette[t >> 4];
		bgcol = cpu.vic.palette[t & 0x0f];
		chr = bP[x * 8];
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = (chr & 0x80) ? fgcol : bgcol;
			chr <<= 1;
		}
	}
}

/*****************************************************************************************************/
static void mode3 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.4. Multicolor bitmap mode (ECM/BMM/MCM=0/1/1)
	---------------------------------------------------

	Similar to the multicolor text mode, this mode also forms (twice as wide)
	pixels by combining two adjacent bits each. So the resolution is reduced to
	160×200 pixels.

	The bit combination "01" is also treated as "background" for the sprite
	priority and collision detection, as in multicolor text mode.

	+----+----+----+----+----+----+----+----+
	|  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	+----+----+----+----+----+----+----+----+
	|         4 pixels (2 bits/pixel)       |
	|                                       |
	| "00": Background color 0 ($d021)      |
	| "01": Color from bits 4-7 of c-data   |
	| "10": Color from bits 0-3 of c-data   |
	| "11": Color from bits 8-11 of c-data  |
	+---------------------------------------+
	*/
	uint8_t * bP = cpu.vic.bitmapPtr + cpu.vic.vc * 8 + cpu.vic.rc;
	uint16_t colors[4];
	uint8_t chr, x;

	x = 0;

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	colors[0] = cpu.vic.B0C;
	while (p < pe) {

		if (cpu.vic.idle) {
			cpu_clock(1);
			colors[1] = colors[2] = colors[3] = 0;
			chr = cpu.RAM[cpu.vic.bank + 0x3fff];
		} else {
			badline_cycle(x);
			uint8_t t = cpu.vic.lineMemChr[x];
			colors[1] = t >> 4;//10
			colors[2] = t & 0x0f; //01
			colors[3] = cpu.vic.lineMemCol[x];
			chr = bP[x * 8];
		}

		x++;

		char_sprites_mc(p, spl, chr, &colors[0]);
		p += 8;
		spl += 8;
	}

	return;

nosprites:

	while (p < pe) {

		colors[0] = cpu.vic.colors[1];

		if (cpu.vic.idle) {
			cpu_clock(1);
			colors[1] = colors[2] = colors[3] = 0;
			chr = cpu.RAM[cpu.vic.bank + 0x3fff];
		} else {
			badline_cycle(x);

			uint8_t t = cpu.vic.lineMemChr[x];
			colors[1] = cpu.vic.palette[t >> 4];//10
			colors[2] = cpu.vic.palette[t & 0x0f]; //01
			colors[3] = cpu.vic.palette[cpu.vic.lineMemCol[x]];
			chr = bP[x * 8];
		}

		x++;

		for (unsigned i = 0; i < 4; i++) {
			uint8_t col = (chr >> 6) & 0x03;
			chr <<= 2;

			*p++ = colors[col];
			*p++ = colors[col];
		}
	}
}

/*****************************************************************************************************/
static void mode4 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	 3.7.3.5. ECM text mode (ECM/BMM/MCM=1/0/0)
	 ------------------------------------------

	 This text mode is the same as the standard text mode, but it allows the
	 selection of one of four background colors for every single character. The
	 selection is made with the upper two bits of the character pointer. This,
	 however, reduces the available character set from 256 to 64 characters.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         8 pixels (1 bit/pixel)        |
	 |                                       |
	 | "0": Depending on bits 6/7 of c-data  |
	 |      00: Background color 0 ($d021)   |
	 |      01: Background color 1 ($d022)   |
	 |      10: Background color 2 ($d023)   |
	 |      11: Background color 3 ($d024)   |
	 | "1": Color from bits 8-11 of c-data   |
	 +---------------------------------------+
	 */

	uint8_t chr;
	uint16_t fgcol;
	uint16_t bgcol;
	uint8_t x = 0;

	update_charsetPtr();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		uint8_t td = cpu.vic.lineMemChr[x];

		bgcol = cpu.vic.R[0x21 + (td >> 6)];
		chr = cpu.vic.charsetPtr[(td & 0x3f) * 8];
		fgcol = cpu.vic.lineMemCol[x];

		x++;

		char_sprites(p, spl, chr, fgcol, bgcol);
		p += 8;
		spl += 8;
	}

	return;

nosprites:
	while (p < pe) {

		badline_cycle(x);

		uint8_t td = cpu.vic.lineMemChr[x];

		bgcol = cpu.vic.colors[1 + (td >> 6)];
		chr = cpu.vic.charsetPtr[(td & 0x3f) * 8];
		fgcol = cpu.vic.palette[cpu.vic.lineMemCol[x]];
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = (chr & 0x80) ? fgcol : bgcol;
			chr <<= 1;
		}
	}
}

/*****************************************************************************************************/
/* Invalid Modes *************************************************************************************/
/*****************************************************************************************************/

static void mode5 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.6. Invalid text mode (ECM/BMM/MCM=1/0/1)
	----------------------------------------------

	Setting the ECM and MCM bits simultaneously doesn't select one of the
	"official" graphics modes of the VIC but creates only black pixels.
	Nevertheless, the graphics data sequencer internally generates valid
	graphics data that can trigger sprite collisions even in this mode. By using
	sprite collisions you can also read out the generated data (but you cannot
	see anything; the screen is black). You can, however, only distinguish
	foreground and background pixels as you cannot get color information from
	sprite collisions.

	The generated graphics is similar to that of the multicolor text mode, but
	the character set is limited to 64 characters as in ECM mode.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         8 pixels (1 bit/pixel)        |
	 |                                       | MC flag = 0
	 | "0": Black (background)               |
	 | "1": Black (foreground)               |
	 +---------------------------------------+
	 |         4 pixels (2 bits/pixel)       |
	 |                                       |
	 | "00": Black (background)              | MC flag = 1
	 | "01": Black (background)              |
	 | "10": Black (foreground)              |
	 | "11": Black (foreground)              |
	 +---------------------------------------+
	 */

	uint8_t chr;
	uint16_t fgcol;
	uint8_t x = 0;
	uint16_t colors[4] = { 0, 0, 0, 0};

	update_charsetPtr();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		uint8_t td = cpu.vic.lineMemChr[x];

		chr = cpu.vic.charsetPtr[(td & 0x3f) * 8];
		fgcol = cpu.vic.lineMemCol[x];

		x++;

		if ((fgcol & 0x08) == 0)
			char_sprites(p, spl, chr, 0, 0);
		else
			char_sprites_mc(p, spl, chr, &colors[0]);

		p += 8;
		spl += 8;
	}

	return;

nosprites:

	const uint16_t bgcol = palette[0];

	while (p < pe) {

		badline_cycle(x);
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = bgcol;
		}
	}
}

/*****************************************************************************************************/
static void mode6 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{
	/*
	3.7.3.7. Invalid bitmap mode 1 (ECM/BMM/MCM=1/1/0)
	--------------------------------------------------

	This mode also only displays a black screen, but the pixels can still be
	read out with the sprite collision trick.

	The structure of the graphics is basically as in standard bitmap mode, but
	the bits 9 and 10 of the g-addresses are always zero due to the set ECM bit
	and so the graphics are, roughly said, made up of four "sections" each of
	which is repeated four times.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         8 pixels (1 bit/pixel)        |
	 |                                       |
	 | "0": Black (background)               |
	 | "1": Black (foreground)               |
	 +---------------------------------------+
	 */

	uint8_t chr;
	uint8_t x = 0;
	uint8_t * bP = cpu.vic.bitmapPtr + (cpu.vic.vc & 0xff3f) * 8 + cpu.vic.rc;

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		chr = bP[x * 8];

		x++;

		char_sprites(p, spl, chr, 0, 0);
		p += 8;
		spl += 8;

	}

	return;

nosprites:

	const uint16_t bgcol = palette[0];

	while (p < pe) {

		badline_cycle(x);
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = bgcol;
		}
	}
}

/*****************************************************************************************************/
static void mode7 (tpixel *p, const tpixel *pe, sprite_data_t *spl)
{

	/*
	3.7.3.8. Invalid bitmap mode 2 (ECM/BMM/MCM=1/1/1)
	--------------------------------------------------
	The last invalid mode also creates a black screen that can be "scanned" with
	sprite-graphics collisions.

	The structure of the graphics is basically as in multicolor bitmap mode, but
	the bits 9 and 10 of the g-addresses are always zero due to the set ECM bit,
	with the same results as in the first invalid bitmap mode. As usual, the bit
	combination "01" is part of the background.

	 +----+----+----+----+----+----+----+----+
	 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
	 +----+----+----+----+----+----+----+----+
	 |         4 pixels (2 bits/pixel)       |
	 |                                       |
	 | "00": Black (background)              |
	 | "01": Black (background)              |
	 | "10": Black (foreground)              |
	 | "11": Black (foreground)              |
	 +---------------------------------------+
	 */


	uint8_t * bP = cpu.vic.bitmapPtr + (cpu.vic.vc & 0xff3f) * 8 + cpu.vic.rc;
	uint8_t chr;
	uint8_t x = 0;
	uint16_t colors[4] = { 0, 0, 0, 0};

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		badline_cycle(x);

		chr = bP[x * 8];
		x++;

		char_sprites_mc(p, spl, chr, &colors[0]);
		p += 8;
		spl += 8;
	}

	return;

nosprites:

	const uint16_t bgcol = palette[0];

	while (p < pe) {

		badline_cycle(x);
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = bgcol;
		}
	}
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

typedef void (*modes_t)( tpixel *p, const tpixel *pe, sprite_data_t *spl);
const modes_t modes[8] = {mode0, mode1, mode2, mode3, mode4, mode5, mode6, mode7};

/* extend by max xscroll rounded up */
static tpixel linebuffer[SCREEN_WIDTH + 8];

static inline void spl_update(sprite_data_t *old, const sprite_data_t _new, uint8_t b)
{
	if ((*old).raw == 0) {
		*old = _new;
	} else {
		(*old).active_mask |= b;
		(*old).collision = 1;
		cpu.vic.lineHasSpriteCollisions = true;
	}
}

static void process_sprite(uint32_t spriteData, unsigned int x, uint8_t i, int idx)
{
	uint8_t b = 1 << i;

	sprite_data_t sprite {
		.active_mask = b,
	};

	if (cpu.vic.MDP & b)
		sprite.MDP = 1;

	sprite_data_t *spl = cpu.vic.spriteLine[idx];

	//if (config.single_frame_mode)
	//  printf("upperByte %x color %u\n", upperByte, cpu.vic.R[0x27 + i]);

	if (x < cpu.vic.sprite_x_min[idx])
		cpu.vic.sprite_x_min[idx] = x;

	if ((cpu.vic.MMC & b) == 0) { // NO MULTICOLOR

		sprite.color = cpu.vic.R[0x27 + i];

		if ((cpu.vic.MXE & b) == 0) { // NO MULTICOLOR, NO SPRITE-EXPANSION
			for (int cnt = 0; cnt < 24; cnt++) {

				if (spriteData & 0x01)
					spl_update(spl + x, sprite, b);

				spriteData >>= 1;
				x = (x + 1) % MAX_X;
			}

		} else {    // NO MULTICOLOR, SPRITE-EXPANSION
			for (int cnt = 0; cnt < 24; cnt++) {

				if (spriteData & 0x01) {
					for (int j = 0; j < 2; j++) {
						spl_update(spl + x, sprite, b);
						x = (x + 1) % MAX_X;
					}
				} else {
					x = (x + 2) % MAX_X;
				}

				spriteData >>= 1;
			}
		}
	} else { // MULTICOLOR

		/*
		 * If the sprite is in multicolor mode, two adjacent
		 * bits each form one pixel.
		 *
		 +----+----+----+----+----+----+----+----+
		 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
		 +----+----+----+----+----+----+----+----+
		 |         8 pixels (1 bit/pixel)        |
		 |                                       | MxMC = 0
		 | "0": Transparent                      |
		 | "1": Sprite color ($d027-$d02e)       |
		 +---------------------------------------+
		 |         4 pixels (2 bits/pixel)       |
		 |                                       |
		 | "00": Transparent                     | MxMC = 1
		 | "01": Sprite multicolor 0 ($d025)     |
		 | "10": Sprite color ($d027-$d02e)      |
		 | "11": Sprite multicolor 1 ($d026)     |
		 +---------------------------------------+
		 *
		 * note we have colors[1] and [2] swapped because the
		 * spriteData bits are reversed
		 */
		uint8_t colors[4];
		colors[0] = 0; //dummy, color 0 is transparent
		colors[1] = cpu.vic.R[0x27 + i];
		colors[2] = cpu.vic.R[0x25];
		colors[3] = cpu.vic.R[0x26];

		if ((cpu.vic.MXE & b) == 0) { // MULTICOLOR, NO SPRITE-EXPANSION
			for (unsigned cnt = 0; cnt < 12; cnt++) {
				int c = spriteData & 0x03;
				spriteData >>= 2;

				if (c) {
					for (int j = 0; j < 2; j++) {
						sprite.color = colors[c];
						spl_update(spl + x, sprite, b);
						x = (x + 1) % MAX_X;
					}
				} else {
					x = (x + 2) % MAX_X;
				}
			}
		} else {    // MULTICOLOR, SPRITE-EXPANSION
			for (unsigned cnt = 0; cnt < 12; cnt++) {
				int c = spriteData & 0x03;
				spriteData >>= 2;

				if (c) {
					for (int j = 0; j < 4; j++) {
						sprite.color = colors[c];
						spl_update(spl + x, sprite, b);
						x = (x + 1) % MAX_X;
					}
				} else {
					x = (x + 4) % MAX_X;
				}
			}
		}
	}

	if (x > cpu.vic.sprite_x_max[idx])
		cpu.vic.sprite_x_max[idx] = x;
}

void vic_do(void)
{
	uint16_t xscroll;
	tpixel *pe;
	tpixel *p;
	sprite_data_t *spl;
	uint8_t mode;
	bool csel_left;
	uint16_t bdcol_left;

	/*****************************************************************************************************/
	/* Linecounter ***************************************************************************************/
	/*****************************************************************************************************/

	if (cpu.vic.rasterLine >= LINECNT) {
		cpu.vic.rasterLine = 0;
		cpu.vic.vcbase = 0;
		cpu.vic.denLatch = 0;

	} else {
		cpu.vic.rasterLine++;
		if (cpu.vic.rasterLine >= LINECNT) 
			nFramesC64++;
	}

	cpu.vic.badlineLate = false;

	int r = cpu.vic.rasterLine;

	/* Rasterline Interrupt
	 * TODO:
	 * It is possible to trigger an interrupt immediately by writing to
	 * $d011/$d012, but the interrupt can never occur more than once per
	 * raster line.
	 * - that means we should intercept it in vic_write() ?
	 */
	if (r == cpu.vic.intRasterLine) {
		cpu.vic.IRST = 1;
		if (cpu.vic.ERST)
			cpu.vic.IRQ = 1;
	}

	/*****************************************************************************************************/
	/* Badlines ******************************************************************************************/
	/*****************************************************************************************************/

	/*
	 * A Bad Line Condition is given at any arbitrary clock cycle if, at the
	 * negative edge of ϕ0 at the beginning of the cycle, RASTER >= $30 and
	 * RASTER <= $f7 and the lower three bits of RASTER are equal to
	 * YSCROLL, and if the DEN bit was set during an arbitrary cycle of
	 * raster line $30.
	 *
	 * The only use of YSCROLL is for comparison with r in the badline. (?)
	 *
	 * TODO: should intercept DEN change in arbitrary cycle of line $30 ?
	 */
	if (r == 0x30)
		cpu.vic.denLatch |= cpu.vic.DEN;

	/* 3.7.2. VC and RC
	 *
	 * 2. In the first phase of cycle 14 of each line, VC is loaded from VCBASE
	 * (VCBASE->VC) and VMLI is cleared. If there is a Bad Line Condition in
	 * this phase, RC is also reset to zero
	 */

	cpu.vic.vc = cpu.vic.vcbase;

	cpu.vic.badline = is_badline(r);

	if (cpu.vic.badline) {
		cpu.vic.idle = 0;
		cpu.vic.rc = 0;
	}

	/*****************************************************************************************************/
	/*****************************************************************************************************/

	//HBlank:
	if (cpu.vic.lineHasSpriteCollisions) {
		/* x positions 0x190 to 0x1E0 - 80 pixels, 10 clocks */
		spl = &cpu.vic.spriteLine[r % 2][0x190];
		for (int i = 0; i < 10; i++) {

			sprite_collisions_clock(spl);
			spl += 8;

			cpu_clock(1);
		}
	} else {
		cpu_clock(10);
	}

	// TODO: review this
#ifdef ADDITIONALCYCLES
	cpu_clock(ADDITIONALCYCLES);
#endif

	//TODO: review
	//cpu.vic.videomatrix =  cpu.vic.bank + (unsigned)(cpu.vic.R[0x18] & 0xf0) * 64;

	/* Upper/lower border:
	 *
	 *  RSEL|  Display window height   | First line  | Last line
	 *  ----+--------------------------+-------------+----------
	 *    0 | 24 text lines/192 pixels |   55 ($37)  | 246 ($f6)
	 *    1 | 25 text lines/200 pixels |   51 ($33)  | 250 ($fa)
	 */

	if (cpu.vic.borderFlag) {
		int firstLine = (cpu.vic.RSEL) ? 0x33 : 0x37;
		if ((cpu.vic.DEN) && (r == firstLine))
			cpu.vic.borderFlag = false;
	} else {
		int lastLine = (cpu.vic.RSEL) ? 0xfb : 0xf7;
		if (r == lastLine)
			cpu.vic.borderFlag = true;
	}

	//TODO: why subtract MAXCYCLESSPRITES ?
	if ((r < FIRSTDISPLAYLINE || r > LASTDISPLAYLINE) && !cpu.vic.badline &&
			!cpu.vic.lineHasSpriteCollisions) {
		if (r == 0)
			cpu_clock(CYCLESPERRASTERLINE - 10 - 2 - MAXCYCLESSPRITES - 1); // (minus hblank l + r)
		else
			cpu_clock(CYCLESPERRASTERLINE - 10 - 2 - MAXCYCLESSPRITES);
		goto after_right_border;
	}

	p = &linebuffer[0];
	pe = p + SCREEN_WIDTH;

	//Left Screenborder: Cycle 10

	if (cpu.vic.lineHasSpriteCollisions) {
		/* x positions 0x1E0 to 0x1F8 - 24 pixels, 3 clocks */
		spl = &cpu.vic.spriteLine[r % 2][0x1E0];
		for (int i = 0; i < 3; i++) {

			sprite_collisions_clock(spl);
			spl += 8;

			cpu_clock(1);
		}
		cpu.ba_low = cpu.vic.badline;
		/* x positions 0-24 */
		spl = &cpu.vic.spriteLine[r % 2][0];
		for (int i = 0; i < 3; i++) {

			sprite_collisions_clock(spl);
			spl += 8;

			cpu_clock(1);
		}
	} else {
		cpu_clock(3);
		cpu.ba_low = cpu.vic.badline;
		cpu_clock(3);
		spl = &cpu.vic.spriteLine[r % 2][24];
	}

	//Start of display area: Cycle 16

	/*
	 * For many YSCROLL values the bad line can occur in the top border so we must
	 * perform the full modeX emulation for timing and initializing the
	 * character/bitmap data for the following lines. We will however ignore both
	 * the calculated pixels and fgcollision at the end.
	 */
	if (cpu.vic.borderFlag && !cpu.vic.badline) {
		fastFillLine(p, pe, cpu.vic.colors[0], spl);
		if (r >= FIRSTDISPLAYLINE && r <= LASTDISPLAYLINE)
			memcpy(&FrameBuf[(r - SCREEN_ROW_OFFSET)*SCREEN_WIDTH], &linebuffer[0], SCREEN_WIDTH*2);
		goto right_border;
	}

	/*****************************************************************************************************/
	/* DISPLAY *******************************************************************************************/
	/*****************************************************************************************************/

	/*
	 * Remember if CSEL was set when we reached x = 24, and current border
	 * color to overwrite the first 7 pixels later if !CSEL
	 *
	 * TODO: we ignore hyperscreen for the foreseeable future...
	 */
	csel_left = cpu.vic.CSEL;
	bdcol_left = cpu.vic.colors[0];

	xscroll = cpu.vic.XSCROLL;

	if (xscroll > 0) {
		if (csel_left || cpu.vic.lineHasSpriteCollisions) {

			uint8_t sprite_collision = 0;

			/*
			 * we don't have the extra border to cover the
			 * bg/sprites, or we need to check for sprite-sprite
			 * collisions
			 */
			for (int i = 0; i < xscroll; i++) {
				sprite_data_t sprite = *spl++;

				if (sprite.raw) {
					*p++ = cpu.vic.palette[sprite.color];

					if (sprite.collision)
						sprite_collision |= sprite.active_mask;
				} else {
					*p++ = cpu.vic.colors[1]; /* B0C */
				}
			}

			if (sprite_collision)
				trigger_sprcol(sprite_collision);

		} else {
			/* just bump everything as the border will cover it */
			spl += xscroll;
			p += xscroll;
		}
		/* bump the end as well for proper clocks during modes (?) */
		pe += xscroll;
	}

	/*****************************************************************************************************/
	/*****************************************************************************************************/
	/*****************************************************************************************************/

	mode = (cpu.vic.ECM << 2) | (cpu.vic.BMM << 1) | cpu.vic.MCM;

	if (!cpu.vic.idle) {
		modes[mode](p, pe, spl);
		cpu.vic.vc = (cpu.vic.vc + 40) & 0x3ff;
	} else {
		if (mode == 1 || mode == 3) {
			modes[mode](p, pe, spl);
		} else {
			//TODO: support idle in all the other modes
			fastFillLine(p, pe, cpu.vic.palette[0], spl);
		}
	}

	/*
	 * In the top/bottom border, sprite-data collisions are not detected and also
	 * discard the graphics we just generated (only to emulate the bad line).
	 *
	 * TODO: actually not detect collisions there...
	 */
	if (cpu.vic.borderFlag) {
		fastFillLineNoCycles(p, pe, cpu.vic.colors[0]);
	}

	/*****************************************************************************************************/

	/* grow the right border over what has been rendered */
	if (!cpu.vic.CSEL) {
		for (int i = SCREEN_WIDTH - 9; i < SCREEN_WIDTH; i++) {
			linebuffer[i] = cpu.vic.colors[0];
		}
	}

	/* grow also the left border as determined earlier */
	if (!csel_left) {
		for (unsigned int i = 0; i < 7; i++) {
			linebuffer[i] = bdcol_left;
		}
	}

	memcpy(&FrameBuf[(r - SCREEN_ROW_OFFSET) * SCREEN_WIDTH], &linebuffer[0], SCREEN_WIDTH*2);

right_border:

	cpu.ba_low = false;

	/* Right border, in the text area (?) */
	cpu_clock(5);

after_right_border:

	/* 3.7.2. VC and RC:
	 *
	 * 5. In the first phase of cycle 58, the VIC checks if RC=7. If so, the video
	 * logic goes to idle state and VCBASE is loaded from VC (VC->VCBASE). If
	 * the video logic is still in display state (which is always the case if
	 * there is a Bad Line Condition), then RC is incremented.
	 *
	 * 3.7.1. Idle state / display state
	 *
	 * The transition from display to idle state occurs in cycle 58 of a
	 * line if the RC contains the value 7 and there is no Bad Line
	 * Condition.
	 */
	if (cpu.vic.rc == 7) {
		/*
		 * TODO: 3.14.3. FLI section suggests this is wrong and we
		 * should not reset vcbase if there's badline. But that also
		 * caused River Raid HUD glitching.
		 */
		cpu.vic.vcbase = cpu.vic.vc;
		if (!cpu.vic.badline)
			cpu.vic.idle = 1;
	}

	if (!cpu.vic.idle) {
		cpu.vic.rc = (cpu.vic.rc + 1) & 0x07;
	}

	/*
	 * If we went idle, a bad line condition becoming true due to YSCROLL write
	 * at this point will not make us active again. Unsure if this is correct
	 * but helped against River Raid bottom HUD glitching.
	 *
	 * TODO: review
	 */
	cpu.vic.badlineLate = true;

	/*****************************************************************************************************/
	/* Sprites *******************************************************************************************/
	/*****************************************************************************************************/

	bool right_border_sprcols = cpu.vic.lineHasSpriteCollisions;
	int idx = (r + 1) % 2;

	if (cpu.vic.lineHasSprites) {
		cpu.vic.lineHasSprites = false;
		cpu.vic.lineHasSpriteCollisions = false;
	}
	if (cpu.vic.spriteBadCyclesLeft || cpu.vic.spriteBadCyclesRight) {
		memset(&cpu.vic.spriteBadCycles[0], 0, sizeof(cpu.vic.spriteBadCycles));
		cpu.vic.spriteBadCyclesLeft = false;
		cpu.vic.spriteBadCyclesRight = false;
	}
	if (cpu.vic.spriteLineDirty[idx]) {
		unsigned int x_min = cpu.vic.sprite_x_min[idx];
		unsigned int x_max = cpu.vic.sprite_x_max[idx];

		if (x_min < x_max) {
			memset(&cpu.vic.spriteLine[idx][x_min], 0,
				sizeof(sprite_data_t) * (x_max - x_min));
		} else {
			/* single sprite that straddles x=0, should be rare */
			memset(&cpu.vic.spriteLine[idx][0], 0, sizeof(cpu.vic.spriteLine[0]));
		}
		cpu.vic.spriteLineDirty[idx] = false;
		cpu.vic.sprite_x_min[idx] = MAX_X;
		cpu.vic.sprite_x_max[idx] = 0;
	}

	uint8_t spritesEnabled = cpu.vic.R[0x15]; //Sprite enabled Register
	unsigned short R17 = cpu.vic.R[0x17]; //Sprite-y-expansion

	if (!spritesEnabled)
		goto sprites_loaded;

	for (uint8_t i = 0; i < 8; i++) {

		unsigned b = 1 << i;

		if (!(spritesEnabled & b))
			continue;

		short y = cpu.vic.R[i * 2 + 1];

		if (r < y)
			continue;

		/* without y-expansion */
		if (!(R17 & b) && r >= y + 21)
			continue;

		/* with y-expansion */
		if ((R17 & b) && r >= y + 2 * 21)
			continue;

		if (i < 3)
			cpu.vic.spriteBadCyclesRight = true;
		else
			cpu.vic.spriteBadCyclesLeft = true;

		uint16_t x = (((cpu.vic.R[0x10] >> i) & 1) << 8) | cpu.vic.R[i * 2];
		if (x >= MAX_X)
			continue;

		//DEBUG
		//if (config.single_frame_mode)
		//  printf("line %d mode %u sprite %u x %u y %u\n", r, mode, i, x, y);

		unsigned short lineOfSprite = r - y;
		if (R17 & b) lineOfSprite = lineOfSprite / 2; // Y-Expansion

		unsigned short spriteaddr = cpu.vic.bank
			| cpu.RAM[cpu.vic.videomatrix + (1024 - 8) + i] << 6
			| (lineOfSprite * 3);

		uint32_t spriteData = ((unsigned)cpu.RAM[ spriteaddr ] << 24)
			| ((unsigned)cpu.RAM[ spriteaddr + 1 ] << 16)
			| ((unsigned)cpu.RAM[ spriteaddr + 2 ] << 8);

		if (!spriteData)
			continue;

		/* rotate so we don't need to shift */
		spriteData = reverse(spriteData);

		cpu.vic.lineHasSprites = true;
		cpu.vic.spriteLineDirty[idx] = true;

		process_sprite(spriteData, x, i, idx);

		for (int j = 0; j < 5; j++) {
			cpu.vic.spriteBadCycles[2*i + j] = 1;
		}
	}

sprites_loaded:

	/*
	 * Trigger sprite collisions for x values in the right border.
	 * We don't try to be clock-precise here. Also some of the data for
	 * sprites 0-2 might be outdated as we might have re-read it now?
	 */
	if (right_border_sprcols) {
		uint8_t sprite_collision = 0;

		for (unsigned int x = 0x158 + xscroll; x < 0x190; x++) {
			sprite_data_t *spl = &cpu.vic.spriteLine[r % 2][x];

			if (spl->collision)
				sprite_collision |= spl->active_mask;
		}

		if (sprite_collision) {
			trigger_sprcol(sprite_collision);
			printf("sprite collision in rigth border\n");
		}
	}

	/*****************************************************************************************************/

	//HBlank:
#if PAL
	cpu_clock(2);
#else
	cpu_clock(3);
#endif


#if 0
	if (cpu.vic.idle) {
		Serial.print("Cycles line ");
		Serial.print(r);
		Serial.print(": ");
		Serial.println(cpu.lineCyclesAbs);
	}
#endif
	return;
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

void installPalette(void) {
	memcpy(cpu.vic.palette, (void*)palette, sizeof(cpu.vic.palette));
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

void vic_adrchange(void)
{
	uint8_t r18 = cpu.vic.R[0x18];
	cpu.vic.videomatrix =  cpu.vic.bank + (unsigned)(r18 & 0xf0) * 64;

	unsigned charsetAddr = r18 & 0x0e;

	if ((cpu.vic.bank & 0x4000) == 0) {
		if (charsetAddr == 0x04)
			cpu.vic.charsetPtrBase = ((uint8_t *)&rom_characters);
		else if (charsetAddr == 0x06)
			cpu.vic.charsetPtrBase = ((uint8_t *)&rom_characters) + 0x800;
		else
			cpu.vic.charsetPtrBase = &cpu.RAM[charsetAddr * 0x400 + cpu.vic.bank];
	} else {
		cpu.vic.charsetPtrBase = &cpu.RAM[charsetAddr * 0x400 + cpu.vic.bank];
	}

	cpu.vic.bitmapPtr = (uint8_t*) &cpu.RAM[cpu.vic.bank | ((r18 & 0x08) * 0x400)];
	if ((cpu.vic.R[0x11] & 0x60) == 0x60)
		cpu.vic.bitmapPtr = (uint8_t*)((uintptr_t)cpu.vic.bitmapPtr & 0xf9ff);
}

/*****************************************************************************************************/

void vic_write(uint32_t address, uint8_t value)
{
	address &= 0x3F;

	switch (address) {
	case 0x11:
		cpu.vic.R[address] = value;
		cpu.vic.intRasterLine = (cpu.vic.intRasterLine & 0xff) | ((((uint16_t) value) << 1) & 0x100);
		if (cpu.vic.rasterLine == 0x30)
			cpu.vic.denLatch |= value & 0x10;

		cpu.vic.badline = is_badline(cpu.vic.rasterLine);

		if (cpu.vic.badline && !cpu.vic.badlineLate) {
			cpu.vic.idle = 0;
		}

		vic_adrchange();

		break;
	case 0x12:
		cpu.vic.intRasterLine = (cpu.vic.intRasterLine & 0x100) | value;
		cpu.vic.R[address] = value;
		break;
	case 0x18:
		cpu.vic.R[address] = value;
		vic_adrchange();
		break;
	case 0x19: //IRQs
		/*
		 * to clear the latch, the written bit has to be 1
		 * apparently this includes the IRQ bit too
		 */
		cpu.vic.R[0x19] &= ~value;
		/*
		 * but if all individual enabled bits were cleared, clear IRQ
		 * as well
		 */
		if (!(cpu.vic.R[0x19] & cpu.vic.R[0x1a] & 0x0f))
			cpu.vic.IRQ = 0;
		break;
	case 0x1A: //IRQ Mask
		cpu.vic.R[address] = value & 0x0f;
		break;
	case 0x1e:
	case 0x1f:
		cpu.vic.R[address] = 0;
		break;
	case 0x20 ... 0x2E:
		cpu.vic.R[address] = value & 0x0f;
		cpu.vic.colors[address - 0x20] = cpu.vic.palette[value & 0x0f];
		break;
	case 0x2F ... 0x3F:
		break;
	default:
		cpu.vic.R[address] = value;
		break;
	}
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

uint8_t vic_read(uint32_t address)
{
	uint8_t ret;

	address &= 0x3F;
	switch (address) {

	case 0x11:
		ret = (cpu.vic.R[address] & 0x7F) | ((cpu.vic.rasterLine & 0x100) >> 1);
		break;
	case 0x12:
		ret = cpu.vic.rasterLine;
		break;
	case 0x16:
		ret = cpu.vic.R[address] | 0xC0;
		break;
	case 0x18:
		ret = cpu.vic.R[address] | 0x01;
		break;
	case 0x19:
		ret = cpu.vic.R[address] | 0x70;
		break;
	case 0x1a:
		ret = cpu.vic.R[address] | 0xF0;
		break;
	case 0x1e:
	case 0x1f:
		ret = cpu.vic.R[address];
		cpu.vic.R[address] = 0;
		break;
	case 0x20 ... 0x2E:
		ret = cpu.vic.R[address] | 0xF0;
		break;
	case 0x2F ... 0x3F:
		ret = 0xFF;
		break;
	default:
		ret = cpu.vic.R[address];
		break;
	}

	return ret;
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

void resetVic(void)
{

	cpu.vic.intRasterLine = 0;
	cpu.vic.rasterLine = 0;
	cpu.vic.lineHasSprites = 0;
	memset(&cpu.RAM[0x400], 0, 1000);
	memset(&cpu.vic, 0, sizeof(cpu.vic));

	installPalette();

	//http://dustlayer.com/vic-ii/2013/4/22/when-visibility-matters
	cpu.vic.R[0x11] = 0x9B;
	cpu.vic.R[0x16] = 0x08;
	cpu.vic.R[0x18] = 0x14;
	cpu.vic.R[0x19] = 0x0f;

	for (unsigned i = 0; i < sizeof(cpu.vic.COLORRAM); i++)
		cpu.vic.COLORRAM[i] = (rand() & 0x0F);

	cpu.RAM[0x39FF] = 0x0;
	cpu.RAM[0x3FFF] = 0x0;
	cpu.RAM[0x39FF + 16384] = 0x0;
	cpu.RAM[0x3FFF + 16384] = 0x0;
	cpu.RAM[0x39FF + 32768] = 0x0;
	cpu.RAM[0x3FFF + 32768] = 0x0;
	cpu.RAM[0x39FF + 49152] = 0x0;
	cpu.RAM[0x3FFF + 49152] = 0x0;

	vic_adrchange();
}

