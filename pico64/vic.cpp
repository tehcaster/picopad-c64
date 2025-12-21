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



#define BORDER      	        (240-200)/2
#define SCREEN_HEIGHT         (200+2*BORDER)
#define SCREEN_WIDTH          320
//#define LINE_MEM_WIDTH        320
#define FIRSTDISPLAYLINE      (  51 - BORDER )
#define LASTDISPLAYLINE       ( 250 + BORDER )
#define BORDER_LEFT           (400-320)/2
#define BORDER_RIGHT          0

typedef uint16_t tpixel;

#define MAXCYCLESSPRITES0_2       3
#define MAXCYCLESSPRITES3_7       5
#define MAXCYCLESSPRITES    (MAXCYCLESSPRITES0_2 + MAXCYCLESSPRITES3_7)

u32 nFramesC64 = 0;

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

inline __attribute__((always_inline))
void fastFillLine(tpixel * p, const tpixel * pe, const uint16_t col, uint16_t * spl);
inline __attribute__((always_inline))
void fastFillLineNoSprites(tpixel * p, const tpixel * pe, const uint16_t col);
inline __attribute__((always_inline))
void fastFillLineNoCycles(tpixel * p, const tpixel * pe, const uint16_t col);


/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

#define SPRITENUM(data) (1 << ((data >> 8) & 0x07))
#define CHARSETPTR() cpu.vic.charsetPtr = cpu.vic.charsetPtrBase + cpu.vic.rc;
#define CYCLES(x) {if (cpu.vic.badline) {cia_clockt(x);} else {cpu_clock(x);} }

#define BADLINE(x) {if (cpu.vic.badline) { \
      cpu.vic.lineMemChr[x] = cpu.RAM[cpu.vic.videomatrix + vc + x]; \
	  cpu.vic.lineMemCol[x] = cpu.vic.COLORRAM[vc + x]; \
	  cia1_clock(1); \
	  cia2_clock(1); \
    } else { \
      cpu_clock(1); \
    } \
  };

#define SPRITEORFIXEDCOLOR() \
  sprite = *spl++; \
  if (sprite) { \
    *p++ = cpu.vic.palette[sprite & 0x0f]; \
  } else { \
    *p++ = col; \
  }


#if 0
#define PRINTOVERFLOW   \
  if (p>pe) { \
    Serial.print("VIC overflow Mode "); \
    Serial.println(mode); \
  }

#define PRINTOVERFLOWS  \
  if (p>pe) { \
    Serial.print("VIC overflow (Sprite) Mode ");  \
    Serial.println(mode); \
  }
#else
#define PRINTOVERFLOW
#define PRINTOVERFLOWS
#endif

static void char_sprites(tpixel *p, uint16_t *spl, uint8_t chr, uint16_t fgcol,
			 uint16_t bgcol)
{
	for (unsigned i = 0; i < 8; i++) {
		uint16_t pixel;
		uint16_t sprite = *spl++;

		if (sprite) {
			int spritenum = SPRITENUM(sprite);
			pixel = sprite & 0x0f;

			/* sprite behind foreground, MDP = 1 */
			if (sprite & 0x4000) {
				if (chr & 0x80) {
					cpu.vic.fgcollision |= spritenum;
					pixel = fgcol;
				}
			} else {
				/* sprite in front of foreground */
				if (chr & 0x80)
					cpu.vic.fgcollision |= spritenum;
			}
		} else {
			pixel = (chr & 0x80) ? fgcol : bgcol;
		}

		*p++ = cpu.vic.palette[pixel];
		chr <<= 1;
	}
}

static void char_sprites_mc(tpixel *p, uint16_t *spl, uint8_t chr, uint16_t *colors)
{
	for (int i = 0; i < 4; i++) {
		uint8_t c = (chr >> 6) & 0x03;
		chr <<= 2;

		for (int j = 0; j < 2; j++) {
			uint16_t pixel;
			uint16_t sprite = *spl++;

			if (sprite) {
				int spritenum = SPRITENUM(sprite);
				pixel = sprite & 0x0f;

				/* sprite behind foreground, MDP = 1 */
				if (sprite & 0x4000) {  // MDP = 1
					/*
					 * "It is interesting to note that not
					 * only the bit combination "00" but
					 * also "01" is regarded as "background"
					 * for the purposes of sprite priority
					 * and collision detection."
					 */
					if (c > 1) {
						cpu.vic.fgcollision |= spritenum;
						pixel = colors[c];
					}
				} else {
					if (c > 1)
						cpu.vic.fgcollision |= spritenum;
				}
			} else {
				pixel = colors[c];
			}
			*p++ = cpu.vic.palette[pixel];
		}
	}
}

