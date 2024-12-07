/*
	This file is part of DISPLAY library. 
  Supports VGA and TFT display

	DISPLAY library is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Copyright (C) 2020 J-M Harvengt
*/

#include "../include.h"

/*
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <string.h>
*/

#include "pico_dsp.h"
#include "font8x8.h"
#include "include.h"

/* This file has own one so remove the one from picolibsdk */
#undef B16

static gfx_mode_t gfxmode = MODE_UNDEFINED;

/* TFT structures / constants */
#define digitalWrite(pin, val) gpio_put(pin, val)

#define SPICLOCK 60000000 
#ifdef USE_VGA
#define SPI_MODE SPI_CPOL_1 
#else
#ifdef ST7789
#ifdef ST7789_POL
#define SPI_MODE SPI_CPOL_0
#else
#define SPI_MODE SPI_CPOL_1
#endif
#endif
#ifdef ILI9341
#define SPI_MODE SPI_CPOL_0
#endif
#endif

#define LINES_PER_BLOCK  64
#define NR_OF_BLOCK      4

#define TFT_SWRESET    0x01
#define TFT_SLPOUT     0x11
#define TFT_INVON      0x21
#define TFT_DISPOFF    0x28
#define TFT_DISPON     0x29
#define TFT_CASET      0x2A
#define TFT_PASET      0x2B
#define TFT_RAMWR      0x2C
#define TFT_MADCTL     0x36
#define TFT_PIXFMT     0x3A
#define TFT_MADCTL_MY  0x80
#define TFT_MADCTL_MX  0x40
#define TFT_MADCTL_MV  0x20
#define TFT_MADCTL_ML  0x10
#define TFT_MADCTL_RGB 0x00
#define TFT_MADCTL_BGR 0x08
#define TFT_MADCTL_MH  0x04

static void SPItransfer(uint8_t val)
{
  uint8_t dat8=val;
  spi_write_blocking(TFT_SPIREG, &dat8, 1);
}

static void SPItransfer16(uint16_t val)
{
  uint8_t dat8[2];
  dat8[0] = val>>8;
  dat8[1] = val&0xff;
  spi_write_blocking(TFT_SPIREG, dat8, 2);
}

#define DELAY_MASK     0x80
static const uint8_t init_commands[] = { 
  1+DELAY_MASK, TFT_SWRESET,  150,
  1+DELAY_MASK, TFT_SLPOUT,   255,
  2+DELAY_MASK, TFT_PIXFMT, 0x55, 10,
  2,            TFT_MADCTL, TFT_MADCTL_MV | TFT_MADCTL_BGR,
  1, TFT_INVON,
  1, TFT_DISPON,
  0
};

/* TFT structures / constants */
#define RGBVAL16(r,g,b)  ( (((r>>3)&0x1f)<<11) | (((g>>2)&0x3f)<<5) | (((b>>3)&0x1f)<<0) )

static uint16_t * blocks[NR_OF_BLOCK];
static uint16_t blocklens[NR_OF_BLOCK];
static dma_channel_config dmaconfig;
static uint dma_tx=0;
static volatile uint8_t rstop = 0;
static volatile bool cancelled = false;
static volatile uint8_t curTransfer = 0;
static uint8_t nbTransfer = 0;
uint32_t nFrames = 0;

/* VGA structures / constants */
#define R16(rgb) ((rgb>>8)&0xf8) 
#define G16(rgb) ((rgb>>3)&0xfc) 
#define B16(rgb) ((rgb<<3)&0xf8) 
#ifdef VGA222
#define VGA_RGB(r,g,b)   ( (((r>>6)&0x03)<<4) | (((g>>6)&0x03)<<2) | (((b>>6)&0x3)<<0) )
#else
#define VGA_RGB(r,g,b)   ( (((r>>5)&0x07)<<5) | (((g>>5)&0x07)<<2) | (((b>>6)&0x3)<<0) )
#endif

// 8 bits 320x240 frame buffer => 64K
static vga_pixel * visible_framebuffer = NULL;
static vga_pixel * framebuffer = NULL;
static vga_pixel * fb0 = NULL;
static vga_pixel * fb1 = NULL;

static int  fb_width;
static int  fb_height;
static int  fb_stride;

