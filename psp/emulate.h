#ifndef _EMULATE_H
#define _EMULATE_H

#define DISPLAY_MODE_UNSCALED    0
#define DISPLAY_MODE_FIT_HEIGHT  1
#define DISPLAY_MODE_FILL_SCREEN 2

typedef struct
{
  char  ShowFps;
  char  ControlMode;
  short ClockFreq;
  char  DisplayMode;
  char  VSync;
  char  UpdateFreq;
  char  Frameskip;
} EmulatorOptions;

int InitEmulation(int *argc, char **argv);
void RunEmulation();
void TrashEmulation();

#endif
