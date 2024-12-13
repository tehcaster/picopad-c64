#define KEYMAP_PRESENT 1

#define PROGMEM

#include "../include.h"

//#include "pico.h"
//#include "pico/stdlib.h"
//#include "hardware/adc.h"
//#include <stdio.h>
//#include <string.h>

extern "C" {
  #include "emuapi.h"
  #include "../config/iopins.h"
}

static bool emu_writeConfig(void);
static bool emu_readConfig(void);
static bool emu_eraseConfig(void);
static bool emu_writeGfxConfig(void);
static bool emu_readGfxConfig(void);
static bool emu_eraseGfxConfig(void);

#include "pico_dsp.h"
extern PICO_DSP tft;

#define MAX_FILENAME_PATH   64
#define NB_FILE_HANDLER     4
#define AUTORUN_FILENAME    "autorun.txt"
#define GFX_CFG_FILENAME    "gfxmode.txt"

#define MAX_FILES           64
#define MAX_FILENAME_SIZE   24
#define MAX_MENULINES       9
#define TEXT_HEIGHT         16
#define TEXT_WIDTH          8
#define MENU_FILE_XOFFSET   (6*TEXT_WIDTH)
#define MENU_FILE_YOFFSET   (2*TEXT_HEIGHT)
#define MENU_FILE_W         (MAX_FILENAME_SIZE*TEXT_WIDTH)
#define MENU_FILE_H         (MAX_MENULINES*TEXT_HEIGHT)
#define MENU_FILE_BGCOLOR   RGBVAL16(0x00,0x00,0x40)
#define MENU_JOYS_YOFFSET   (12*TEXT_HEIGHT)
#define MENU_VBAR_XOFFSET   (0*TEXT_WIDTH)
#define MENU_VBAR_YOFFSET   (MENU_FILE_YOFFSET)

#define MENU_TFT_XOFFSET    (MENU_FILE_XOFFSET+MENU_FILE_W+8)
#define MENU_TFT_YOFFSET    (MENU_VBAR_YOFFSET+32)
#define MENU_VGA_XOFFSET    (MENU_FILE_XOFFSET+MENU_FILE_W+8)
#define MENU_VGA_YOFFSET    (MENU_VBAR_YOFFSET+MENU_FILE_H-32-37)



static char selection[MAX_FILENAME_PATH]="";

#if (defined(PICOMPUTER) || defined(PICOZX) )
static const unsigned short * keys;
static unsigned char keymatrix[7];
static int keymatrix_hitrow=-1;
static bool key_fn=false;
static bool key_alt=false;
static uint32_t keypress_t_ms=0;
static uint32_t last_t_ms=0;
static uint32_t hundred_ms_cnt=0;
static bool ledflash_toggle=false;
#endif
static int keyMap;

static bool joySwapped = false;
static uint16_t bLastState;
static int xRef;
static int yRef;
static uint8_t usbnavpad=0;

static bool menuOn=true;
static bool autorun=false;


/********************************
 * Generic output and malloc
********************************/ 
void emu_printf(const char * text)
{
  printf("%s\n",text);
}

void emu_printf(int val)
{
  printf("%d\n",val);
}

void emu_printi(int val)
{
  printf("%d\n",val);
}

void emu_printh(int val)
{
  printf("0x%.8\n",val);
}

static int malbufpt = 0;
static char malbuf[EXTRA_HEAP];

void * emu_Malloc(int size)
{
  void * retval =  malloc(size);
  if (!retval) {
    emu_printf("failled to allocate");
    emu_printf(size);
    emu_printf("fallback");
    if ( (malbufpt+size) < sizeof(malbuf) ) {
      retval = (void *)&malbuf[malbufpt];
      malbufpt += size;      
    }
    else {
      emu_printf("failure to allocate");
    }
  }
  else {
    emu_printf("could allocate dynamic ");
    emu_printf(size);    
  }
  
  return retval;
}

void * emu_MallocI(int size)
{
  void * retval =  NULL; 

  if ( (malbufpt+size) < sizeof(malbuf) ) {
    retval = (void *)&malbuf[malbufpt];
    malbufpt += size;
    emu_printf("could allocate static ");
    emu_printf(size);          
  }
  else {
    emu_printf("failure to allocate");
  }

  return retval;
}
void emu_Free(void * pt)
{
  free(pt);
}

/********************************
 * OSKB handling
********************************/ 
#if (defined(ILI9341) || defined(ST7789)) && defined(USE_VGA)
// On screen keyboard position
#define KXOFF      28 //64
#define KYOFF      96
#define KWIDTH     11 //22
#define KHEIGHT    3