static semaphore_t core1_initted;
static void core1_func();
static void core1_sio_irq();

static void core1_func()
{ }

void PICO_DSP::setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
  int dx=0;
  int dy=0;
#ifdef ST7789
  if (TFT_REALWIDTH == TFT_REALHEIGHT)
  {
#ifdef ROTATE_SCREEN
    if (!flipped) {
      dy += 80;    
    }
#else  
    if (flipped) {
      dx += 80;    
    }
#endif      
  }
#endif  

  digitalWrite(_dc, 0);
  SPItransfer(TFT_CASET);
  digitalWrite(_dc, 1);
  SPItransfer16(x1+dx);
  digitalWrite(_dc, 1);
  SPItransfer16(x2+dx);  
  digitalWrite(_dc, 0);
  SPItransfer(TFT_PASET);
  digitalWrite(_dc, 1);
  SPItransfer16(y1+dy);
  digitalWrite(_dc, 1);
  SPItransfer16(y2+dy);  

  digitalWrite(_dc, 0);
  SPItransfer(TFT_RAMWR);
  digitalWrite(_dc, 1);
 
  return; 
}


PICO_DSP::PICO_DSP()
{
}

gfx_error_t PICO_DSP::begin(gfx_mode_t mode)
{
  switch(mode) {
    case MODE_TFT_320x240:
      gfxmode = mode;
      fb_width = TFT_WIDTH;
      fb_height = TFT_HEIGHT;      
      fb_stride = fb_width;    
      _cs   = TFT_CS;
      _dc   = TFT_DC;
      _rst  = TFT_RST;
      _mosi = TFT_MOSI;
      _sclk = TFT_SCLK;
      _bkl = TFT_BACKLIGHT;

      DispInit(1);
      break;
  }


  return(GFX_OK);
}

void PICO_DSP::end()
{
}

gfx_mode_t PICO_DSP::getMode(void)
{
  return gfxmode;
}

bool PICO_DSP::isflipped(void)
{
  return(flipped);
}
  

/***********************************************************************************************
    DMA functions
 ***********************************************************************************************/
static void dma_isr() { 
  irq_clear(DMA_IRQ_0);
  dma_hw->ints0 = 1u << dma_tx;
  //curTransfer++;
  //if (curTransfer >= nbTransfer) {
  //  curTransfer = 0;
    nFrames++;
  //}
  if (cancelled) {
    rstop = 1;
  }
  else 
  {
    dma_channel_transfer_from_buffer_now(dma_tx, blocks[0], TFT_HEIGHT*TFT_WIDTH);
    //dma_channel_transfer_from_buffer_now(dma_tx, blocks[curTransfer],blocklens[curTransfer]);
  }  
}

static void setDmaStruct() {
  // Setup the control channel
  if (dma_tx == 0)  {
    dma_tx = dma_claim_unused_channel(true);
  }    
  dmaconfig = dma_channel_get_default_config(dma_tx);
  channel_config_set_transfer_data_size(&dmaconfig, DMA_SIZE_16);
  channel_config_set_dreq(&dmaconfig, TFT_SPIDREQ);
  channel_config_set_bswap(&dmaconfig, true);
  //channel_config_set_read_increment(&dmaconfig, true); // read incrementing
  //channel_config_set_write_increment(&dmaconfig, false); // no write incrementing

  dma_channel_configure(
      dma_tx,
      &dmaconfig,
      &spi_get_hw(TFT_SPIREG)->dr, // write address
      blocks[0],
      TFT_HEIGHT*TFT_WIDTH,
      false
  ); 

  irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
  dma_channel_set_irq0_enabled(dma_tx, true);
  irq_set_enabled(DMA_IRQ_0, true);
  dma_hw->ints0 = 1u << dma_tx;  
}

static size_t fb_used = 0;

static inline uint16_t* fb_malloc(size_t len)
{
  uint32_t addr = (uint32_t)FrameBuf + fb_used;

  fb_used += len;

  return (uint16_t*) addr;
}

