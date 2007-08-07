#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "video.h"
#include "psp.h"
#include "ctrl.h"
#include "perf.h"
#include "image.h"

#include "config.h"
#include "atari.h"
#include "colours.h"
#include "input.h"
#include "log.h"
#include "monitor.h"
#include "screen.h"
#include "ui.h"
#include "util.h"

#include "pokeysnd.h"
#include "sound.h"

#include "emulate.h"

#define SCREEN_BUFFER_WIDTH 512
const int ScreenBufferSkip = 512 - ATARI_WIDTH;

extern unsigned char audsrv[];
extern unsigned int size_audsrv;

extern unsigned char usbd[];
extern unsigned int size_usbd;

extern unsigned char ps2kbd[];
extern unsigned int size_ps2kbd;

extern EmulatorConfig Config;
extern GameConfig ActiveGameConfig;

static int ClearScreen;
static int ScreenX;
static int ScreenY;
static int ScreenW;
static int ScreenH;

static PspFpsCounter FpsCounter;

PspImage *Screen;

static SceCtrlData ButtonPad;

double Atari_time(void)
{
	static double fake_timer = 0;
	return fake_timer++;
}

void Atari_sleep(double s)
{
/* TODO */
/*
	if (ui_is_active){
	        int i,ret;
	        for (i=0;i<s * 100.0;i++){
	
			ee_sema_t sema;
	                sema.attr = 0;
	                sema.count = 0;
	                sema.init_count = 0;
	                sema.max_count = 1;
	                ret = CreateSema(&sema);
	                if (ret <= 0) {
	                        //could not create sema, strange!  continue anyway.
	                        return;
	                }
	
	                iSignalSema(ret);
	                WaitSema(ret);
	                DeleteSema(ret);
	        }
	}
*/
}


void Atari_Initialise(int *argc, char *argv[])
{
#ifdef SOUND
	Sound_Initialise(argc, argv);
#endif
}

int Atari_Exit(int run_monitor)
{
	Aflushlog();
	return FALSE;
}

/* Copies the atari screen buffer to the image buffer */
static void Copy_Screen_Buffer()
{
  int i, j;
  u8 *screen, *image;

  screen = (u8*)atari_screen;
  image = (u8*)Screen->Pixels;

  for (i = 0; i < ATARI_HEIGHT; i++)
  {
    for (j = 0; j < ATARI_WIDTH; j++)
      *image++ = *screen++;
    image += ScreenBufferSkip;
  }
}

void Atari_DisplayScreen(void)
{
  Copy_Screen_Buffer();

  pspVideoBegin();
  
  /* Clear the buffer first, if necessary */
  if (ClearScreen >= 0)
  {
    ClearScreen--;
    pspVideoClearScreen();
  }

  /* Blit screen */
  pspVideoPutImage(Screen, ScreenX, ScreenY, ScreenW, ScreenH);

  if (Config.ShowFps)
  {
    static char fps_display[32];
    sprintf(fps_display, " %3.02f", pspPerfGetFps(&FpsCounter));

    int width = pspFontGetTextWidth(&PspStockFont, fps_display);
    int height = pspFontGetLineHeight(&PspStockFont);

    pspVideoFillRect(SCR_WIDTH - width, 0, SCR_WIDTH, height, PSP_COLOR_BLACK);
    pspVideoPrint(&PspStockFont, SCR_WIDTH - width, 0, fps_display, PSP_COLOR_WHITE);
  }
  
  pspVideoEnd();
  pspVideoSwapBuffers();
}

