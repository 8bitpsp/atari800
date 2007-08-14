#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "video.h"
#include "psp.h"
#include "ctrl.h"
#include "perf.h"
#include "image.h"
#include "kybd.h"

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

extern UBYTE cim_encountered;

static int ClearScreen;
static int ScreenX;
static int ScreenY;
static int ScreenW;
static int ScreenH;
static int SoundReady;
static int ShowKybd;
static PspFpsCounter FpsCounter;
static PspKeyboardLayout *KeyboardLayout, *KeypadLayout;
static int JoyState[4] =  { 0xff, 0xff, 0xff, 0xff };
static int TrigState[4] = { 1, 1, 1, 1 };
static char SoundBuffer[SOUND_FREQ / 50];
static const int ScreenBufferSkip = SCREEN_BUFFER_WIDTH - ATARI_WIDTH;

PspImage *Screen;

static int ParseInput();
static void Copy_Screen_Buffer();
static void AudioCallback(void* buf, unsigned int *length, void *userdata);
inline void HandleKeyInput(unsigned int code, int on);

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

  /* Initialize computer keyboard layout */
  KeyboardLayout = pspKybdLoadLayout("atari800.lyt", NULL, HandleKeyInput);
  KeypadLayout = pspKybdLoadLayout("atari5200.lyt", NULL, HandleKeyInput);

  /* Initialize performance counter */
  pspPerfInitFps(&FpsCounter);

  /* Initialize emulator */
  int foo = 0;
  if (!Atari800_Initialise(&foo, NULL))
  {
    pspImageDestroy(Screen);
    return 0;
  }

  return 1;
}

/* Release emulation resources */
void TrashEmulation()
{
  /* Destroy keyboard layout */
  if (KeyboardLayout) pspKybdDestroyLayout(KeyboardLayout);
  if (KeypadLayout) pspKybdDestroyLayout(KeypadLayout);

  pspImageDestroy(Screen);
  Atari800_Exit(FALSE);
}

void Atari_Initialise(int *argc, char *argv[])
{
#ifdef SOUND
  SoundReady = 0;
	Sound_Initialise(argc, argv);
#endif
}

int Atari_Exit(int run_monitor)
{
	Aflushlog();
	return (cim_encountered) ? TRUE : FALSE;
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
    for (j = 0; j < ATARI_WIDTH; j++) *image++ = *screen++;
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

  /* Draw keyboard */
  if (ShowKybd) pspKybdRender((machine_type == MACHINE_5200) 
    ? KeypadLayout : KeyboardLayout);

  if (Config.ShowFps)
  {
    static char fps_display[64];
    sprintf(fps_display, " %3.02f (%3i%%) ", 
      pspPerfGetFps(&FpsCounter), percent_atari_speed);

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
	enable_new_pokey = 0;
  Pokey_sound_init(FREQ_17_EXACT, SOUND_FREQ, 1, 0);
}

void AudioCallback(void* buf, unsigned int *length, void *userdata)
{
  PspSample *OutBuf = (PspSample*)buf;

  if (!SoundReady)
  {
    memset(OutBuf, 0, sizeof(PspSample) * *length);
    return;
  }

  int i;
  for(i = 0; i < SoundReady; i++) 
  {
    int sample = ((int)SoundBuffer[i] - 0x80) << 8;
    OutBuf[i].Left = OutBuf[i].Right = (sample > 32767) ? 32767 
      : ((sample < -32768) ? 32768 : sample);
  }

  *length = SoundReady;
  SoundReady = 0;
}

void Sound_Update(void)
{
	unsigned int nsamples = (tv_mode == TV_NTSC) 
		? PSP_AUDIO_SAMPLE_ALIGN(SOUND_FREQ / 60) 
		: PSP_AUDIO_SAMPLE_ALIGN(SOUND_FREQ / 50);

  Pokey_process(SoundBuffer, nsamples);
  SoundReady = nsamples;
}

void Sound_Pause(void)
{
  pspAudioSetChannelCallback(0, NULL, 0);
}

void Sound_Continue(void)
{
  SoundReady = 0;
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
  ShowKybd = 0;

  /* Reset emulation preferences */
  refresh_rate = Config.Frameskip + 1;

#ifdef SOUND
  /* Resume sound */
	Sound_Continue();
#endif

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

#ifdef SOUND
  /* Stop sound */
  Sound_Pause();
#endif
}

inline void HandleKeyInput(unsigned int code, int on)
{
  if (code & KBD) /* Keyboard */
  { if (on) key_code = CODE_MASK(code); }
  else if (code & CSL) /* Console */
  { if (on) key_consol ^= CODE_MASK(code); }
  else if (code & STA) /* State-based (shift/ctrl) */
  {
    if (on)
    {
      switch (CODE_MASK(code))
      {
      case AKEY_SHFT: key_shift = 1; break;
//      case AKEY_CTRL: key_ctrl  = 1; break;
      }
    }
  }
}

int ParseInput()
{
  /* Clear keyboard and joystick state */
  if (!ShowKybd)
  {
	  key_code = AKEY_NONE;
	  key_consol = CONSOL_NONE;
	  JoyState[0] = 0xff;
	  TrigState[0] = 1;
	  key_shift = 0;
	}

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
  PspKeyboardLayout *layout = (machine_type == MACHINE_5200) 
    ? KeypadLayout : KeyboardLayout;

  /* Navigate the virtual keyboard, if shown */
  if (ShowKybd) pspKybdNavigate(layout, &pad);

  /* Parse input */
  key_ctrl = 0;
  for (i = 0; ButtonMapId[i] >= 0; i++)
  {
    code = ActiveGameConfig.ButtonConfig[ButtonMapId[i]];
    on = (pad.Buttons & ButtonMask[i]) == ButtonMask[i];

    /* Check to see if a button set is pressed. If so, unset it, so it */
    /* doesn't trigger any other combination presses. */
    if (on) pad.Buttons &= ~ButtonMask[i];

    if (!ShowKybd)
    {
	    if (code & JOY)      /* Joystick */
	    { if (on) JoyState[0] &= 0xf0 | CODE_MASK(code); }
	    else if (code & TRG) /* Trigger */
	    { if (on) TrigState[0] = CODE_MASK(code); }
	    else if (code & KBD) /* Keyboard */
	    { if (on) key_code = CODE_MASK(code); }
	    else if (code & CSL) /* Console */
	    { if (on) key_consol ^= CODE_MASK(code); }
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
	  }

    if (code & SPC) /* Emulator-specific */
    {
      int inverted = -(int)CODE_MASK(code);
      switch (inverted)
      {
      case AKEY_EXIT:
        if (on) return 1;
        break;
      case AKEY_SHOW_KEYS:
        if (ShowKybd != on)
        {
          if (on) pspKybdReinit(layout);
          else { pspKybdReleaseAll(layout); ClearScreen = 1; }
        }
        ShowKybd = on;
        break;
      default:
        if (on && key_code == AKEY_NONE) key_code = inverted;
        break;
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