/*****************************************************************************************************/
static void mode0 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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

	CHARSETPTR();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		BADLINE(x);

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

		BADLINE(x);

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
static void mode1 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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
	uint16_t bgcol, fgcol, pixel;
	uint16_t colors[4];
	uint8_t chr;
	uint8_t x = 0;

	CHARSETPTR();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	colors[0] = cpu.vic.B0C;

	while (p < pe) {

		if (cpu.vic.idle) {
			cpu_clock(1);
			fgcol = colors[1] = colors[2] = colors[3] = 0;
			chr = cpu.RAM[cpu.vic.bank + 0x3fff];
		} else {
			BADLINE(x);
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
			BADLINE(x);

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
static void mode2 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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
	uint16_t fgcol, pixel;
	uint16_t bgcol;
	uint8_t x = 0;
	uint8_t * bP = cpu.vic.bitmapPtr + vc * 8 + cpu.vic.rc;

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		BADLINE(x);

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
		BADLINE(x);

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
static void mode3 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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
	uint8_t * bP = cpu.vic.bitmapPtr + vc * 8 + cpu.vic.rc;
	uint16_t colors[4];
	uint16_t pixel;
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
			BADLINE(x);
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
			BADLINE(x);

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
static void mode4 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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

	uint8_t chr, pixel;
	uint16_t fgcol;
	uint16_t bgcol;
	uint8_t x = 0;

	CHARSETPTR();

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		BADLINE(x);

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

		BADLINE(x);

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

static void mode5 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc)
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

	CHARSETPTR();

	uint8_t chr, pixel;
	uint16_t fgcol;
	uint8_t x = 0;
	uint16_t colors[4] = { 0, 0, 0, 0};

	if (!cpu.vic.lineHasSprites)
		goto nosprites;

	while (p < pe) {

		BADLINE(x);

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

		BADLINE(x);
		x++;

		for (unsigned i = 0; i < 8; i++) {
			*p++ = bgcol;
		}
	}
}

/*****************************************************************************************************/
void mode6 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc) {
  /*
    Ungültiger Bitmap-Modus 1 (ECM/BMM/MCM=1/1/0)

    Dieser Modus erzeugt nur ebenfalls nur ein schwarzes Bild, die Pixel lassen
    sich allerdings auch hier mit dem Spritekollisionstrick auslesen.

    Der Aufbau der Grafik ist im Prinzip wie im Standard-Bitmap-Modus, aber die
    Bits 9 und 10 der g-Adressen sind wegen dem gesetzten ECM-Bit immer Null,
    entsprechend besteht auch die Grafik - grob gesagt - aus vier
    "Abschnitten", die jeweils viermal wiederholt dargestellt werden.

  */

  uint8_t chr, pixel;
  uint8_t x = 0;
  uint8_t * bP = cpu.vic.bitmapPtr + vc * 8 + cpu.vic.rc;

  if (cpu.vic.lineHasSprites) {

    do {

      BADLINE(x);

      chr = bP[x * 8];

      x++;

      unsigned m = min(8, pe - p);
      for (unsigned i = 0; i < m; i++) {

        int sprite = *spl;
        *spl++ = 0;

        if (sprite) {     // Sprite: Ja
          /*
             Sprite-Prioritäten (Anzeige)
             MDP = 1: Grafikhintergrund, Sprite, Vordergrund
             MDP = 0: Grafikhintergung, Vordergrund, Sprite

             Kollision:
             Eine Kollision zwischen Sprites und anderen Grafikdaten wird erkannt,
             sobald beim Bildaufbau ein nicht transparentes Spritepixel und ein Vordergrundpixel ausgegeben wird.

          */
          int spritenum = SPRITENUM(sprite);
          pixel = sprite & 0x0f; //Hintergrundgrafik
          if (sprite & 0x4000) {   // MDP = 1
            if (chr & 0x80) { //Vordergrundpixel ist gesetzt
              cpu.vic.fgcollision |= spritenum;
              pixel = 0;
            }
          } else {            // MDP = 0
            if (chr & 0x80) cpu.vic.fgcollision |= spritenum; //Vordergrundpixel ist gesetzt
          }

        } else {            // Kein Sprite
          pixel = 0;
        }

        *p++ = cpu.vic.palette[pixel];
        chr = chr << 1;

      }

    } while (p < pe);
    PRINTOVERFLOWS

  } else { //Keine Sprites
    //Farbe immer schwarz
    const uint16_t bgcol = palette[0];
    while (p < pe - 8) {

      BADLINE(x);
      x++;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;

    };
    while (p < pe) {

      BADLINE(x);
      x++;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol;

    };
    PRINTOVERFLOW
  }
  while (x<40) {BADLINE(x); x++;}
}
/*****************************************************************************************************/
void mode7 (tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc) {
  /*
    Ungültiger Bitmap-Modus 2 (ECM/BMM/MCM=1/1/1)

    Der letzte ungültige Modus liefert auch ein schwarzes Bild, das sich jedoch
    genauso mit Hilfe der Sprite-Grafik-Kollisionen "abtasten" läßt.

    Der Aufbau der Grafik ist im Prinzip wie im Multicolor-Bitmap-Modus, aber
    die Bits 9 und 10 der g-Adressen sind wegen dem gesetzten ECM-Bit immer
    Null, was sich in der Darstellung genauso wie beim ersten ungültigen
    Bitmap-Modus wiederspiegelt. Die Bitkombination "01" wird wie gewohnt zum
    Hintergrund gezählt.

  */

  uint8_t chr;
  uint8_t x = 0;
  uint16_t pixel;
  uint8_t * bP = cpu.vic.bitmapPtr + vc * 8 + cpu.vic.rc;

  if (cpu.vic.lineHasSprites) {

    do {

      BADLINE(x);

      chr = bP[x * 8];
      x++;

      for (unsigned i = 0; i < 4; i++) {
        if (p >= pe) break;

        int sprite = *spl;
		*spl++ = 0;

        if (sprite) {    // Sprite: Ja
          int spritenum = SPRITENUM(sprite);
          pixel = sprite & 0x0f;//Hintergrundgrafik
          if (sprite & 0x4000) {  // MDP = 1

            if (chr & 0x80) {  //Vordergrundpixel ist gesetzt
              cpu.vic.fgcollision |= spritenum;
              pixel = 0;
            }
          } else {          // MDP = 0
            if (chr & 0x80) cpu.vic.fgcollision |= spritenum; //Vordergundpixel ist gesetzt
          }
        } else { // Kein Sprite
          pixel = 0;
        }

        *p++ = cpu.vic.palette[pixel];
        if (p >= pe) break;

        sprite = *spl;
        *spl++ = 0;

        if (sprite) {    // Sprite: Ja
          int spritenum = SPRITENUM(sprite);
          pixel = sprite & 0x0f;//Hintergrundgrafik
          if (sprite & 0x4000) {  // MDP = 1

            if (chr & 0x80) {  //Vordergrundpixel ist gesetzt
              cpu.vic.fgcollision |= spritenum;
              pixel = 0;
            }
          } else {          // MDP = 0
            if (chr & 0x80) cpu.vic.fgcollision |= spritenum; //Vordergundpixel ist gesetzt
          }
        } else { // Kein Sprite
          pixel = 0;
        }

        *p++ = cpu.vic.palette[pixel];
        chr = chr << 2;


      }

    } while (p < pe);
    PRINTOVERFLOWS

  } else { //Keine Sprites

    const uint16_t bgcol = palette[0];
    while (p < pe - 8) {

      BADLINE(x);
      x++;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;
      *p++ = bgcol; *p++ = bgcol;

    };
    while (p < pe) {

      BADLINE(x);
      x++;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol; if (p >= pe) break;
      *p++ = bgcol; if (p >= pe) break; *p++ = bgcol;

    };
    PRINTOVERFLOW

  }
  while (x<40) {BADLINE(x); x++;}
}
/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