int Atari_Keyboard(void)
{
  if (ButtonPad.Buttons & PSP_CTRL_START)
    return AKEY_WARMSTART;
  else if (ButtonPad.Buttons & PSP_CTRL_LEFT)
    return AKEY_LEFT;
  else if (ButtonPad.Buttons & PSP_CTRL_RIGHT)
    return AKEY_RIGHT;
  else if (ButtonPad.Buttons & PSP_CTRL_UP)
    return AKEY_UP;
  else if (ButtonPad.Buttons & PSP_CTRL_DOWN)
    return AKEY_DOWN;

  return AKEY_NONE;
/*
	int new_pad = PadButtons();
	PS2KbdRawKey key;
	key_consol = CONSOL_NONE;

	if (ui_is_active) {
		if (new_pad & PAD_CROSS)
			return AKEY_RETURN;
		if (new_pad & PAD_CIRCLE)
			return AKEY_ESCAPE;
		if (new_pad & PAD_LEFT)
			return AKEY_LEFT;
		if (new_pad & PAD_RIGHT)
			return AKEY_RIGHT;
		if (new_pad & PAD_UP)
			return AKEY_UP;
		if (new_pad & PAD_DOWN)
			return AKEY_DOWN;
		if (new_pad & PAD_L1)
		    return AKEY_COLDSTART;
		if (new_pad & PAD_R1)
			return AKEY_WARMSTART;
	}
	//PAD_CROSS is used for Atari_TRIG().
	if (new_pad & PAD_TRIANGLE)
		return AKEY_UI;
	if (new_pad & PAD_SQUARE)
		return AKEY_SPACE;
	if (new_pad & PAD_CIRCLE)
		return AKEY_RETURN;
	if (new_pad & PAD_L1)
		return AKEY_COLDSTART;
	if (new_pad & PAD_R1)
		return AKEY_WARMSTART;
	if (machine_type == MACHINE_5200) {
		if (new_pad & PAD_START)
			return AKEY_5200_START;
	}
	else {
		if (new_pad & PAD_START)
			key_consol ^= CONSOL_START;
		if (new_pad & PAD_SELECT)
			key_consol ^= CONSOL_SELECT;
		if (new_pad & PAD_CROSS)
			return AKEY_HELP;
	}
if (machine_type != MACHINE_5200 || ui_is_active) {

	while (PS2KbdReadRaw(&key) != 0) {
		if (key.state == PS2KBD_RAWKEY_DOWN) {
			switch (key.key) {
			case EOF:
				Atari800_Exit(FALSE);
				exit(0);
			    break;
			case 0x28:
				return AKEY_RETURN;
			case 0x29:
				return AKEY_ESCAPE;
			case 0x2A:
				return AKEY_BACKSPACE;
			case 0x2B:
				return AKEY_TAB;
			case 0x2C:
				return AKEY_SPACE;
			case 0x46://Print Screen Button
				return AKEY_SCREENSHOT;
			case 0x4F:
			    return AKEY_RIGHT;
			case 0x50:
			    return AKEY_LEFT;
			case 0x51:
			    return AKEY_DOWN;
			case 0x52:
			    return AKEY_UP;
			case 0x58:
			    return AKEY_RETURN;

			case 0xE0:
				PS2KbdCONTROL = 1;
				return AKEY_NONE;
			case 0xE4:
				PS2KbdCONTROL = 1;
				return AKEY_NONE;
			case 0xE1:
				PS2KbdSHIFT = 1;
				return AKEY_NONE;
			case 0xE2:
				PS2KbdALT = 1;
				return AKEY_NONE;
			case 0xE5:
				PS2KbdSHIFT = 1;
				return AKEY_NONE;
			case 0xE6:
				PS2KbdALT = 1;
				return AKEY_NONE;
			default:
				break;
			}
		}

		if ((key.state == PS2KBD_RAWKEY_DOWN) && !PS2KbdSHIFT && !PS2KbdALT) {
			switch (key.key) {
			case 0x1E:
				return AKEY_1;
			case 0X1F:
				return AKEY_2;
			case 0x20:
				return AKEY_3;
			case 0x21:
				return AKEY_4;
			case 0x22:
				return AKEY_5;
			case 0x23:
				return AKEY_6;
			case 0x24:
				return AKEY_7;
			case 0x25:
				return AKEY_8;
			case 0x26:
				return AKEY_9;
			case 0x27:
				return AKEY_0;
			case 0x2D:
				return AKEY_MINUS;
			case 0x2E:
				return AKEY_EQUAL;
			case 0x2F:
				return AKEY_BRACKETLEFT;
			case 0x30:
				return AKEY_BRACKETRIGHT;
			case 0x31:
				return AKEY_BACKSLASH;
			case 0x33:
				return AKEY_SEMICOLON;
			case 0x34:
				return AKEY_QUOTE;
			case 0x35:
				return AKEY_ATARI;
			case 0x36:
				return AKEY_COMMA;
			case 0x37:
				return AKEY_FULLSTOP;
			case 0x38:
				return AKEY_SLASH;
			case 0x3A://F1
				return AKEY_UI;
			case 0x3E://F5
				return AKEY_WARMSTART;
			case 0x42://F9
				return AKEY_EXIT;
			case 0x43://F10
				return AKEY_SCREENSHOT;
			default:
				break;
			}
		}
		if ((key.state == PS2KBD_RAWKEY_DOWN) && PS2KbdSHIFT && !PS2KbdCONTROL && !PS2KbdALT) {
			switch (key.key) {
			case 0x4:
				return AKEY_A;
			case 0x5:
				return AKEY_B;
			case 0x6:
				return AKEY_C;
			case 0x7:
				return AKEY_D;
			case 0x8:
				return AKEY_E;
			case 0x9:
				return AKEY_F;
			case 0xA:
				return AKEY_G;
			case 0xB:
				return AKEY_H;
			case 0xC:
				return AKEY_I;
			case 0xD:
				return AKEY_J;
			case 0xE:
				return AKEY_K;
			case 0xF:
				return AKEY_L;
			case 0x10:
				return AKEY_M;
			case 0x11:
				return AKEY_N;
			case 0x12:
				return AKEY_O;
			case 0x13:
				return AKEY_P;
			case 0x14:
				return AKEY_Q;
			case 0x15:
				return AKEY_R;
			case 0x16:
				return AKEY_S;
			case 0x17:
				return AKEY_T;
			case 0x18:
				return AKEY_U;
			case 0x19:
				return AKEY_V;
			case 0x1A:
				return AKEY_W;
			case 0x1B:
				return AKEY_X;
			case 0x1C:
				return AKEY_Y;
			case 0x1D:
				return AKEY_Z;
			case 0x1E:
				return AKEY_EXCLAMATION;
			case 0X1F:
				return AKEY_AT;
			case 0x20:
				return AKEY_HASH;
			case 0x21:
				return AKEY_DOLLAR;
			case 0x22:
				return AKEY_PERCENT;
			case 0x23:
//				return AKEY_CIRCUMFLEX;
				return AKEY_CARET;
			case 0x24:
				return AKEY_AMPERSAND;
			case 0x25:
				return AKEY_ASTERISK;
			case 0x26:
				return AKEY_PARENLEFT;
			case 0x27:
				return AKEY_PARENRIGHT;
			case 0x2B:
				return AKEY_SETTAB;
			case 0x2D:
				return AKEY_UNDERSCORE;
			case 0x2E:
				return AKEY_PLUS;
			case 0x31:
				return AKEY_BAR;
			case 0x33:
				return AKEY_COLON;
			case 0x34:
				return AKEY_DBLQUOTE;
			case 0x36:
				return AKEY_LESS;
			case 0x37:
				return AKEY_GREATER;
			case 0x38:
				return AKEY_QUESTION;
			case 0x3E://Shift+F5
				return AKEY_COLDSTART;
			case 0x43://Shift+F10
				return AKEY_SCREENSHOT_INTERLACE;
			case 0x49://Shift+Insert key
				return AKEY_INSERT_LINE;
			case 0x4C://Shift+Backspace Key
				return AKEY_DELETE_LINE;
			default:
				break;
			}
		}
		if ((key.state == PS2KBD_RAWKEY_DOWN) && !PS2KbdSHIFT && !PS2KbdCONTROL && !PS2KbdALT) {
			switch (key.key) {
			case 0x4:
				return AKEY_a;
			case 0x5:
				return AKEY_b;
			case 0x6:
				return AKEY_c;
			case 0x7:
				return AKEY_d;
			case 0x8:
				return AKEY_e;
			case 0x9:
				return AKEY_f;
			case 0xA:
				return AKEY_g;
			case 0xB:
				return AKEY_h;
			case 0xC:
				return AKEY_i;
			case 0xD:
				return AKEY_j;
			case 0xE:
				return AKEY_k;
			case 0xF:
				return AKEY_l;
			case 0x10:
				return AKEY_m;
			case 0x11:
				return AKEY_n;
			case 0x12:
				return AKEY_o;
			case 0x13:
				return AKEY_p;
			case 0x14:
				return AKEY_q;
			case 0x15:
				return AKEY_r;
			case 0x16:
				return AKEY_s;
			case 0x17:
				return AKEY_t;
			case 0x18:
				return AKEY_u;
			case 0x19:
				return AKEY_v;
			case 0x1A:
				return AKEY_w;
			case 0x1B:
				return AKEY_x;
			case 0x1C:
				return AKEY_y;
			case 0x1D:
				return AKEY_z;
			case 0x49:
				return AKEY_INSERT_CHAR;
			case 0x4C:
				return AKEY_DELETE_CHAR;
			default:
				break;
			}
		}
		if ((key.state == PS2KBD_RAWKEY_DOWN) && PS2KbdCONTROL && !PS2KbdALT) {
			switch(key.key) {
			case 0x4:
				return AKEY_CTRL_a;
			case 0x5:
				return AKEY_CTRL_b;
			case 0x6:
				return AKEY_CTRL_c;
			case 0x7:
				return AKEY_CTRL_d;
			case 0x8:
				return AKEY_CTRL_e;
			case 0x9:
				return AKEY_CTRL_f;
			case 0xA:
				return AKEY_CTRL_g;
			case 0xB:
				return AKEY_CTRL_h;
			case 0xC:
				return AKEY_CTRL_i;
			case 0xD:
				return AKEY_CTRL_j;
			case 0xE:
				return AKEY_CTRL_k;
			case 0xF:
				return AKEY_CTRL_l;
			case 0x10:
				return AKEY_CTRL_m;
			case 0x11:
				return AKEY_CTRL_n;
			case 0x12:
				return AKEY_CTRL_o;
			case 0x13:
				return AKEY_CTRL_p;
			case 0x14:
				return AKEY_CTRL_q;
			case 0x15:
				return AKEY_CTRL_r;
			case 0x16:
				return AKEY_CTRL_s;
			case 0x17:
				return AKEY_CTRL_t;
			case 0x18:
				return AKEY_CTRL_u;
			case 0x19:
				return AKEY_CTRL_v;
			case 0x1A:
				return AKEY_CTRL_w;
			case 0x1B:
				return AKEY_CTRL_x;
			case 0x1C:
				return AKEY_CTRL_y;
			case 0x1D:
				return AKEY_CTRL_z;
			case 0x1E:
				return AKEY_CTRL_1;
			case 0x1F:
				return AKEY_CTRL_2;
			case 0x20:
				return AKEY_CTRL_3;
			case 0x21:
				return AKEY_CTRL_4;
			case 0x22:
				return AKEY_CTRL_5;
			case 0x23:
				return AKEY_CTRL_6;
			case 0x24:
				return AKEY_CTRL_7;
			case 0x25:
				return AKEY_CTRL_8;
			case 0x26:
				return AKEY_CTRL_9;
			case 0x27:
				return AKEY_CTRL_0;
			case 0x2B:
				return AKEY_CLRTAB;
			case 0x33:
				return AKEY_SEMICOLON | AKEY_CTRL;
			case 0x36:
				return AKEY_LESS | AKEY_CTRL;
			case 0x37:
				return AKEY_GREATER | AKEY_CTRL;
			default:
				break;
			}
		}
		if ((key.state == PS2KBD_RAWKEY_DOWN) && PS2KbdALT) {
			switch(key.key) {
			//case dcr ylsa
			case 0x7:
				alt_function = MENU_DISK;
				return AKEY_UI;
			case 0x6:
				alt_function = MENU_CARTRIDGE;
				return AKEY_UI;
			case 0x15:
				alt_function = MENU_RUN;
				return AKEY_UI;
			case 0x1C:
				alt_function = MENU_SYSTEM;
				return AKEY_UI;
			case 0xF:
				alt_function = MENU_LOADSTATE;
				return AKEY_UI;
			case 0x16:
				alt_function = MENU_SAVESTATE;
				return AKEY_UI;
			case 0x4:
				alt_function = MENU_ABOUT;
				return AKEY_UI;
			default:
				break;
			}
		}


		if (key.state == PS2KBD_RAWKEY_UP) {
			switch (key.key) {
			case 0x39:

			return AKEY_CAPSTOGGLE;
			case 0xE0:
				PS2KbdCONTROL = 0;
				return AKEY_NONE;
			case 0xE4:
				PS2KbdCONTROL = 0;
				return AKEY_NONE;
			case 0xE1:
				PS2KbdSHIFT = 0;
				return AKEY_NONE;
			case 0xE2:
				PS2KbdALT = 0;
				return AKEY_NONE;
			case 0xE5:
				PS2KbdSHIFT = 0;
				return AKEY_NONE;
			case 0xE6:
				PS2KbdALT = 0;
				return AKEY_NONE;
			default:
				break;
			}
		}
	}
}
	return AKEY_NONE;
*/
}