void PICO_DSP::startRefresh(void) {
  if (gfxmode == MODE_TFT_320x240) {
    uint32_t remaining = TFT_HEIGHT*TFT_WIDTH*2;
    int i=0;
    nbTransfer = 0;
    while (remaining > 0) {
      uint16_t * fb = blocks[i];
      int32_t len = (remaining >= (LINES_PER_BLOCK*TFT_WIDTH*2)?LINES_PER_BLOCK*TFT_WIDTH*2:remaining);
      switch (i) {
        case 0:
          if (fb == 0) fb = fb_malloc(len);
          break;
        case 1:
          if (fb == 0) fb = fb_malloc(len);
          break;
        case 2:
          if (fb == 0) fb = fb_malloc(len);
          break;
        case 3:
          if (fb == 0) fb = fb_malloc(len);
          break;
      }
      blocks[i] = fb;
      blocklens[i] = len/2;
      if (blocks[i] == 0) {
        fillScreenNoDma(RGBVAL16(0xFF,0xFF,0x00));
        printf("FB allocaltion failed for block %d\n",i);
        sleep_ms(10000);    
      }
      nbTransfer++;
      remaining -= len;    
      i++;
    }                
    curTransfer = 0;  
    rstop = 0;     
#if 1
    digitalWrite(_cs, 1);
    setDmaStruct();
#endif
    fillScreen(RGBVAL16(0x00,0x00,0x00));
#if 1
    digitalWrite(_cs, 0);
    setArea((TFT_REALWIDTH-TFT_WIDTH)/2, (TFT_REALHEIGHT-TFT_HEIGHT)/2, (TFT_REALWIDTH-TFT_WIDTH)/2 + TFT_WIDTH-1, (TFT_REALHEIGHT-TFT_HEIGHT)/2+TFT_HEIGHT-1);  
    // we switch to 16bit mode!!
    spi_set_format(TFT_SPIREG, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    dma_start_channel_mask(1u << dma_tx);    
#endif

  }
  else { 
    fillScreen(RGBVAL16(0x00,0x00,0x00));   
  }
}

void PICO_DSP::stopRefresh(void) {
  if (gfxmode == MODE_TFT_320x240) {
    rstop = 1;
    unsigned long m = time_us_32()*1000;   
    cancelled = true; 
    while (!rstop)  {
      if ((time_us_32()*1000 - m) > 100) break;
      sleep_ms(100);
      asm volatile("wfi");
    };
    rstop = 0;
    sleep_ms(100);
    cancelled = false;  
    //dmatx.detachInterrupt();
    fillScreen(RGBVAL16(0x00,0x00,0x00));
    digitalWrite(_cs, 1);
    // we switch back to GFX mode!!
    begin(gfxmode);
    setArea(0, 0, TFT_REALWIDTH-1, TFT_REALHEIGHT-1);    
  }
}


/***********************************************************************************************
    GFX functions
 ***********************************************************************************************/
// retrieve size of the frame buffer
int PICO_DSP::get_frame_buffer_size(int *width, int *height)
{
  if (width != nullptr) *width = fb_width;
  if (height != nullptr) *height = fb_height;
  return fb_stride;
}

void PICO_DSP::waitSync()
{
  if (gfxmode == MODE_TFT_320x240) {
  }
//  else { 
//    WaitVSync();
//  }
}

void PICO_DSP::waitLine(int line)
{
  if (gfxmode == MODE_TFT_320x240) {
  }
  else { 
//  while (currentLine != line) {};
  }
}


/***********************************************************************************************
    GFX functions
 ***********************************************************************************************/
void PICO_DSP::fillScreen(dsp_pixel color) {
  int i,j;
  if (gfxmode == MODE_TFT_320x240) {
    for (j=0; j<TFT_HEIGHT; j++)
    {
      uint16_t * block=blocks[j>>6];
      uint16_t * dst=&block[(j&0x3F)*fb_stride];
      for (i=0; i<fb_width; i++)
      {
        *dst++ = color;
      }
    }
 //   DispUpdateAll();
  }
}

void PICO_DSP::writeLine(int width, int height, int y, dsp_pixel *buf) {
  if (gfxmode == MODE_TFT_320x240) {
    uint16_t * block=blocks[y>>6];
    uint16_t * dst=&block[(y&0x3F)*fb_stride];
    if (width > fb_width) {
#ifdef TFT_LINEARINT   
      int delta = (width/(width-fb_width))-1;   
      int pos = delta;
      for (int i=0; i<fb_width; i++)
      {
        uint16_t val = *buf++;
        pos--;      
        if (pos == 0) {
#ifdef LINEARINT_HACK        
          val  = ((uint32_t)*buf++ + val)/2;
#else
          uint16_t val2 = *buf++;
          val = RGBVAL16((R16(val)+R16(val2))/2,(G16(val)+G16(val2))/2,(B16(val)+B16(val2))/2);
 #endif        
          pos = delta;
        }
        *dst++=val;
      }
  #else
      int step = ((width << 8)/fb_width);
      int pos = 0;
      for (int i=0; i<fb_width; i++)
      {
        *dst++=buf[pos >> 8];
        pos +=step;
      }  
  #endif       
    }
    else if ((width*2) == fb_width) 
    {
      for (int i=0; i<width; i++)
      {
        *dst++=*buf;
        *dst++=*buf++;
      }       
    }
    else
    {
      if (width <= fb_width) {
        dst += (fb_width-width)/2;
      }
      for (int i=0; i<width; i++)
      {
        *dst++=*buf++;
      }       
    }    
  }
}

/***********************************************************************************************
    No DMA functions
 ***********************************************************************************************/
void PICO_DSP::fillScreenNoDma(dsp_pixel color) {
  if (gfxmode == MODE_TFT_320x240) {
    ////digitalWrite(_cs, 0);
    ////setArea(0, 0, TFT_REALWIDTH-1, TFT_REALHEIGHT-1);  
    DispStartImg(0, TFT_REALWIDTH, 0, TFT_REALHEIGHT);
    int i,j;
    for (j=0; j<TFT_REALHEIGHT; j++)
    {
      for (i=0; i<TFT_REALWIDTH; i++) {
        //digitalWrite(_dc, 1);
	////SPItransfer16(color);
	DispSendImg2(color);
      }
    }
#ifdef ILI9341  
    digitalWrite(_dc, 0);
    SPItransfer(ILI9341_SLPOUT);
    digitalWrite(_dc, 1);
#endif
    ////digitalWrite(_cs, 1);
    ////setArea(0, 0, (TFT_REALWIDTH-1), (TFT_REALHEIGHT-1));
    DispStopImg();
  }
}

void PICO_DSP::drawRectNoDma(int16_t x, int16_t y, int16_t w, int16_t h, dsp_pixel color) {
  if (gfxmode == MODE_TFT_320x240) {
    ////digitalWrite(_cs, 0);
    ////setArea(x,y,x+w-1,y+h-1);
    DispStartImg(x,x+w,y,y+h);
    int i;
    for (i=0; i<(w*h); i++)
    {
      ////SPItransfer16(color);
      DispSendImg2(color);
    }
#ifdef ILI9341  
    digitalWrite(_dc, 0);
    SPItransfer(ILI9341_SLPOUT);
    digitalWrite(_dc, 1);
#endif
    ////digitalWrite(_cs, 1);
    ////setArea(0, 0, (TFT_REALWIDTH-1), (TFT_REALHEIGHT-1));   
    DispStopImg();
  }
}

void PICO_DSP::drawTextNoDma(int16_t x, int16_t y, const char * text, dsp_pixel fgcolor, dsp_pixel bgcolor, bool doublesize) {
  if (gfxmode == MODE_TFT_320x240) {
    uint16_t c;
    while ((c = *text++)) {
      const unsigned char * charpt=&font8x8[c][0];
      ////digitalWrite(_cs, 0);
      ////setArea(x,y,x+7,y+(doublesize?15:7));
      DispStartImg(x,x+8,y,y+(doublesize?16:8));
      for (int i=0;i<8;i++)
      {
        unsigned char bits;
        if (doublesize) {
          bits = *charpt;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);
          bits = bits >> 1;     
          if (bits&0x01) DispSendImg2(fgcolor);
          else DispSendImg2(bgcolor);       
        }
        bits = *charpt++;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) DispSendImg2(fgcolor);
        else DispSendImg2(bgcolor);
      }
      x +=8;
#ifdef ILI9341  
      digitalWrite(_dc, 0);
      SPItransfer(ILI9341_SLPOUT);
      digitalWrite(_dc, 1);
#endif
      ////digitalWrite(_cs, 1); 
      DispStopImg();
    }

    ////digitalWrite(_cs, 0);
    ////setArea(0, 0, (TFT_REALWIDTH-1), (TFT_REALHEIGHT-1));  
    ////digitalWrite(_cs, 1);
    DispStopImg();
  }
}

