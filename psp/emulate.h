#ifndef _EMULATE_H
#define _EMULATE_H

#define DISPLAY_MODE_UNSCALED    0
#define DISPLAY_MODE_FIT_HEIGHT  1
#define DISPLAY_MODE_FILL_SCREEN 2

int InitEmulation();
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

#define CODE_MASK(x) (x & 0xff)

#define JOY 0x0100 /* Joystick */
#define TRG 0x0200 /* Trigger */
#define KBD 0x0400 /* Keyboard */
#define CSL 0x0800 /* Console */
#define STA 0x1000 /* State-based (shift/ctrl) */
#define SPC 0x2000 /* Emulator-reserved */
#define MET 0x4000 /* Atari800 PSP reserved */

#define META_SHOW_MENU 1
#define META_SHOW_KEYS 2

typedef struct
{
  int ShowFps;
  int ControlMode;
  int ClockFreq;
  int DisplayMode;
  int VSync;
  int Frameskip;
  int FrameSync;
  int ToggleVK;
} EmulatorConfig;

typedef struct
{
  unsigned int ButtonConfig[MAP_BUTTONS];
} GameConfig;

#endif