int Atari_PORT(int num)
{
  int ret = 0xff;
  if (num == 0)
  {
    if (ButtonPad.Buttons & PSP_CTRL_ANALLEFT)
      ret &= 0xf0 | STICK_LEFT;
    else if (ButtonPad.Buttons & PSP_CTRL_ANALRIGHT)
      ret &= 0xf0 | STICK_RIGHT;
    else if (ButtonPad.Buttons & PSP_CTRL_ANALUP)
      ret &= 0xf0 | STICK_FORWARD;
    else if (ButtonPad.Buttons & PSP_CTRL_ANALDOWN)
      ret &= 0xf0 | STICK_BACK;
  }
  return ret;
}

int Atari_TRIG(int num)
{
  if (num == 0)
  {
    if (ButtonPad.Buttons & PSP_CTRL_CROSS)
      return 0;
  }
  return 1;
}

#ifdef SOUND

void Sound_Initialise(int *argc, char *argv[])
{
/*
  if (audsrv_init() != 0)
    Aprint("failed to initialize audsrv: %s", audsrv_get_error_string());
  else {
    struct audsrv_fmt_t format;
    format.bits = 8;
    format.freq = 44100;
    format.channels = 1;
    audsrv_set_format(&format);
    audsrv_set_volume(MAX_VOLUME);
    Pokey_sound_init(FREQ_17_EXACT, 44100, 1, 0);
  }
*/
}