typedef void (*modes_t)( tpixel *p, const tpixel *pe, uint16_t *spl, const uint16_t vc ); //Funktionspointer
const modes_t modes[8] = {mode0, mode1, mode2, mode3, mode4, mode5, mode6, mode7};

/* extend by max xscroll rounded up */
static tpixel linebuffer[SCREEN_WIDTH + 8];

void vic_do(void) {

  uint16_t vc;
  uint16_t xscroll;
  tpixel *pe;
  tpixel *p;
  uint16_t *spl;
  uint8_t mode;
  bool csel_left;
  uint16_t bdcol_left;

  /*****************************************************************************************************/
  /* Linecounter ***************************************************************************************/
  /*****************************************************************************************************/
  /*
    ?PEEK(678) NTSC =0
    ?PEEK(678) PAL = 1
  */

  if ( cpu.vic.rasterLine >= LINECNT ) {
    //reSID sound needs much time - too much to keep everything in sync and with stable refreshrate
    //but it is not called very often, so most of the time, we have more time than needed.
    //We can measure the time needed for a frame and calc a correction factor to speed things up.
    unsigned long m = fbmicros();
    cpu.vic.neededTime = (m - cpu.vic.timeStart);
    cpu.vic.timeStart = m;
    cpu.vic.lineClock.setIntervalFast(LINETIMER_DEFAULT_FREQ - ((float)cpu.vic.neededTime / (float)LINECNT - LINETIMER_DEFAULT_FREQ ));

    cpu.vic.rasterLine = 0;
    cpu.vic.vcbase = 0;
    cpu.vic.denLatch = 0;
    //if (cpu.vic.rasterLine == LINECNT) {
      //emu_DrawVsync();
    //}

  } else {
    cpu.vic.rasterLine++;
    if (cpu.vic.rasterLine >= LINECNT) 
      nFramesC64++;
  }


  cpu.vic.badlineLate = false;

  int r = cpu.vic.rasterLine;

  if (r == cpu.vic.intRasterLine )//Set Rasterline-Interrupt
    cpu.vic.R[0x19] |= 1 | ((cpu.vic.R[0x1a] & 1) << 7);

  /*****************************************************************************************************/
  /* Badlines ******************************************************************************************/
  /*****************************************************************************************************/
  /*
    Ein Bad-Line-Zustand liegt in einem beliebigen Taktzyklus vor, wenn an der
    negativen Flanke von ø0 zu Beginn des Zyklus RASTER >= $30 und RASTER <=
    $f7 und die unteren drei Bits von RASTER mit YSCROLL übereinstimmen und in
    einem beliebigen Zyklus von Rasterzeile $30 das DEN-Bit gesetzt war.

    (default 3)
    yscroll : POKE 53265, PEEK(53265) AND 248 OR 1:POKE 1024,0
    yscroll : poke 53265, peek(53265) and 248 or 1

    DEN : POKE 53265, PEEK(53265) AND 224 Bildschirm aus

    Die einzige Verwendung von YSCROLL ist der Vergleich mit r in der Badline

  */

  if (r == 0x30 ) cpu.vic.denLatch |= cpu.vic.DEN;

  /* 3.7.2
    2. In der ersten Phase von Zyklus 14 jeder Zeile wird VC mit VCBASE geladen
     (VCBASE->VC) und VMLI gelöscht. Wenn zu diesem Zeitpunkt ein
     Bad-Line-Zustand vorliegt, wird zusätzlich RC auf Null gesetzt.
  */

  vc = cpu.vic.vcbase;

  cpu.vic.badline = (cpu.vic.denLatch && (r >= 0x30) && (r <= 0xf7) && ( (r & 0x07) == cpu.vic.YSCROLL));

  if (cpu.vic.badline) {
    cpu.vic.idle = 0;
    cpu.vic.rc = 0;
  }

  /*****************************************************************************************************/
  /*****************************************************************************************************/
#if 1
  {
    int t = MAXCYCLESSPRITES3_7 - cpu.vic.spriteCycles3_7;
    if (t > 0) cpu_clock(t);
    if (cpu.vic.spriteCycles3_7 > 0) cia_clockt(cpu.vic.spriteCycles3_7);
  }
#endif

   //HBlank:
   cpu_clock(10);

#ifdef ADDITIONALCYCLES
  cpu_clock(ADDITIONALCYCLES);
#endif

  //cpu.vic.videomatrix =  cpu.vic.bank + (unsigned)(cpu.vic.R[0x18] & 0xf0) * 64;

  /* Rand oben /unten **********************************************************************************/
  /*
    RSEL  Höhe des Anzeigefensters  Erste Zeile   Letzte Zeile
    0 24 Textzeilen/192 Pixel 55 ($37)  246 ($f6) = 192 sichtbare Zeilen, der Rest ist Rand oder unsichtbar
    1 25 Textzeilen/200 Pixel 51 ($33)  250 ($fa) = 200 sichtbare Zeilen, der Rest ist Rand oder unsichtbar
  */

  if (cpu.vic.borderFlag) {
    int firstLine = (cpu.vic.RSEL) ? 0x33 : 0x37;
    if ((cpu.vic.DEN) && (r == firstLine)) cpu.vic.borderFlag = false;
  } else {
    int lastLine = (cpu.vic.RSEL) ? 0xfb : 0xf7;
    if (r == lastLine) cpu.vic.borderFlag = true;
  }

  if (r < FIRSTDISPLAYLINE || r > LASTDISPLAYLINE ) {
    if (r == 0)
      cpu_clock(CYCLESPERRASTERLINE - 10 - 2 - MAXCYCLESSPRITES - 1); // (minus hblank l + r)
    else
      cpu_clock(CYCLESPERRASTERLINE - 10 - 2 - MAXCYCLESSPRITES  );
    goto noDisplayIncRC;
  }

  //max_x =  (!cpu.vic.CSEL) ? 40:38;
  p = &linebuffer[0];
  pe = p + SCREEN_WIDTH;
  //Left Screenborder: Cycle 10
  spl = &cpu.vic.spriteLine[24];
  cpu_clock(6);

  /*
   * For many YSCROLL values the bad line can occur in the top border so we must
   * perform the full modeX emulation for timing and initializing the
   * character/bitmap data for the following lines. We will however ignore both
   * the calculated pixels and fgcollision at the end.
   */
  if (cpu.vic.borderFlag && !cpu.vic.badline) {
	cpu_clock(5);
    fastFillLineNoSprites(p, pe + BORDER_RIGHT, cpu.vic.colors[0]);
    if (r >= FIRSTDISPLAYLINE + BORDER && r <= LASTDISPLAYLINE - BORDER)
      memcpy(&FrameBuf[(r - FIRSTDISPLAYLINE)*SCREEN_WIDTH], &linebuffer[0], SCREEN_WIDTH*2);
    goto noDisplayIncRC ;
  }


  /*****************************************************************************************************/
  /* DISPLAY *******************************************************************************************/
  /*****************************************************************************************************/


  //max_x =  (!cpu.vic.CSEL) ? 40:38;
  //X-Scrolling:

  /*
   * Remember if CSEL was set when we reached x = 24, current border color to
   * overwrite first 7 pixels later if !CSEL
   *
   * TODO: we ignore hyperscreen for the foreseeable future...
   */
  csel_left = cpu.vic.CSEL;
  bdcol_left = cpu.vic.colors[0];

  xscroll = cpu.vic.XSCROLL;

  if (xscroll > 0) {
    if (csel_left) {
      /* we don't have the extra border to cover the bg/sprites so render them */
      uint16_t sprite;
      uint16_t col = cpu.vic.colors[1]; /* B0C */

      for (int i = 0; i < xscroll; i++) {
        SPRITEORFIXEDCOLOR();
      }
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


  cpu.vic.fgcollision = 0;
  mode = (cpu.vic.ECM << 2) | (cpu.vic.BMM << 1) | cpu.vic.MCM;

  if ( !cpu.vic.idle)  {

#if 0
    static uint8_t omode = 99;
    if (mode != omode) {
      Serial.print("Graphicsmode:");
      Serial.println(mode);
      omode = mode;
    }
#endif

    modes[mode](p, pe, spl, vc);
    vc = (vc + 40) & 0x3ff;

  } else {
	  /*
3.7.3.9. Idle-Zustand
---------------------

Im Idle-Zustand liest der VIC die Grafikdaten von Adresse $3fff (bzw. $39ff
bei gesetztem ECM-Bit) und stellt sie im ausgewählten Grafikmodus dar,
wobei aber die Videomatrix-Daten (normalerweise in den c-Zugriffen gelesen)
nur aus "0"-Bits bestehen. Es wird also immer wiederholt das Byte an
Adresse $3fff/$39ff ausgegeben.

c-Zugriff

 Es werden keine c-Zugriffe ausgeführt.

 Daten

 +----+----+----+----+----+----+----+----+----+----+----+----+
 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
 +----+----+----+----+----+----+----+----+----+----+----+----+
 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |  0 |
 +----+----+----+----+----+----+----+----+----+----+----+----+

g-Zugriff

 Adressen (ECM=0)

 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 | 13 | 12 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |
 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+

 Adressen (ECM=1)

 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 | 13 | 12 | 11 | 10 |  9 |  8 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+
 |  1 |  1 |  1 |  0 |  0 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |  1 |
 +----+----+----+----+----+----+----+----+----+----+----+----+----+----+

 Daten

 +----+----+----+----+----+----+----+----+
 |  7 |  6 |  5 |  4 |  3 |  2 |  1 |  0 |
 +----+----+----+----+----+----+----+----+
 |         8 Pixel (1 Bit/Pixel)         | Standard-Textmodus/
 |                                       | Multicolor-Textmodus/
 | "0": Hintergrundfarbe 0 ($d021)       | ECM-Textmodus
 | "1": Schwarz                          |
 +---------------------------------------+
 |         8 Pixel (1 Bit/Pixel)         | Standard-Bitmap-Modus/
 |                                       | Ungültiger Textmodus/
 | "0": Schwarz (Hintergrund)            | Ungültiger Bitmap-Modus 1
 | "1": Schwarz (Vordergrund)            |
 +---------------------------------------+
 |         4 Pixel (2 Bit/Pixel)         | Multicolor-Bitmap-Modus
 |                                       |
 | "00": Hintergrundfarbe 0 ($d021)      |
 | "01": Schwarz (Hintergrund)           |
 | "10": Schwarz (Vordergrund)           |
 | "11": Schwarz (Vordergrund)           |
 +---------------------------------------+
 |         4 Pixel (2 Bit/Pixel)         | Ungültiger Bitmap-Modus 2
 |                                       |
 | "00": Schwarz (Hintergrund)           |
 | "01": Schwarz (Hintergrund)           |
 | "10": Schwarz (Vordergrund)           |
 | "11": Schwarz (Vordergrund)           |
 +---------------------------------------+
*/ 
	//Modes 1 & 3
    if (mode == 1 || mode == 3) {
		modes[mode](p, pe, spl, vc);
    } else {//TODO: all other modes
	fastFillLine(p, pe, cpu.vic.palette[0], spl);
	}
  }

  /*
   * In the top/bottom border, sprite-data collisions are not detected and also
   * discard the graphics we just generated (only to emulate the bad line).
   */
  if (cpu.vic.borderFlag) {
    fastFillLineNoCycles(p, pe, cpu.vic.colors[0]);
  /*
    Bei den MBC- und MMC-Interrupts löst jeweils nur die erste Kollision einen
    Interrupt aus (d.h. wenn die Kollisionsregister $d01e bzw. $d01f vor der
    Kollision den Inhalt Null hatten). Um nach einer Kollision weitere
    Interrupts auszulösen, muß das betreffende Register erst durch Auslesen
    gelöscht werden.
  */
  } else if (cpu.vic.fgcollision) {
    if (cpu.vic.MD == 0) {
      cpu.vic.R[0x19] |= 2 | ( (cpu.vic.R[0x1a] & 2) << 6);
    }
    cpu.vic.MD |= cpu.vic.fgcollision;
  }

  /*****************************************************************************************************/

  /* grow the right border over what has been rendered */
  if (!cpu.vic.CSEL) {
    for (unsigned int i = SCREEN_WIDTH - 9; i < SCREEN_WIDTH; i++) {
      linebuffer[i] = cpu.vic.colors[0];
    }
  }

  /* grow also the left border as determined earlier */
  if (!csel_left) {
    for (unsigned int i = 0; i < 7; i++) {
      linebuffer[i] = bdcol_left;
    }
  }

  memcpy(&FrameBuf[(r - FIRSTDISPLAYLINE)*SCREEN_WIDTH], &linebuffer[0], SCREEN_WIDTH*2);
  //memset(&linebuffer[0], 0, sizeof(linebuffer));


//Rechter Rand nach CSEL, im Textbereich
cpu_clock(5);


noDisplayIncRC:
  /* 3.7.2
    5. In der ersten Phase von Zyklus 58 wird geprüft, ob RC=7 ist. Wenn ja,
     geht die Videologik in den Idle-Zustand und VCBASE wird mit VC geladen
     (VC->VCBASE). Ist die Videologik danach im Display-Zustand (liegt ein
     Bad-Line-Zustand vor, ist dies immer der Fall), wird RC erhöht.
  */

  if (cpu.vic.rc == 7) {
    cpu.vic.idle = 1;
    cpu.vic.vcbase = vc;
  }
  //Ist dies richtig ??
  if ((!cpu.vic.idle) || (cpu.vic.denLatch && (r >= 0x30) && (r <= 0xf7) && ( (r & 0x07) == cpu.vic.YSCROLL))) {
    cpu.vic.rc = (cpu.vic.rc + 1) & 0x07;
  }

  /*
   * If we went idle, a bad line condition becoming true due to YSCROLL write
   * at this point will not make us active again. Unsure if this is correct
   * but helped against River Raid bottom HUD glitching.
   */
  cpu.vic.badlineLate = true;

  /*****************************************************************************************************/
  /* Sprites *******************************************************************************************/
  /*****************************************************************************************************/

  cpu.vic.spriteCycles0_2 = 0;
  cpu.vic.spriteCycles3_7 = 0;

  if (cpu.vic.lineHasSprites) {
	cpu.vic.lineHasSprites = 0;
    memset(cpu.vic.spriteLine, 0, sizeof(cpu.vic.spriteLine) );
  }

  uint32_t spriteYCheck = cpu.vic.R[0x15]; //Sprite enabled Register

  if (spriteYCheck) {

    unsigned short R17 = cpu.vic.R[0x17]; //Sprite-y-expansion
    unsigned char collision = 0;
    short lastSpriteNum = 0;

    for (unsigned short i = 0; i < 8; i++) {
      if (!spriteYCheck) break;

      unsigned b = 1 << i;

      if (spriteYCheck & b )  {
        spriteYCheck &= ~b;
        short y = cpu.vic.R[i * 2 + 1];

        if ( (r >= y ) && //y-Position > Sprite-y ?
             (((r < y + 21) && (~R17 & b )) || // ohne y-expansion
              ((r < y + 2 * 21 ) && (R17 & b ))) ) //mit y-expansion
        {

          //Sprite Cycles
          if (i < 3) {
            if (!lastSpriteNum) cpu.vic.spriteCycles0_2 += 1;
            cpu.vic.spriteCycles0_2 += 2;
          } else {
            if (!lastSpriteNum) cpu.vic.spriteCycles3_7 += 1;
            cpu.vic.spriteCycles3_7 += 2;
          }
          lastSpriteNum = i;
          //Sprite Cycles END


          if (r < FIRSTDISPLAYLINE || r > LASTDISPLAYLINE ) continue;

          uint16_t x =  (((cpu.vic.R[0x10] >> i) & 1) << 8) | cpu.vic.R[i * 2];
          if (x >= SPRITE_MAX_X) continue;

          //DEBUG
          //if (config.single_frame_mode)
          //  printf("line %d mode %u sprite %u x %u y %u\n", r, mode, i, x, y);

          unsigned short lineOfSprite = r - y;
          if (R17 & b) lineOfSprite = lineOfSprite / 2; // Y-Expansion
          unsigned short spriteadr = cpu.vic.bank | cpu.RAM[cpu.vic.videomatrix + (1024 - 8) + i] << 6 | (lineOfSprite * 3);
          unsigned spriteData = ((unsigned)cpu.RAM[ spriteadr ] << 24) | ((unsigned)cpu.RAM[ spriteadr + 1 ] << 16) | ((unsigned)cpu.RAM[ spriteadr + 2 ] << 8);

          if (!spriteData) continue;
          spriteData = reverse(spriteData);
          cpu.vic.lineHasSprites = 1;

          uint16_t * slp = &cpu.vic.spriteLine[x]; //Sprite-Line-Pointer
          unsigned short upperByte = ( 0x80 | ( (cpu.vic.MDP & b) ? 0x40 : 0 ) | i ) << 8; //Bit7 = Sprite "da", Bit 6 = Sprite-Priorität vor Grafik/Text, Bits 3..0 = Spritenummer

          //if (config.single_frame_mode)
          //  printf("upperByte %x color %u\n", upperByte, cpu.vic.R[0x27 + i]);

          //Sprite in Spritezeile schreiben:
          if ((cpu.vic.MMC & b) == 0) { // NO MULTICOLOR

            uint16_t color = upperByte | cpu.vic.R[0x27 + i];

            if ((cpu.vic.MXE & b) == 0) { // NO MULTICOLOR, NO SPRITE-EXPANSION
              for (unsigned cnt = 0; cnt < 24; cnt++) {
                int c = spriteData & 0x01;
                spriteData >>= 1;

                if (c) {
                  if (*slp == 0) *slp = color;
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                }
                slp++;

              }

            } else {    // NO MULTICOLOR, SPRITE-EXPANSION
	      for (unsigned cnt = 0; cnt < 24; cnt++) {
                int c = spriteData & 0x01;
                spriteData >>= 1;
                //So wie oben, aber zwei gleiche Pixel

                if (c) {
                  if (*slp == 0) *slp = color;
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                  if (*slp == 0) *slp = color;
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                } else {
                  slp += 2;
                }

              }
            }



          } else { // MULTICOLOR
            /* Im Mehrfarbenmodus (Multicolor-Modus) bekommen alle Sprites zwei zusätzliche gemeinsame Farben.
              Die horizontale Auflösung wird von 24 auf 12 halbiert, da bei der Sprite-Definition jeweils zwei Bits zusammengefasst werden.
            */
            uint16_t colors[4];
            colors[0] = 0; //dummy, color 0 is transparent
            colors[1] = upperByte | cpu.vic.R[0x27 + i];
            colors[2] = upperByte | cpu.vic.R[0x25];
            colors[3] = upperByte | cpu.vic.R[0x26];

            if ((cpu.vic.MXE & b) == 0) { // MULTICOLOR, NO SPRITE-EXPANSION
              for (unsigned cnt = 0; cnt < 12; cnt++) {
                int c = spriteData & 0x03;
                spriteData >>= 2;

                if (c) {
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                } else {
                  slp += 2;
                }

              }

            } else {    // MULTICOLOR, SPRITE-EXPANSION
              for (unsigned cnt = 0; cnt < 12; cnt++) {
                int c = spriteData & 0x03;
                spriteData >>= 2;

                //So wie oben, aber vier gleiche Pixel
                if (c) {
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                  if (*slp == 0) *slp = colors[c];
                  else collision |= b | (1 << ((*slp >> 8) & 0x07));
                  slp++;
                } else {
                  slp += 4;
                }

              }

            }
          }

        }
        else lastSpriteNum = 0;
      }

    }

    if (collision) {
      if (cpu.vic.MM == 0) {
        cpu.vic.R[0x19] |= 4 | ((cpu.vic.R[0x1a] & 4) << 5 );
      }
      cpu.vic.MM |= collision;
    }

  }
  /*****************************************************************************************************/
#if 0
  {
    int t = MAXCYCLESSPRITES0_2 - cpu.vic.spriteCycles0_2;
    if (t > 0) cpu_clock(t);
    if (cpu.vic.spriteCycles0_2 > 0) cia_clockt(cpu.vic.spriteCycles0_2);
  }
#endif

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
void fastFillLineNoCycles(tpixel * p, const tpixel * pe, const uint16_t col)
{
  while (p < pe)
    *p++ = col;
}

void fastFillLineNoSprites(tpixel * p, const tpixel * pe, const uint16_t col) {
  int i = 0;

  while (p < pe) {
		*p++ = col;
		i = (i + 1) & 0x07;
		if (!i) CYCLES(1);
  }


}

void fastFillLine(tpixel * p, const tpixel * pe, const uint16_t col, uint16_t *  spl) {
  if (spl != NULL && cpu.vic.lineHasSprites) {
	int i = 0;
	uint16_t sprite;
    while ( p < pe ) {
		SPRITEORFIXEDCOLOR();
		i = (i + 1) & 0x07;
		if (!i) CYCLES(1);
    };

  } else {

    fastFillLineNoSprites(p, pe, col);

  }
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

void vic_displaySimpleModeScreen(void) {
}


void vic_do_simple(void) {
  uint16_t vc;
  int cycles = 0;

if ( cpu.vic.rasterLine >= LINECNT ) {

    //reSID sound needs much time - too much to keep everything in sync and with stable refreshrate
    //but it is not called very often, so most of the time, we have more time than needed.
    //We can measure the time needed for a frame and calc a correction factor to speed things up.
    unsigned long m = fbmicros();
    cpu.vic.neededTime = (m - cpu.vic.timeStart);
    cpu.vic.timeStart = m;
    cpu.vic.lineClock.setIntervalFast(LINETIMER_DEFAULT_FREQ - ((float)cpu.vic.neededTime / (float)LINECNT - LINETIMER_DEFAULT_FREQ ));

    cpu.vic.rasterLine = 0;
    cpu.vic.vcbase = 0;
    cpu.vic.denLatch = 0;

    nFramesC64++;

  } else  {
    cpu.vic.rasterLine++;
    if (cpu.vic.rasterLine >= LINECNT) 
      nFramesC64++;
    cpu_clock(1);
    cycles += 1;
  }

  int r = cpu.vic.rasterLine;

  if (r == cpu.vic.intRasterLine )//Set Rasterline-Interrupt
    cpu.vic.R[0x19] |= 1 | ((cpu.vic.R[0x1a] & 1) << 7);

  cpu_clock(9);
  cycles += 9;

  if (r == 0x30 ) cpu.vic.denLatch |= cpu.vic.DEN;

  vc = cpu.vic.vcbase;

  cpu.vic.badline = (cpu.vic.denLatch && (r >= 0x30) && (r <= 0xf7) && ( (r & 0x07) == cpu.vic.YSCROLL));

  if (cpu.vic.badline) {
    cpu.vic.idle = 0;
    cpu.vic.rc = 0;
  }


  /* Rand oben /unten **********************************************************************************/
  /*
    RSEL  Höhe des Anzeigefensters  Erste Zeile   Letzte Zeile
    0 24 Textzeilen/192 Pixel 55 ($37)  246 ($f6) = 192 sichtbare Zeilen, der Rest ist Rand oder unsichtbar
    1 25 Textzeilen/200 Pixel 51 ($33)  250 ($fa) = 200 sichtbare Zeilen, der Rest ist Rand oder unsichtbar
  */

  if (cpu.vic.borderFlag) {
    int firstLine = (cpu.vic.RSEL) ? 0x33 : 0x37;
    if ((cpu.vic.DEN) && (r == firstLine)) cpu.vic.borderFlag = false;
  } else {
    int lastLine = (cpu.vic.RSEL) ? 0xfb : 0xf7;
    if (r == lastLine) cpu.vic.borderFlag = true;
  }


 //left screenborder
 cpu_clock(6);
 cycles += 6;

 CYCLES(40);
 cycles += 40;
 vc += 40;

 //right screenborder
 cpu_clock(4); //1
 cycles += 4;


  if (cpu.vic.rc == 7) {
    cpu.vic.idle = 1;
    cpu.vic.vcbase = vc;
  }
  //Ist dies richtig ??
  if ((!cpu.vic.idle) || (cpu.vic.denLatch && (r >= 0x30) && (r <= 0xf7) && ( (r & 0x07) == cpu.vic.YSCROLL))) {
    cpu.vic.rc = (cpu.vic.rc + 1) & 0x07;
  }

  cpu_clock(3); //1
 cycles += 3;

 int cyclesleft = CYCLESPERRASTERLINE - cycles;
 if (cyclesleft) cpu_clock(cyclesleft);

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

void vic_adrchange(void) {
  uint8_t r18 = cpu.vic.R[0x18];
  cpu.vic.videomatrix =  cpu.vic.bank + (unsigned)(r18 & 0xf0) * 64;

  unsigned charsetAddr = r18 & 0x0e;
  if  ((cpu.vic.bank & 0x4000) == 0) {
    if (charsetAddr == 0x04) cpu.vic.charsetPtrBase =  ((uint8_t *)&rom_characters);
    else if (charsetAddr == 0x06) cpu.vic.charsetPtrBase =  ((uint8_t *)&rom_characters) + 0x800;
    else
      cpu.vic.charsetPtrBase = &cpu.RAM[charsetAddr * 0x400 + cpu.vic.bank] ;
  } else
    cpu.vic.charsetPtrBase = &cpu.RAM[charsetAddr * 0x400 + cpu.vic.bank];

  cpu.vic.bitmapPtr = (uint8_t*) &cpu.RAM[cpu.vic.bank | ((r18 & 0x08) * 0x400)];
  if ((cpu.vic.R[0x11] & 0x60) == 0x60)  cpu.vic.bitmapPtr = (uint8_t*)((uintptr_t)cpu.vic.bitmapPtr & 0xf9ff);

}
/*****************************************************************************************************/
void vic_write(uint32_t address, uint8_t value) {

  address &= 0x3F;

  switch (address) {
    case 0x11 :
    cpu.vic.R[address] = value;
      cpu.vic.intRasterLine = (cpu.vic.intRasterLine & 0xff) | ((((uint16_t) value) << 1) & 0x100);
      if (cpu.vic.rasterLine == 0x30 ) cpu.vic.denLatch |= value & 0x10;

      cpu.vic.badline = (cpu.vic.denLatch && (cpu.vic.rasterLine >= 0x30) && (cpu.vic.rasterLine <= 0xf7) && ( (cpu.vic.rasterLine & 0x07) == (value & 0x07)));

    if (cpu.vic.badline && !cpu.vic.badlineLate) {
    cpu.vic.idle = 0;
    }

    vic_adrchange();

      break;
    case 0x12 :
      cpu.vic.intRasterLine = (cpu.vic.intRasterLine & 0x100) | value;
      cpu.vic.R[address] = value;
      break;
    case 0x18 :
      cpu.vic.R[address] = value;
      vic_adrchange();
      break;
    case 0x19 : //IRQs
      cpu.vic.R[0x19] &= (~value & 0x0f);
      break;
    case 0x1A : //IRQ Mask
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
    default :
      cpu.vic.R[address] = value;
      break;
  }

  //#if DEBUGVIC
#if 0
  Serial.print("VIC ");
  Serial.print(address, HEX);
  Serial.print("=");
  Serial.println(value, HEX);
  //logAddr(address, value, 1);
#endif
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

uint8_t vic_read(uint32_t address) {
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

#if DEBUGVIC
  Serial.print("VIC ");
  logAddr(address, ret, 0);
#endif
  return ret;
}

/*****************************************************************************************************/
/*****************************************************************************************************/
/*****************************************************************************************************/

void resetVic(void) {
  enableCycleCounter();

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


/*
  ?PEEK(678) NTSC =0
  ?PEEK(678) PAL = 1
  PRINT TIME$
*/
/*
          Raster-  Takt-   sichtb.  sichtbare
  VIC-II  System  zeilen   zyklen  Zeilen   Pixel/Zeile
  -------------------------------------------------------
  6569    PAL    312     63    284     403
  6567R8  NTSC   263     65    235     418
  6567R56A  NTSC   262   64    234     411
*/
