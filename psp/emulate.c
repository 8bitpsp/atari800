#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psptypes.h>
#include <psprtc.h>

#include "pl_snd.h"
#include "video.h"
#include "pl_psp.h"
#include "ctrl.h"
#include "pl_perf.h"
#include "image.h"
#include "pl_vk.h"

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
extern int CropScreen;
extern char *ScreenshotPath;

extern UBYTE cim_encountered;

static int ClearScreen;
static int ScreenX;
static int ScreenY;
static int ScreenW;
static int ScreenH;
static int Frame;
static int TicksPerUpdate;
static u32 TicksPerSecond;
static u64 LastTick;
static u64 CurrentTick;
static int ShowKybd;
static int key_ctrl;
static pl_perf_counter FpsCounter;
static PspKeyboardLayout *KeyboardLayout, *KeypadLayout;
static int JoyState[4] =  { 0xff, 0xff, 0xff, 0xff };
static int TrigState[4] = { 1, 1, 1, 1 };
static const int ScreenBufferSkip = SCREEN_BUFFER_WIDTH - ATARI_WIDTH;

PspImage *Screen;

static int ParseInput();
static void CopyScreenBuffer();
static void AudioCallback(pl_snd_sample* buf, unsigned int samples, void *userdata);
static inline void HandleKeyInput(unsigned int code, int on);

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
  pl_perf_init_counter(&FpsCounter);

  /* Initialize emulator */
  int foo = 0;
  if (!Atari800_Initialise(&foo, NULL))
  {
    pspImageDestroy(Screen);
    return 0;
  }

  pl_snd_set_callback(0, AudioCallback, 0);

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
	Sound_Initialise(argc, argv);
#endif
}

int Atari_Exit(int run_monitor)
{
	Aflushlog();
	return (cim_encountered) ? TRUE : FALSE;
}

/* Copies the atari screen buffer to the image buffer */
void CopyScreenBuffer()
{
  int i, j;
  u8 *screen, *image;

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
  CopyScreenBuffer();

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
    sprintf(fps_display, " %3.02f ", pl_perf_update_counter(&FpsCounter));

    int width = pspFontGetTextWidth(&PspStockFont, fps_display);
    int height = pspFontGetLineHeight(&PspStockFont);

    pspVideoFillRect(SCR_WIDTH - width, 0, SCR_WIDTH, height, PSP_COLOR_BLACK);
    pspVideoPrint(&PspStockFont, SCR_WIDTH - width, 0, fps_display, PSP_COLOR_WHITE);
  }

  pspVideoEnd();

  /* Wait if needed */
  if (Config.FrameSync)
  {
    do { sceRtcGetCurrentTick(&CurrentTick); }
    while (CurrentTick - LastTick < TicksPerUpdate);
    LastTick = CurrentTick;
  }

  /* Wait for VSync signal */
  if (Config.VSync) pspVideoWaitVSync();

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
  Pokey_sound_init(FREQ_17_EXACT, SOUND_FREQ, 1, SND_BIT16);
}

static void AudioCallback(pl_snd_sample* buf, unsigned int samples, void *userdata)
{
  unsigned int nsamples = 1024;
  static short SoundBuffer[1024];

  Pokey_process(SoundBuffer, nsamples);

  int i;
  for (i = 0; i < nsamples; i++) 
    buf[i].stereo.l = buf[i].stereo.r = SoundBuffer[i];
}

void Sound_Update(void)
{
  /* Actual work is done in AudioCallback */
}

void Sound_Pause(void)
{
  pl_snd_pause(0);
}

void Sound_Continue(void)
{
  pl_snd_resume(0);
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

  /* Reconfigure visible dimensions, if necessary */
  if (CropScreen)
  {
    Screen->Viewport.X += 8;
    Screen->Viewport.Width -= 16;
  }

  /* Clear screen */
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
    ScreenW = 336.0f * ratio - 2;
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

  /* Recompute update frequency */
  TicksPerSecond = sceRtcGetTickResolution();
  if (Config.FrameSync)
  {
    TicksPerUpdate = TicksPerSecond
      / ((tv_mode == TV_NTSC) ? 60 : 50 / (Config.Frameskip + 1));
    sceRtcGetCurrentTick(&LastTick);
  }
  Frame = 0;

#ifdef SOUND
  /* Resume sound */
	Sound_Continue();
#endif

  /* Start emulation - main loop*/
  while (!ExitPSP)
  {
    /* Check input */
    if (ParseInput()) break;

    /* Process current frame */
    Atari800_Frame();

    /* Run the system emulation for a frame */
    if (++Frame > Config.Frameskip)
    {
      Atari_DisplayScreen();
      Frame = 0;
    }
  }

#ifdef SOUND
  /* Stop sound */
  Sound_Pause();
#endif
}

inline void HandleKeyInput(unsigned int code, int on)
{
  key_code = AKEY_NONE;
  key_consol = CONSOL_NONE;

  if (code & KBD) /* Keyboard */
  { 
    if (on) key_code = CODE_MASK(code); 
    else key_code = AKEY_NONE;
  }
  else if (code & CSL) /* Console */
  { 
    if (on) key_consol ^= CODE_MASK(code); 
    else key_consol = CONSOL_NONE;
  }
  else if (code & STA) /* State-based (shift/ctrl) */
  {
    switch (CODE_MASK(code))
    {
    case AKEY_SHFT: key_shift = on; break;
    case AKEY_CTRL: key_ctrl  = on; break;
    }
  }
  else if (code & SPC) /* Emulator-specific */
  {
    if (on) key_code = -(int)CODE_MASK(code);
    else key_code = AKEY_NONE;
  }
}

int ParseInput()
{
  /* Clear keyboard and joystick state */
  JoyState[0] = 0xff;
  TrigState[0] = 1;

  if (!ShowKybd)
  {
	  key_code = AKEY_NONE;
	  key_consol = CONSOL_NONE;
	  key_shift = key_ctrl = 0;
	}

  /* Get PSP input */
  static SceCtrlData pad;
  if (!pspCtrlPollControls(&pad))
    return 0;

#ifdef PSP_DEBUG
  if ((pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_START))
    == (PSP_CTRL_SELECT | PSP_CTRL_START))
      pspUtilSaveVramSeq(ScreenshotPath, "game");
#endif

  int i, on, code;
  PspKeyboardLayout *layout = (machine_type == MACHINE_5200) 
    ? KeypadLayout : KeyboardLayout;

  /* Navigate the virtual keyboard, if shown */
  if (ShowKybd) pspKybdNavigate(layout, &pad);

  /* Parse input */
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
        switch (CODE_MASK(code))
        {
        case AKEY_SHFT: key_shift = on; break;
        case AKEY_CTRL: key_ctrl  = on; break;
        }
	    }
	    else if (code & SPC) /* Emulator-specific */
	    {
	      if (on && key_code == AKEY_NONE) 
	        key_code = -(int)CODE_MASK(code);
	    }
	  }

    if (code & MET)
    {
      switch (CODE_MASK(code))
      {
      case META_SHOW_MENU:
        if (on) return 1;
        break;
      case META_SHOW_KEYS:
        if (ShowKybd != on && layout)
        {
          if (on) pspKybdReinit(layout);
          else { pspKybdReleaseAll(layout); ClearScreen = 1; }
        }
        ShowKybd = on;
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