void Sound_Update(void)
{
/*
  static char buffer[44100 / 50];
  unsigned int nsamples = (tv_mode == TV_NTSC) ? (44100 / 60) : (44100 / 50);
  Pokey_process(buffer, nsamples);
  audsrv_wait_audio(nsamples);
  audsrv_play_audio(buffer, nsamples);
*/
}

void Sound_Pause(void)
{
/*
  audsrv_stop_audio();
*/
}

void Sound_Continue(void)
{
/*
  if (audsrv_init() != 0)
    Aprint("failed to initialize audsrv: %s", audsrv_get_error_string());
  else {
    struct audsrv_fmt_t format;
    format.bits = 8;
    format.freq = 44100;
    format.channels = 1;
    audsrv_set_format(&format);
    audsrv_set_volume(MAX_VOLUME);
  }
*/
}

#endif /* SOUND */

int Atari_OpenDir(const char *filename)
{
  return FALSE;
}

int Atari_ReadDir(char *fullpath, char *filename, int *isdir,
                  int *readonly, int *size, char *timetext)
{
  return FALSE;
}

int InitEmulation(int *argc, char **argv)
{
  /* Create screen buffer */
  if (!(Screen = pspImageCreateVram(SCREEN_BUFFER_WIDTH, 
  	ATARI_HEIGHT, PSP_IMAGE_INDEXED)))
    	return 0;

  Screen->Viewport.Width = 336;
  Screen->Viewport.X = (ATARI_WIDTH - 336) >> 1;

  /* Initialize palette */
  int i, c;
  for (i = 0; i < 256; i++)
  {
    c = colortable[i];
    Screen->Palette[i] = 
      RGB((c & 0x00ff0000) >> 16,
          (c & 0x0000ff00) >> 8,
          (c & 0x000000ff));
  }

  /* Initialize performance counter */
  pspPerfInitFps(&FpsCounter);

  /* Initialize emulator */
  return Atari800_Initialise(argc, argv);
}

