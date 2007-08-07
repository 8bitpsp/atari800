#ifndef _EMULATE_H
#define _EMULATE_H

#define DISPLAY_MODE_UNSCALED    0
#define DISPLAY_MODE_FIT_HEIGHT  1
#define DISPLAY_MODE_FILL_SCREEN 2

int InitEmulation(int *argc, char **argv);
void RunEmulation();
void TrashEmulation();

#define MAP_BUTTONS            18

#define MAP_ANALOG_UP          0
#define MAP_ANALOG_DOWN        1
#define MAP_ANALOG_LEFT        2 
#define MAP_ANALOG_RIGHT       3
#define MAP_BUTTON_UP          4
#define MAP_BUTTON_DOWN        5
#define MAP_BUTTON_LEFT        6
#define MAP_BUTTON_RIGHT       7
#define MAP_BUTTON_SQUARE      8
#define MAP_BUTTON_CROSS       9
#define MAP_BUTTON_CIRCLE      10
#define MAP_BUTTON_TRIANGLE    11
#define MAP_BUTTON_LTRIGGER    12
#define MAP_BUTTON_RTRIGGER    13
#define MAP_BUTTON_SELECT      14
#define MAP_BUTTON_START       15
#define MAP_BUTTON_LRTRIGGERS  16
#define MAP_BUTTON_STARTSELECT 17

#define JOY 0x10000
#define SYS 0x20000
#define SPC 0x40000

#define SPC_MENU 1

typedef struct
{
  int ShowFps;
  int ControlMode;
  int ClockFreq;
  int DisplayMode;
  int VSync;
  int UpdateFreq;
  int Frameskip;
} EmulatorConfig;

typedef struct
{
  unsigned int ButtonConfig[MAP_BUTTONS];
} GameConfig;

#endif