static bool oskbOn = false;
static int cxpos = 0;
static int cypos = 0;
static int oskbMap = 0;
static uint16_t oskbBLastState = 0;

static void lineOSKB2(int kxoff, int kyoff, char * str, int row)
{
  char c[2] = {'A',0};
  const char * cpt = str;
  for (int i=0; i<KWIDTH; i++)
  {
    c[0] = *cpt++;
    c[1] = 0;
    uint16_t bg = RGBVAL16(0x00,0x00,0xff);
    if ( (cxpos == i) && (cypos == row) ) bg = RGBVAL16(0xff,0x00,0x00);
    tft.drawTextNoDma(kxoff+8*i,kyoff, &c[0], RGBVAL16(0x00,0xff,0xff), bg, ((i&1)?false:true));
  } 
}

static void lineOSKB(int kxoff, int kyoff, char * str, int row)
{
  char c[4] = {' ',0,' ',0};
  const char * cpt = str;
  for (int i=0; i<KWIDTH; i++)
  {
    c[1] = *cpt++;
    uint16_t bg;
    if (row&1) bg = (i&1)?RGBVAL16(0xff,0xff,0xff):RGBVAL16(0xe0,0xe0,0xe0);
    else bg = (i&1)?RGBVAL16(0xe0,0xe0,0xe0):RGBVAL16(0xff,0xff,0xff);
    if ( (cxpos == i) && (cypos == row) ) bg = RGBVAL16(0x00,0xff,0xff);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+0 , "   ", RGBVAL16(0x00,0x00,0x00), bg, false);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+8 , &c[0], RGBVAL16(0x00,0x00,0x00), bg, true);
    tft.drawTextNoDma(kxoff+24*i,kyoff+32*row+24, "   ", RGBVAL16(0x00,0x00,0x00), bg, false);
  } 
}


static void drawOskb(void)
{
//  lineOSKB2(KXOFF,KYOFF+0,  (char *)"Q1W2E3R4T5Y6U7I8O9P0<=",  0);
//  lineOSKB2(KXOFF,KYOFF+16, (char *)"  A!S@D#F$G%H+J&K*L-EN",  1);
//  lineOSKB2(KXOFF,KYOFF+32, (char *)"  Z(X)C?V/B\"N<M>.,SP  ", 2);
/*  
  if (oskbMap == 0) {
    lineOSKB(KXOFF,KYOFF, keylables_map1_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map1_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map1_2,  2);
  }
  else if (oskbMap == 1) {
    lineOSKB(KXOFF,KYOFF, keylables_map2_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map2_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map2_2,  2);
  }
  else {
    lineOSKB(KXOFF,KYOFF, keylables_map3_0,  0);
    lineOSKB(KXOFF,KYOFF, keylables_map3_1,  1);
    lineOSKB(KXOFF,KYOFF, keylables_map3_2,  2);
  }
*/  
}

void toggleOskb(bool forceoff) {
  if (forceoff) oskbOn=true; 
  if (oskbOn) {
    oskbOn = false;
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,32, "Press USER2 to toggle onscreen keyboard.", RGBVAL16(0xff,0xff,0xff), RGBVAL16(0x00,0x00,0x00), true);
  } else {
    oskbOn = true;
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,32, " Press USER2 to exit onscreen keyboard. ", RGBVAL16(0xff,0xff,0xff), RGBVAL16(0x00,0x00,0x00), true);
    tft.drawTextNoDma(0,64, "    (USER1 to toggle between keymaps)   ", RGBVAL16(0x00,0xff,0xff), RGBVAL16(0x00,0x00,0xff), true);
    tft.drawRectNoDma(KXOFF,KYOFF, 22*8, 3*16, RGBVAL16(0x00,0x00,0xFF));
    drawOskb();        
  }
}

