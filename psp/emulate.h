#ifndef _EMULATE_H
#define _EMULATE_H

#define DISPLAY_MODE_UNSCALED    0
#define DISPLAY_MODE_FIT_HEIGHT  1
#define DISPLAY_MODE_FILL_SCREEN 2

typedef struct
{
  int ShowFps;
  int ControlMode;
  int ClockFreq;
  int DisplayMode;
  int VSync;
  int UpdateFreq;
  int Frameskip;
} EmulatorOptions;

int InitEmulation(int *argc, char **argv);
void RunEmulation();
void TrashEmulation();

#endif