void RunEmulation()
{
  float ratio;

  pspImageClear(Screen, 0);

  /* Recompute screen size/position */
  switch (Config.DisplayMode)
  {
  default:
  case DISPLAY_MODE_UNSCALED:
    ScreenW = Screen->Viewport.Width;
    ScreenH = Screen->Viewport.Height;
    break;
  case DISPLAY_MODE_FIT_HEIGHT:
    ratio = (float)SCR_HEIGHT / (float)Screen->Viewport.Height;
    ScreenW = (float)Screen->Viewport.Width * ratio - 2;
    ScreenH = SCR_HEIGHT;
    break;
  case DISPLAY_MODE_FILL_SCREEN:
    ScreenW = SCR_WIDTH - 3;
    ScreenH = SCR_HEIGHT;
    break;
  }

  ScreenX = (SCR_WIDTH / 2) - (ScreenW / 2);
  ScreenY = (SCR_HEIGHT / 2) - (ScreenH / 2);
  ClearScreen = 1;

  /* Start emulation - main loop*/
  while (!ExitPSP)
  {
    pspCtrlPollControls(&ButtonPad);
    /* TODO */
    if ((ButtonPad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))
      == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))
        break;
    key_code = Atari_Keyboard();
    /* TODO: implement frame skipping, vsync and frequency manipulation */
    Atari800_Frame();
    if (display_screen)
      Atari_DisplayScreen();
  }
}

void TrashEmulation()
{
  pspImageDestroy(Screen);
}