/*******************************************************************
 Experimental PWM interrupt based sound driver !!!
*******************************************************************/
//#include "hardware/irq.h"
//#include "hardware/pwm.h"

static bool fillfirsthalf = true;
static uint16_t cnt = 0;
static uint16_t sampleBufferSize = 0;

static uint32_t * i2s_tx_buffer;
static short * i2s_tx_buffer16;

u32 timeSWISR = 0;

static void SOFTWARE_isr() {
  u32 start = Time();
  if (fillfirsthalf) {
    SND_Process((short *)i2s_tx_buffer, sampleBufferSize);
  }  
  else { 
    SND_Process((short *)&i2s_tx_buffer[sampleBufferSize/2], sampleBufferSize);
  }
  timeSWISR += Time() - start;
}

static void AUDIO_isr() {
  pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
  long s = i2s_tx_buffer16[cnt++]; 
  s += i2s_tx_buffer16[cnt++];
  s = s/2 + 32767; 
  pwm_set_gpio_level(AUDIO_PIN, s >> 8);
  cnt = cnt & (sampleBufferSize*2-1);

  if (cnt == 0) {
    fillfirsthalf = false;
    //irq_set_pending(RTC_IRQ+1);
    multicore_fifo_push_blocking(0);
  } 
  else if (cnt == sampleBufferSize) {
    fillfirsthalf = true;
    //irq_set_pending(RTC_IRQ+1);
    multicore_fifo_push_blocking(0);
  }
}

