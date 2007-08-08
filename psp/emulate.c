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

extern EmulatorConfig Config;
extern GameConfig ActiveGameConfig;
extern const u64 ButtonMask[];
extern const int ButtonMapId[];

static int ClearScreen;
static int ScreenX;
static int ScreenY;
static int ScreenW;
static int ScreenH;
static PspFpsCounter FpsCounter;
static int JoyState[4] =  { 0xff, 0xff, 0xff, 0xff };
static int TrigState[4] = { 1, 1, 1, 1 };
static const int ScreenBufferSkip = SCREEN_BUFFER_WIDTH - ATARI_WIDTH;

PspImage *Screen;

static int ParseInput();
static void Copy_Screen_Buffer();
static void AudioCallback(void* buf, unsigned int *length, void *userdata);

/* Initialize emulation */
int InitEmulation()
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
  if (!Atari800_Initialise(0, NULL))
  {
    pspImageDestroy(Screen);
    return 0;
  }

  return 1;
}

/* Release emulation resources */
void TrashEmulation()
{
  pspImageDestroy(Screen);
  Atari800_Exit(FALSE);
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
void Copy_Screen_Buffer()
{
  int i, j;
  u8 *screen, *image;

  /* TODO: copy only the middle 336 columns */
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
  return key_code;
}

int Atari_PORT(int num)
{
  return JoyState[num];
}

int Atari_TRIG(int num)
{
  return TrigState[num];
}

#ifdef SOUND

void Sound_Initialise(int *argc, char *argv[])
{
  Pokey_sound_init(FREQ_17_EXACT, 44100, 1, 0);
}

void AudioCallback(void* buf, unsigned int *length, void *userdata)
{
  static char buffer[44100 / 50];
  unsigned int nsamples = (tv_mode == TV_NTSC) ? (44100 / 60) : (44100 / 50);
  Pokey_process(buffer, nsamples);

  PspSample *OutBuf = (PspSample*)buf;
  int i;

  /* TODO: test sound rate when TV mode is switched - currently hardcoded */
  /* to PAL */
  for(i = 0; i < nsamples; i++) 
    OutBuf[i].Left = OutBuf[i].Right = buffer[i];
}
void Sound_Update(void)
{
}

void Sound_Pause(void)
{
  pspAudioSetChannelCallback(0, NULL, 0);
}

void Sound_Continue(void)
{
  pspAudioSetChannelCallback(0, AudioCallback, 0);
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

/* Run emulation */
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
    ScreenW = (float)Screen->Viewport.Width * ratio;
    ScreenH = SCR_HEIGHT;
    break;
  case DISPLAY_MODE_FILL_SCREEN:
    ScreenW = SCR_WIDTH;
    ScreenH = SCR_HEIGHT;
    break;
  }

  ScreenX = (SCR_WIDTH / 2) - (ScreenW / 2);
  ScreenY = (SCR_HEIGHT / 2) - (ScreenH / 2);
  ClearScreen = 1;

  /* Reset emulation preferences */
  refresh_rate = Config.Frameskip + 1;

  /* Start emulation - main loop*/
  while (!ExitPSP)
  {
    /* Check input */
    if (ParseInput()) break;

    /* TODO: implement vsync and frequency manipulation */
    Atari800_Frame();
    if (display_screen)
      Atari_DisplayScreen();
  }
}

int ParseInput()
{
  /* Clear keyboard and joystick state */
  key_code = AKEY_NONE;
  key_consol = CONSOL_NONE;
  JoyState[0] = 0xff;
  TrigState[0] = 1;
  key_shift = 0;

  /* Get PSP input */
  static SceCtrlData pad;
  if (!pspCtrlPollControls(&pad))
    return 0;

  /* DEBUGGING
  if ((pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_START))
    == (PSP_CTRL_SELECT | PSP_CTRL_START))
      pspUtilSaveVramSeq(ScreenshotPath, "game");
  //*/

  int i, on, code, key_ctrl;

  /* Parse input */
  key_ctrl = 0;
  for (i = 0; ButtonMapId[i] >= 0; i++)
  {
    code = ActiveGameConfig.ButtonConfig[ButtonMapId[i]];
    on = (pad.Buttons & ButtonMask[i]) == ButtonMask[i];

    /* Check to see if a button set is pressed. If so, unset it, so it */
    /* doesn't trigger any other combination presses. */
    if (on) pad.Buttons &= ~ButtonMask[i];

    if (code & JOY)      /* Joystick */
    {
      if (on) JoyState[0] &= 0xf0 | CODE_MASK(code);
    }
    else if (code & TRG) /* Trigger */
    {
      if (on) TrigState[0] = CODE_MASK(code);
    }
    else if (code & KBD) /* Keyboard */
    {
      if (on) key_code = CODE_MASK(code);
    }
    else if (code & CSL) /* Console */
    {
      if (on) key_consol ^= CODE_MASK(code);
    }
    else if (code & STA) /* State-based (shift/ctrl) */
    {
      if (on)
      {
        switch (CODE_MASK(code))
        {
        case AKEY_SHFT: key_shift = 1; break;
        case AKEY_CTRL: key_ctrl  = 1; break;
        }
      }
    }
    else if (code & SPC) /* Emulator-specific */
    {
      int inverted = -(int)CODE_MASK(code);
      switch (inverted)
      {
      case AKEY_EXIT:
        if (on) return 1;
        break;
      default:
        if (on && key_code == AKEY_NONE) 
          key_code = inverted;
      }
    }

    if (key_code != AKEY_NONE)
    {
      if (machine_type == MACHINE_5200)
      {
        if (key_shift && 
          !(key_code == AKEY_5200_HASH || key_code == AKEY_5200_ASTERISK))
            key_code |= AKEY_SHFT;
      }
      else
      {
        if (key_shift) key_code |= AKEY_SHFT;
        if (key_ctrl)  key_code |= AKEY_CTRL;
      }
    }
  }

  return 0;
}

