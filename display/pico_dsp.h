/*
  This file is part of DISPLAY library. 
  Supports VGA and TFT display

  DISPLAY library is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

	Teensy VGA inspired from the original Teensy3 uVGA library of Eric PREVOTEAU.
	QTIMER/FlexIO code based on Teensy4 examples of KurtE, Manitou and easone 
	from the Teensy4 forum (https://forum.pjrc.com)
*/

#ifndef _PICO_DSP_H
#define _PICO_DSP_H

#include "../include.h"

extern uint32_t nFrames;
extern bool audio_paused;
void audio_vol_update();

#define TFT_WIDTH      320
#define TFT_HEIGHT     240

#define AUDIO_SAMPLE_BUFFER_SIZE 256

typedef uint16_t dsp_pixel;

class PICO_DSP
{
public:

  PICO_DSP();

  // Initialization
  void begin();
  void startRefresh(void);
  void stopRefresh();
  void begin_audio();
  void end_audio();

  // framebuffer/screen operation
  void setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);

protected:
  uint8_t _rst, _cs, _dc;
  uint8_t _miso, _mosi, _sclk, _bkl;
};

#endif