static void core1_sio_irq() {
  irq_clear(SIO_IRQ_PROC1);
  while(multicore_fifo_rvalid()) {
    uint16_t raw = multicore_fifo_pop_blocking();
    SOFTWARE_isr();
  } 
  multicore_fifo_clear_irq();
}

static void core1_func_tft() {
    multicore_fifo_clear_irq();
    irq_set_exclusive_handler(SIO_IRQ_PROC1,core1_sio_irq);
    //irq_set_priority (SIO_IRQ_PROC1, 129);    
    irq_set_enabled(SIO_IRQ_PROC1,true);

    while (true) {
        tight_loop_contents();
    }
}

void PICO_DSP::begin_audio(int samplesize)
{
  i2s_tx_buffer =  (uint32_t*)malloc(samplesize*sizeof(uint32_t));

  if (i2s_tx_buffer == NULL) {
    printf("sound buffer could not be allocated!!!!!\n");
    return;  
  }
  memset((void*)i2s_tx_buffer,0, samplesize*sizeof(uint32_t));
  printf("sound buffer allocated\n");

  i2s_tx_buffer16 = (short*)i2s_tx_buffer;

  sampleBufferSize = samplesize;

  gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

  if (gfxmode == MODE_TFT_320x240) {
    multicore_launch_core1(core1_func_tft);
  }
  
  int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
  // Setup PWM interrupt to fire when PWM cycle is complete
  pwm_clear_irq(audio_pin_slice);
  pwm_set_irq_enabled(audio_pin_slice, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, AUDIO_isr);
  irq_set_priority (PWM_IRQ_WRAP, 128);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  //irq_set_exclusive_handler(RTC_IRQ+1,SOFTWARE_isr);
  //irq_set_priority (RTC_IRQ+1, 120);
  //irq_set_enabled(RTC_IRQ+1,true);


  // Setup PWM for audio output
  pwm_config config = pwm_get_default_config();
//  pwm_config_set_clkdiv(&config, 5.5f);
  pwm_config_set_clkdiv(&config, 50.0f);
  pwm_config_set_wrap(&config, 254);
  pwm_init(audio_pin_slice, &config, true);

  pwm_set_gpio_level(AUDIO_PIN, 0);
  printf("sound initialized\n");
}
 
void PICO_DSP::end_audio()
{
  if (i2s_tx_buffer != NULL) {
    free(i2s_tx_buffer);
  }  
}


 