static int handleOskb(void)
{  
  int retval = 0;

  uint16_t bClick = bLastState & ~oskbBLastState;
  oskbBLastState = bLastState;
  /*
  static const char * digits = "0123456789ABCDEF";
  char buf[5] = {0,0,0,0,0};
  int val = bClick;
  buf[0] = digits[(val>>12)&0xf];
  buf[1] = digits[(val>>8)&0xf];
  buf[2] = digits[(val>>4)&0xf];
  buf[3] = digits[val&0xf];
  tft.drawTextNoDma(0,KYOFF+ 64,buf,RGBVAL16(0x00,0x00,0x00),RGBVAL16(0xFF,0xFF,0xFF),1);
  */
  if (bClick & MASK_KEY_USER2)
  { 
    toggleOskb(false);
  }
  if (oskbOn)
  {
    bool updated = true;
    if (bClick & MASK_KEY_USER1)
    { 
      oskbMap += 1;
      if (oskbMap == 3) oskbMap = 0;
    }    
    else if (bClick & MASK_JOY2_LEFT)
    {  
      cxpos++;
      if (cxpos >= KWIDTH) cxpos = 0;
    }
    else if (bClick & MASK_JOY2_RIGHT)
    {  
      cxpos--;
      if (cxpos < 0) cxpos = KWIDTH-1;
    }
    else if (bClick & MASK_JOY2_DOWN)
    {  
      cypos++;
      if (cypos >= KHEIGHT) cypos = 0;
    }
    else if (bClick & MASK_JOY2_UP)
    {  
      cypos--;
      if (cypos < 0) cypos = KHEIGHT-1;
    }
    else if (oskbBLastState & MASK_JOY2_BTN)
    {  
      retval = cypos*KWIDTH+cxpos+1;
      if (retval) {
        retval--;
        //if (retval & 1) retval = key_map2[retval>>1];
        //else retval = key_map1[retval>>1];
        if (oskbMap == 0) {
          retval = key_map1[retval];
        }
        else if (oskbMap == 1) {
          retval = key_map2[retval];
        }
        else {
          retval = key_map3[retval];
        }
        //if (retval) { toggleOskb(true); updated=false; };
      }
    }
    else {
      updated=false;
    }    
    if (updated) drawOskb();
  }

  return retval;    
}
#endif

/********************************
 * File IO
********************************/ 
sFile file;
int emu_FileOpen(const char * filepath, const char * mode)
{
  int retval = 0;

  emu_printf("FileOpen...");
  emu_printf(filepath);
  if( (FileOpen(&file, filepath)) ) {
    retval = 1;  
  }
  else {
    emu_printf("FileOpen failed");
  }
  return (retval);
}

int emu_FileRead(void * buf, int size, int handler)
{
  unsigned int retval=0;
  retval = FileRead(&file, buf, size);
  return retval; 
}

void emu_FileClose(int handler)
{
  FileClose(&file);
}

unsigned int emu_FileSize(const char * filepath)
{
  int filesize=0;
  emu_printf("FileSize...");
  emu_printf(filepath);

  return GetFileSize(filepath);
}

#ifdef XXXTODO
static FIL outfile; 

static bool emu_writeGfxConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, "/" GFX_CFG_FILENAME, FA_CREATE_NEW | FA_WRITE)) ) {
    f_close(&outfile);
    retval = true;
  } 
  return retval;   
}

static bool emu_readGfxConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, "/" GFX_CFG_FILENAME, FA_READ)) ) {
    f_close(&outfile);
    retval = true;
  }  
  return retval;   
}

static bool emu_eraseGfxConfig(void)
{
  f_unlink ("/" GFX_CFG_FILENAME);
  return true;
}

static bool emu_writeConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, ROMSDIR "/" AUTORUN_FILENAME, FA_CREATE_NEW | FA_WRITE)) ) {
    unsigned int sizeread=0;
    if( (f_write (&outfile, selection, strlen(selection), &sizeread)) ) {
      emu_printf("Config write failed");        
    }
    else {
      retval = true;
    }  
    f_close(&outfile);   
  } 
  return retval; 
}

static bool emu_readConfig(void)
{
  bool retval = false;
  if( !(f_open(&outfile, ROMSDIR "/" AUTORUN_FILENAME, FA_READ)) ) {
    unsigned int filesize = f_size(&outfile);
    unsigned int sizeread=0;
    if( (f_read (&outfile, selection, filesize, &sizeread)) ) {
      emu_printf("Config read failed");        
    }
    else {
      if (sizeread == filesize) {
        selection[filesize]=0;
        retval = true;
      }
    }  
    f_close(&outfile);   
  }  
  return retval; 
}

static bool emu_eraseConfig(void)
{
  f_unlink (ROMSDIR "/" AUTORUN_FILENAME);
  return true;
}
#endif

/********************************
 * Initialization
********************************/ 
void emu_init(void)
{
      tft.begin(MODE_TFT_320x240);
}


void emu_start(void)
{
  usbnavpad = 0;

  keyMap = 0;
}
