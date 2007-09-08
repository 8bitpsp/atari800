#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>

#include "menu.h"
#include "emulate.h"

#include "pokeysnd.h"
#include "sio.h"
#include "cartridge.h"
#include "statesav.h"
#include "atari.h"
#include "input.h"

#include "video.h"
#include "image.h"
#include "ui.h"
#include "ctrl.h"
#include "psp.h"
#include "util.h"
#include "init.h"
#include "fileio.h"

#define TAB_QUICKLOAD 0
#define TAB_STATE     1
#define TAB_CONTROL   2
#define TAB_OPTION    3
#define TAB_SYSTEM    4
#define TAB_ABOUT     5
#define TAB_MAX       TAB_SYSTEM

#define OPTION_DISPLAY_MODE 1
#define OPTION_FRAMESKIP    2
#define OPTION_VSYNC        3
#define OPTION_CLOCK_FREQ   4
#define OPTION_SHOW_FPS     5
#define OPTION_CONTROL_MODE 6
#define OPTION_FRAME_SYNC   7
#define OPTION_ANIMATE      8

#define SYSTEM_SCRNSHOT     1
#define SYSTEM_RESET        2
#define SYSTEM_MACHINE_TYPE 3
#define SYSTEM_TV_MODE      4
#define SYSTEM_CROP_SCREEN  5
#define SYSTEM_STEREO       6
#define SYSTEM_EJECT        7

#define MACHINE_TYPE(machine, ram) ((ram) << 8 | ((machine) & 0xff))
#define MACHINE(mtype)       ((mtype) & 0xff)
#define RAM(mtype)           ((mtype) >> 8)

int CropScreen;
EmulatorConfig Config;
GameConfig ActiveGameConfig;

/* Default configurations */
GameConfig DefaultComputerConfig =
{
  {
    JOY|STICK_FORWARD,   /* Analog Up    */
    JOY|STICK_BACK,      /* Analog Down  */
    JOY|STICK_LEFT,      /* Analog Left  */
    JOY|STICK_RIGHT,     /* Analog Right */
    KBD|AKEY_UP,         /* D-pad Up     */
    KBD|AKEY_DOWN,       /* D-pad Down   */
    KBD|AKEY_LEFT,       /* D-pad Left   */
    KBD|AKEY_RIGHT,      /* D-pad Right  */
    0,                   /* Square       */
    TRG|0,               /* Cross        */
    KBD|AKEY_SPACE,      /* Circle       */
    0,                   /* Triangle     */
    0,                   /* L Trigger    */
    MET|META_SHOW_KEYS,  /* R Trigger    */
    CSL|CONSOL_SELECT,   /* Select       */
    CSL|CONSOL_START,    /* Start        */
    MET|META_SHOW_MENU,  /* L+R Triggers */
    0,                   /* Start+Select */
  }
},
DefaultConsoleConfig = 
{
  {
    JOY|STICK_FORWARD,    /* Analog Up    */
    JOY|STICK_BACK,       /* Analog Down  */
    JOY|STICK_LEFT,       /* Analog Left  */
    JOY|STICK_RIGHT,      /* Analog Right */
    JOY|STICK_FORWARD,    /* D-pad Up     */
    JOY|STICK_BACK,       /* D-pad Down   */
    JOY|STICK_LEFT,       /* D-pad Left   */
    JOY|STICK_RIGHT,      /* D-pad Right  */
    0,                    /* Square       */
    TRG|0,                /* Cross        */
    0,                    /* Circle       */
    0,                    /* Triangle     */
    0,                    /* L Trigger    */
    MET|META_SHOW_KEYS,   /* R Trigger    */
    KBD|AKEY_5200_PAUSE,  /* Select       */
    KBD|AKEY_5200_START,  /* Start        */
    MET|META_SHOW_MENU,    /* L+R Triggers */
    0,                    /* Start+Select */
  }
};

/* Button masks */
const u64 ButtonMask[] = 
{
  PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER, 
  PSP_CTRL_START    | PSP_CTRL_SELECT,
  PSP_CTRL_ANALUP,    PSP_CTRL_ANALDOWN,
  PSP_CTRL_ANALLEFT,  PSP_CTRL_ANALRIGHT,
  PSP_CTRL_UP,        PSP_CTRL_DOWN,
  PSP_CTRL_LEFT,      PSP_CTRL_RIGHT,
  PSP_CTRL_SQUARE,    PSP_CTRL_CROSS,
  PSP_CTRL_CIRCLE,    PSP_CTRL_TRIANGLE,
  PSP_CTRL_LTRIGGER,  PSP_CTRL_RTRIGGER,
  PSP_CTRL_SELECT,    PSP_CTRL_START,
  0 /* End */
};

/* Button map ID's */
const int ButtonMapId[] = 
{
  MAP_BUTTON_LRTRIGGERS, 
  MAP_BUTTON_STARTSELECT,
  MAP_ANALOG_UP,       MAP_ANALOG_DOWN,
  MAP_ANALOG_LEFT,     MAP_ANALOG_RIGHT,
  MAP_BUTTON_UP,       MAP_BUTTON_DOWN,
  MAP_BUTTON_LEFT,     MAP_BUTTON_RIGHT,
  MAP_BUTTON_SQUARE,   MAP_BUTTON_CROSS,
  MAP_BUTTON_CIRCLE,   MAP_BUTTON_TRIANGLE,
  MAP_BUTTON_LTRIGGER, MAP_BUTTON_RTRIGGER,
  MAP_BUTTON_SELECT,   MAP_BUTTON_START,
  -1 /* End */
};

extern PspImage *Screen;

static int TabIndex;
static int ResumeEmulation;
static PspImage *Background;
static PspImage *NoSaveIcon;

static char *LoadedGame;
static char *GamePath;
static char *SaveStatePath;
static char *ConfigPath;
char *ScreenshotPath;

static const char 
  *NoCartName = "BASIC",
  *OptionsFile = "options.ini",
  *ScreenshotDir = "screens",
  *ConfigDir = "config",
  *SaveStateDir = "savedata",
  *DefaultComputerConfigFile = "comp_def",
  *DefaultConsoleConfigFile = "cons_def",
  *QuickloadFilter[] = 
    { "XEX", "EXE", "COM", "BIN", /* Executables */
      "ATR", "XFD", "ATR.GZ", "ATZ", "XFD.GZ", "XFZ", "DCM", /* Disk images */
      "BAS", "LST", /* Listings */
      "CAR", "CART", "ROM", "A52", /* Cartridges */
      "CAS", /* Cassette tapes */
      '\0' },
  PresentSlotText[] = "\026\244\020 Save\t\026\001\020 Load\t\026\243\020 Delete",
  EmptySlotText[] = "\026\244\020 Save",
  ControlHelpText[] = "\026\250\020 Change mapping\t\026\001\020 Save to \271\t"
    "\026\244\020 Set as default\t\026\243\020 Load defaults";

/* Tab labels */
static const char *TabLabel[] = 
{
  "Game",
  "Save/Load",
  "Controls",
  "Options",
  "System",
  "About"
};

/* Option definitions */
static const PspMenuOptionDef
  ToggleOptions[] = {
    MENU_OPTION("Disabled", 0),
    MENU_OPTION("Enabled",  1),
    MENU_END_OPTIONS
  },
  ScreenSizeOptions[] = {
    MENU_OPTION("Actual size",              DISPLAY_MODE_UNSCALED),
    MENU_OPTION("4:3 scaled (fit height)",  DISPLAY_MODE_FIT_HEIGHT),
    MENU_OPTION("16:9 scaled (fit screen)", DISPLAY_MODE_FILL_SCREEN),
    MENU_END_OPTIONS
  },
  FrameSkipOptions[] = {
    MENU_OPTION("No skipping",   0),
    MENU_OPTION("Skip 1 frame",  1),
    MENU_OPTION("Skip 2 frames", 2),
    MENU_OPTION("Skip 3 frames", 3),
    MENU_OPTION("Skip 4 frames", 4),
    MENU_OPTION("Skip 5 frames", 5),
    MENU_END_OPTIONS
  },
  PspClockFreqOptions[] = {
    MENU_OPTION("222 MHz", 222),
    MENU_OPTION("266 MHz", 266),
    MENU_OPTION("300 MHz", 300),
    MENU_OPTION("333 MHz", 333),
    MENU_END_OPTIONS
  },
  ControlModeOptions[] = {
    MENU_OPTION("\026\242\020 cancels, \026\241\020 confirms (US)",    0),
    MENU_OPTION("\026\241\020 cancels, \026\242\020 confirms (Japan)", 1),
    MENU_END_OPTIONS
  },
  MachineTypeOptions[] = {
    MENU_OPTION("800",                 MACHINE_TYPE(MACHINE_OSB,  48)),
    MENU_OPTION("800 XL",              MACHINE_TYPE(MACHINE_XLXE, 64)),
    MENU_OPTION("130 XE",              MACHINE_TYPE(MACHINE_XLXE, 128)),
    MENU_OPTION("320 XE (Compy Shop)", MACHINE_TYPE(MACHINE_XLXE, RAM_320_COMPY_SHOP)),
    MENU_OPTION("320 XE (Rambo)",      MACHINE_TYPE(MACHINE_XLXE, RAM_320_RAMBO)),
    MENU_OPTION("5200",                MACHINE_TYPE(MACHINE_5200, 16)),
    MENU_END_OPTIONS
  },
  TVModeOptions[] = {
    MENU_OPTION("NTSC", TV_NTSC),
    MENU_OPTION("PAL",  TV_PAL),
    MENU_END_OPTIONS
  },
  ComputerButtonMapOptions[] = {
    /* Unmapped */
    MENU_OPTION("None", 0),
    /* Special keys */
    MENU_OPTION("Special: Open Menu",     MET|META_SHOW_MENU),
    MENU_OPTION("Special: Show Keyboard", MET|META_SHOW_KEYS),
    /* Console */
    MENU_OPTION("Console: Reset",  SPC|-AKEY_WARMSTART),
    MENU_OPTION("Console: Option", CSL|CONSOL_OPTION),
    MENU_OPTION("Console: Select", CSL|CONSOL_SELECT),
    MENU_OPTION("Console: Start",  CSL|CONSOL_START),
    MENU_OPTION("Console: Help",   KBD|AKEY_HELP),
    /* Joystick */
    MENU_OPTION("Joystick: Up",   JOY|STICK_FORWARD),
    MENU_OPTION("Joystick: Down", JOY|STICK_BACK),
    MENU_OPTION("Joystick: Left", JOY|STICK_LEFT),
    MENU_OPTION("Joystick: Right",JOY|STICK_RIGHT),
    MENU_OPTION("Joystick: Fire", TRG|0),
    /* Keyboard */
    MENU_OPTION("Keyboard: Up",   KBD|AKEY_UP), 
    MENU_OPTION("Keyboard: Down", KBD|AKEY_DOWN),
    MENU_OPTION("Keyboard: Left", KBD|AKEY_LEFT), 
    MENU_OPTION("Keyboard: Right",KBD|AKEY_RIGHT),
    /* Keyboard: Function keys */
    MENU_OPTION("F1", KBD|AKEY_F1), MENU_OPTION("F2", KBD|AKEY_F2),
    MENU_OPTION("F3", KBD|AKEY_F3), MENU_OPTION("F4", KBD|AKEY_F4),
    /* Keyboard: misc */
    MENU_OPTION("Space",       KBD|AKEY_SPACE), 
    MENU_OPTION("Return",      KBD|AKEY_RETURN),
    MENU_OPTION("Tab",         KBD|AKEY_TAB),
    MENU_OPTION("Backspace",   KBD|AKEY_BACKSPACE),
    MENU_OPTION("Escape",      KBD|AKEY_ESCAPE),
    MENU_OPTION("Toggle CAPS", KBD|AKEY_CAPSTOGGLE),
    MENU_OPTION("Break",       SPC|-AKEY_BREAK),
    MENU_OPTION("Atari Key",   KBD|AKEY_ATARI),
    MENU_OPTION("Shift",       STA|AKEY_SHFT),
    MENU_OPTION("Control",     STA|AKEY_CTRL),
    /* Keyboard: digits */
    MENU_OPTION("1", KBD|AKEY_1), MENU_OPTION("2", KBD|AKEY_2),
    MENU_OPTION("3", KBD|AKEY_3), MENU_OPTION("4", KBD|AKEY_4),
    MENU_OPTION("5", KBD|AKEY_5), MENU_OPTION("6", KBD|AKEY_6),
    MENU_OPTION("7", KBD|AKEY_7), MENU_OPTION("8", KBD|AKEY_8),
    MENU_OPTION("9", KBD|AKEY_9), MENU_OPTION("0", KBD|AKEY_0),
    /* Keyboard: letters */
    MENU_OPTION("A", KBD|AKEY_a), MENU_OPTION("B", KBD|AKEY_b),
    MENU_OPTION("C", KBD|AKEY_c), MENU_OPTION("D", KBD|AKEY_d),
    MENU_OPTION("E", KBD|AKEY_e), MENU_OPTION("F", KBD|AKEY_f),
    MENU_OPTION("G", KBD|AKEY_g), MENU_OPTION("H", KBD|AKEY_h),
    MENU_OPTION("I", KBD|AKEY_i), MENU_OPTION("J", KBD|AKEY_j),
    MENU_OPTION("K", KBD|AKEY_k), MENU_OPTION("L", KBD|AKEY_l),
    MENU_OPTION("M", KBD|AKEY_m), MENU_OPTION("N", KBD|AKEY_n),
    MENU_OPTION("O", KBD|AKEY_o), MENU_OPTION("P", KBD|AKEY_p),
    MENU_OPTION("Q", KBD|AKEY_q), MENU_OPTION("R", KBD|AKEY_r),
    MENU_OPTION("S", KBD|AKEY_s), MENU_OPTION("T", KBD|AKEY_t),
    MENU_OPTION("U", KBD|AKEY_u), MENU_OPTION("V", KBD|AKEY_v),
    MENU_OPTION("W", KBD|AKEY_w), MENU_OPTION("X", KBD|AKEY_x),
    MENU_OPTION("Y", KBD|AKEY_y), MENU_OPTION("Z", KBD|AKEY_z),
    /* Keyboard: symbols */
    MENU_OPTION("< (less than)",   KBD|AKEY_LESS),
    MENU_OPTION("> (greater than)",KBD|AKEY_GREATER),
    MENU_OPTION("= (equals)",      KBD|AKEY_EQUAL),
    MENU_OPTION("+ (plus)",        KBD|AKEY_PLUS),
    MENU_OPTION("* (asterisk)",    KBD|AKEY_ASTERISK),
    MENU_OPTION("/ (slash)",       KBD|AKEY_SLASH),
    MENU_OPTION(": (colon)",       KBD|AKEY_COLON),
    MENU_OPTION("; (semicolon)",   KBD|AKEY_SEMICOLON),
    MENU_OPTION(", (comma)",       KBD|AKEY_COMMA), 
    MENU_OPTION(". (period)",      KBD|AKEY_FULLSTOP),
    MENU_OPTION("_ (underscore)",  KBD|AKEY_UNDERSCORE),
    MENU_END_OPTIONS
  },
  ConsoleButtonMapOptions[] = {
    /* Unmapped */
    MENU_OPTION("None", 0),
    /* Special keys */
    MENU_OPTION("Special: Open Menu",     MET|META_SHOW_MENU),
    MENU_OPTION("Special: Show Keyboard", MET|META_SHOW_KEYS),
    /* Console */
    MENU_OPTION("Console: Start", KBD|AKEY_5200_START),
    MENU_OPTION("Console: Pause", KBD|AKEY_5200_PAUSE),
    MENU_OPTION("Console: Reset", KBD|AKEY_5200_RESET),
    /* Joystick */
    MENU_OPTION("Joystick: Up",   JOY|STICK_FORWARD),
    MENU_OPTION("Joystick: Down", JOY|STICK_BACK),
    MENU_OPTION("Joystick: Left", JOY|STICK_LEFT),
    MENU_OPTION("Joystick: Right",JOY|STICK_RIGHT),
    MENU_OPTION("Joystick: Fire", TRG|0),
    /* Keypad */
    MENU_OPTION("1", KBD|AKEY_5200_1), MENU_OPTION("2", KBD|AKEY_5200_2),
    MENU_OPTION("3", KBD|AKEY_5200_3), MENU_OPTION("4", KBD|AKEY_5200_4),
    MENU_OPTION("5", KBD|AKEY_5200_5), MENU_OPTION("6", KBD|AKEY_5200_6),
    MENU_OPTION("7", KBD|AKEY_5200_7), MENU_OPTION("8", KBD|AKEY_5200_8),
    MENU_OPTION("9", KBD|AKEY_5200_9), MENU_OPTION("0", KBD|AKEY_5200_0),
    MENU_OPTION("*", KBD|AKEY_5200_ASTERISK),
    MENU_OPTION("#", KBD|AKEY_5200_HASH),
    MENU_END_OPTIONS
  };

/* Menu definitions */
static const PspMenuItemDef
  OptionMenuDef[] = {
    MENU_HEADER("Video"),
    MENU_ITEM("Screen size", OPTION_DISPLAY_MODE, ScreenSizeOptions, -1, 
      "\026\250\020 Change screen size"),
    MENU_HEADER("Performance"),
    MENU_ITEM("Frame limiter", OPTION_FRAME_SYNC, ToggleOptions, -1, 
      "\026\250\020 Enable to run the system at proper speed; disable to run as fast as possible"),
    MENU_ITEM("Frame skipping", OPTION_FRAMESKIP, FrameSkipOptions, -1, 
      "\026\250\020 Change number of frames skipped per update"),
    MENU_ITEM("VSync", OPTION_VSYNC, ToggleOptions, -1, 
      "\026\250\020 Enable to reduce tearing; disable to increase speed"),
    MENU_ITEM("PSP clock frequency", OPTION_CLOCK_FREQ, PspClockFreqOptions, -1, 
      "\026\250\020 Larger values: faster emulation, faster battery depletion (default: 222MHz)"),
    MENU_ITEM("Show FPS counter", OPTION_SHOW_FPS, ToggleOptions, -1,
      "\026\250\020 Show/hide the frames-per-second counter"),
    MENU_HEADER("Menu"),
    MENU_ITEM("Button mode", OPTION_CONTROL_MODE, ControlModeOptions,  -1,
      "\026\250\020 Change OK and Cancel button mapping"),
    MENU_ITEM("Animations", OPTION_ANIMATE, ToggleOptions,  -1,
      "\026\250\020 Change Enable/disable in-menu animations"),
    MENU_END_ITEMS
  },
  ComputerControlMenuDef[] = {
    MENU_ITEM(PSP_CHAR_ANALUP, MAP_ANALOG_UP, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALRIGHT, MAP_ANALOG_RIGHT,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_UP, MAP_BUTTON_UP, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_DOWN, MAP_BUTTON_DOWN, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LEFT, MAP_BUTTON_LEFT, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_RIGHT, MAP_BUTTON_RIGHT,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_SQUARE, MAP_BUTTON_SQUARE, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_CROSS, MAP_BUTTON_CROSS,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_CIRCLE, MAP_BUTTON_CIRCLE, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_SELECT, MAP_BUTTON_SELECT, 
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_START, MAP_BUTTON_START,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, 
      MAP_BUTTON_LRTRIGGERS, ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_START"+"PSP_CHAR_SELECT, MAP_BUTTON_STARTSELECT,
      ComputerButtonMapOptions, -1, ControlHelpText),
    MENU_END_ITEMS
  },
  ConsoleControlMenuDef[] = {
    MENU_ITEM(PSP_CHAR_ANALUP, MAP_ANALOG_UP,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_ANALRIGHT, MAP_ANALOG_RIGHT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_UP, MAP_BUTTON_UP,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_DOWN, MAP_BUTTON_DOWN,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LEFT, MAP_BUTTON_LEFT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_RIGHT, MAP_BUTTON_RIGHT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_SQUARE, MAP_BUTTON_SQUARE,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_CROSS, MAP_BUTTON_CROSS,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_CIRCLE, MAP_BUTTON_CIRCLE,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_SELECT, MAP_BUTTON_SELECT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_START, MAP_BUTTON_START,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, 
      MAP_BUTTON_LRTRIGGERS, ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_ITEM(PSP_CHAR_START"+"PSP_CHAR_SELECT, MAP_BUTTON_STARTSELECT,
      ConsoleButtonMapOptions, -1, ControlHelpText),
    MENU_END_ITEMS
  },
  SystemMenuDef[] = {
    MENU_HEADER("Storage"),
    MENU_ITEM("Eject all", SYSTEM_EJECT, NULL, -1, 
      "\026\001\020 Eject all disks/cartridges"),
    MENU_HEADER("Audio"),
    MENU_ITEM("Stereo sound", SYSTEM_STEREO, ToggleOptions, -1, 
      "\026\250\020 Toggle stereo sound (dual POKEY emulation)"),
    MENU_HEADER("Video"),
    MENU_ITEM("Horizontal crop", SYSTEM_CROP_SCREEN, ToggleOptions, -1, 
      "\026\250\020 Remove 8-pixel wide strips on each side of the screen"),
    MENU_ITEM("TV frequency", SYSTEM_TV_MODE, TVModeOptions, -1, 
      "\026\250\020 Change video frequency of emulated machine"),
    MENU_HEADER("Hardware"),
    MENU_ITEM("Machine type", SYSTEM_MACHINE_TYPE, MachineTypeOptions, -1, 
      "\026\250\020 Change emulated machine"),
    MENU_HEADER("System"),
    MENU_ITEM("Reset", SYSTEM_RESET, NULL, -1, "\026\001\020 Reset"),
    MENU_ITEM("Save screenshot",  SYSTEM_SCRNSHOT, NULL, -1,
      "\026\001\020 Save screenshot"),
    MENU_END_ITEMS
  };

  /* Cartridge types (copied from /ui.c) */
  static const char* CartType[] = 
  {
	  "Standard 8 KB cartridge",
	  "Standard 16 KB cartridge",
	  "OSS '034M' 16 KB cartridge",
	  "Standard 32 KB 5200 cartridge",
	  "DB 32 KB cartridge",
	  "Two chip 16 KB 5200 cartridge",
	  "Bounty Bob 40 KB 5200 cartridge",
	  "64 KB Williams cartridge",
	  "Express 64 KB cartridge",
	  "Diamond 64 KB cartridge",
	  "SpartaDOS X 64 KB cartridge",
	  "XEGS 32 KB cartridge",
	  "XEGS 64 KB cartridge",
	  "XEGS 128 KB cartridge",
	  "OSS 'M091' 16 KB cartridge",
	  "One chip 16 KB 5200 cartridge",
	  "Atrax 128 KB cartridge",
	  "Bounty Bob 40 KB cartridge",
	  "Standard 8 KB 5200 cartridge",
	  "Standard 4 KB 5200 cartridge",
	  "Right slot 8 KB cartridge",
	  "32 KB Williams cartridge",
	  "XEGS 256 KB cartridge",
	  "XEGS 512 KB cartridge",
	  "XEGS 1 MB cartridge",
	  "MegaCart 16 KB cartridge",
	  "MegaCart 32 KB cartridge",
	  "MegaCart 64 KB cartridge",
	  "MegaCart 128 KB cartridge",
	  "MegaCart 256 KB cartridge",
	  "MegaCart 512 KB cartridge",
	  "MegaCart 1 MB cartridge",
	  "Switchable XEGS 32 KB cartridge",
	  "Switchable XEGS 64 KB cartridge",
	  "Switchable XEGS 128 KB cartridge",
	  "Switchable XEGS 256 KB cartridge",
	  "Switchable XEGS 512 KB cartridge",
	  "Switchable XEGS 1 MB cartridge",
	  "Phoenix 8 KB cartridge",
	  "Blizzard 16 KB cartridge",
	  "Atarimax 128 KB Flash cartridge",
	  "Atarimax 1 MB Flash cartridge",
	  "SpartaDOS X 128 KB cartridge",
	  NULL
  };

/* Function declarations */
static void InitGameConfig(GameConfig *config);
static int  LoadGameConfig(const char *config_name, GameConfig *config);
static int  SaveGameConfig(const char *config_name, const GameConfig *config);

static void LoadOptions();
static int  SaveOptions();

static void DisplayControlTab();
static void DisplayStateTab();

static PspImage* LoadStateIcon(const char *path);
static int LoadState(const char *path);
static PspImage* SaveState(const char *path, PspImage *icon);

/* Interface callbacks */
static int  OnGenericCancel(const void *uiobject, const void *param);
static void OnGenericRender(const void *uiobject, const void *item_obj);
static int  OnGenericButtonPress(const PspUiFileBrowser *browser, 
  const char *path, u32 button_mask);

static int OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask);
static void OnSplashRender(const void *uiobject, const void *null);

static int OnMenuItemChanged(const struct PspUiMenu *uimenu, PspMenuItem* item, 
  const PspMenuOption* option);
static int OnMenuButtonPress(const struct PspUiMenu *uimenu, 
  PspMenuItem* sel_item, u32 button_mask);
static int OnMenuOk(const void *uimenu, const void* sel_item);

static int OnSaveStateOk(const void *gallery, const void *item);
static int OnSaveStateButtonPress(const PspUiGallery *gallery, 
  PspMenuItem* item, u32 button_mask);

static void OnSystemRender(const void *uiobject, const void *item_obj);

static int OnQuickloadOk(const void *browser, const void *path);

/* UI object declarations */
PspUiSplash SplashScreen =
{
  OnSplashRender,
  OnGenericCancel,
  OnSplashButtonPress,
  NULL
};

PspUiFileBrowser QuickloadBrowser = 
{
  OnGenericRender,
  OnQuickloadOk,
  OnGenericCancel,
  OnGenericButtonPress,
  QuickloadFilter,
  0
};

PspUiMenu OptionUiMenu =
{
  NULL,                  /* PspMenu */
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiGallery SaveStateGallery = 
{
  NULL,                        /* PspMenu */
  OnGenericRender,             /* OnRender() */
  OnSaveStateOk,               /* OnOk() */
  OnGenericCancel,             /* OnCancel() */
  OnSaveStateButtonPress,      /* OnButtonPress() */
  NULL                         /* Userdata */
};

PspUiMenu SystemUiMenu =
{
  NULL,                  /* PspMenu */
  OnSystemRender,        /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiMenu ControlUiMenu =
{
  NULL,                  /* PspMenu */
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

void InitMenu()
{
  /* Initialize UI components */
  UiMetric.Font = &PspStockFont;
  UiMetric.Left = 8;
  UiMetric.Top = 24;
  UiMetric.Right = 472;
  UiMetric.Bottom = 250;
  UiMetric.OkButton = (!Config.ControlMode) 
    ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
  UiMetric.CancelButton = (!Config.ControlMode) 
    ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
  UiMetric.ScrollbarColor = PSP_COLOR_GRAY;
  UiMetric.ScrollbarBgColor = 0x44ffffff;
  UiMetric.ScrollbarWidth = 10;
  UiMetric.TextColor = PSP_COLOR_GRAY;
  UiMetric.SelectedColor = PSP_COLOR_YELLOW;
  UiMetric.SelectedBgColor = COLOR(0xff,0xff,0xff,0x44);
  UiMetric.StatusBarColor = PSP_COLOR_WHITE;
  UiMetric.BrowserFileColor = PSP_COLOR_GRAY;
  UiMetric.BrowserDirectoryColor = PSP_COLOR_YELLOW;
  UiMetric.GalleryIconsPerRow = 5;
  UiMetric.GalleryIconMarginWidth = 8;
  UiMetric.MenuItemMargin = 20;
  UiMetric.MenuSelOptionBg = PSP_COLOR_BLACK;
  UiMetric.MenuOptionBoxColor = PSP_COLOR_GRAY;
  UiMetric.MenuOptionBoxBg = COLOR(0, 0, 33, 0xBB);
  UiMetric.MenuDecorColor = PSP_COLOR_YELLOW;
  UiMetric.TitlePadding = 4;
  UiMetric.TitleColor = PSP_COLOR_WHITE;
  UiMetric.MenuFps = 30;
  UiMetric.TabBgColor = COLOR(0xcc,0xdb,0xe3, 0xff);
  UiMetric.Animate = 1;

  /* Reset variables */
  TabIndex = TAB_ABOUT;
  Background = NULL;
  NoSaveIcon = NULL;
  LoadedGame = NULL;
  GamePath = NULL;

  /* Initialize emulation */
  if (!InitEmulation()) return;

  /* Initialize paths */
  SaveStatePath = (char*)malloc(sizeof(char) 
    * (strlen(pspGetAppDirectory()) + strlen(SaveStateDir) + 2));
  sprintf(SaveStatePath, "%s%s/", pspGetAppDirectory(), SaveStateDir);
  ScreenshotPath = (char*)malloc(sizeof(char) 
    * (strlen(pspGetAppDirectory()) + strlen(ScreenshotDir) + 2));
  sprintf(ScreenshotPath, "%s%s/", pspGetAppDirectory(), ScreenshotDir);
  ConfigPath = (char*)malloc(sizeof(char) 
    * (strlen(pspGetAppDirectory()) + strlen(ConfigDir) + 2));
  sprintf(ConfigPath, "%s%s/", pspGetAppDirectory(), ConfigDir);

  /* Initialize options */
  LoadOptions();
  Atari800_InitialiseMachine();

  /* Load the background image */
  Background = pspImageLoadPng("background.png");
  UiMetric.Background = Background;

  /* Init NoSaveState icon image */
  NoSaveIcon=pspImageCreate(168, 120, PSP_IMAGE_16BPP);
  pspImageClear(NoSaveIcon, RGB(0x29,0x29,0x29));

  /* Initialize options menu */
  OptionUiMenu.Menu = pspMenuCreate();
  pspMenuLoad(OptionUiMenu.Menu, OptionMenuDef);

  /* Initialize state menu */
  SaveStateGallery.Menu = pspMenuCreate();
  int i;
  PspMenuItem *item;
  for (i = 0; i < 10; i++)
  {
    item = pspMenuAppendItem(SaveStateGallery.Menu, NULL, i);
    pspMenuSetHelpText(item, EmptySlotText);
  }

  /* Initialize system menu */
  SystemUiMenu.Menu = pspMenuCreate();
  pspMenuLoad(SystemUiMenu.Menu, SystemMenuDef);

  /* Initialize control menu */
  ControlUiMenu.Menu = pspMenuCreate();
  pspMenuLoad(ControlUiMenu.Menu, ComputerControlMenuDef);

  /* Load default configuration */
  LoadGameConfig(DefaultConsoleConfigFile, &DefaultConsoleConfig);
  LoadGameConfig(DefaultComputerConfigFile, &DefaultComputerConfig);
  LoadGameConfig(LoadedGame, &ActiveGameConfig);
}

void DisplayMenu()
{
  PspMenuItem *item;

  /* Menu loop */
  do
  {
    ResumeEmulation = 0;

    /* Set normal clock frequency */
    pspSetClockFrequency(222);
    /* Set buttons to autorepeat */
    pspCtrlSetPollingMode(PSP_CTRL_AUTOREPEAT);

    /* Display appropriate tab */
    switch (TabIndex)
    {
    case TAB_QUICKLOAD:
      pspUiOpenBrowser(&QuickloadBrowser, (LoadedGame) ? LoadedGame : GamePath);
      break;

    case TAB_STATE:
      DisplayStateTab();
      break;

    case TAB_CONTROL:
      DisplayControlTab();
      break;

    case TAB_OPTION:
      /* Init menu options */
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_DISPLAY_MODE);
      pspMenuSelectOptionByValue(item, (void*)Config.DisplayMode);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_FRAMESKIP);
      pspMenuSelectOptionByValue(item, (void*)(int)Config.Frameskip);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_VSYNC);
      pspMenuSelectOptionByValue(item, (void*)Config.VSync);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_CLOCK_FREQ);
      pspMenuSelectOptionByValue(item, (void*)Config.ClockFreq);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_SHOW_FPS);
      pspMenuSelectOptionByValue(item, (void*)Config.ShowFps);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_CONTROL_MODE);
      pspMenuSelectOptionByValue(item, (void*)Config.ControlMode);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_ANIMATE);
      pspMenuSelectOptionByValue(item, (void*)UiMetric.Animate);
      item = pspMenuFindItemById(OptionUiMenu.Menu, OPTION_FRAME_SYNC);
      pspMenuSelectOptionByValue(item, (void*)Config.FrameSync);
      pspUiOpenMenu(&OptionUiMenu, NULL);
      break;

    case TAB_SYSTEM:
      item = pspMenuFindItemById(SystemUiMenu.Menu, SYSTEM_STEREO);
      pspMenuSelectOptionByValue(item, (void*)stereo_enabled);
      item = pspMenuFindItemById(SystemUiMenu.Menu, SYSTEM_MACHINE_TYPE);
      pspMenuSelectOptionByValue(item, (void*)(MACHINE_TYPE(machine_type, ram_size)));
      item = pspMenuFindItemById(SystemUiMenu.Menu, SYSTEM_CROP_SCREEN);
      pspMenuSelectOptionByValue(item, (void*)CropScreen);
      item = pspMenuFindItemById(SystemUiMenu.Menu, SYSTEM_TV_MODE);
      pspMenuSelectOptionByValue(item, (void*)tv_mode);
      pspUiOpenMenu(&SystemUiMenu, NULL);
      break;

    case TAB_ABOUT:
      pspUiSplashScreen(&SplashScreen);
      break;
    }

    if (!ExitPSP)
    {
      /* Set clock frequency during emulation */
      pspSetClockFrequency(Config.ClockFreq);
      /* Set buttons to normal mode */
      pspCtrlSetPollingMode(PSP_CTRL_NORMAL);

      /* Resume emulation */
      if (ResumeEmulation)
      {
        if (UiMetric.Animate) pspUiFadeout();
        RunEmulation();
        if (UiMetric.Animate) pspUiFadeout();
      }
    }
  } while (!ExitPSP);
}

static void DisplayControlTab()
{
  int i;
  PspMenuItem *item;
  const char *config_name;
  config_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
  char *game_name = strdup(config_name);
  char *dot = strrchr(game_name, '.');
  if (dot) *dot='\0';

  /* Reinitialize the control menu to reflect console or computer */
  pspMenuLoad(ControlUiMenu.Menu, 
    (machine_type == MACHINE_5200) 
      ? ConsoleControlMenuDef : ComputerControlMenuDef);

  /* Load current button mappings */
  for (item = ControlUiMenu.Menu->First, i = 0; item; item = item->Next, i++)
    pspMenuSelectOptionByValue(item, (void*)ActiveGameConfig.ButtonConfig[i]);

  pspUiOpenMenu(&ControlUiMenu, game_name);
  free(game_name);
}

static void DisplayStateTab()
{
  PspMenuItem *item;
  SceIoStat stat;
  char caption[128];

  const char *config_name;
  config_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
  char *path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  char *game_name = strdup(config_name);
  char *dot = strrchr(game_name, '.');
  if (dot) *dot='\0';

  /* Initialize icons */
  for (item = SaveStateGallery.Menu->First; item; item = item->Next)
  {
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, item->ID);

    if (pspFileIoCheckIfExists(path))
    {
      if (sceIoGetstat(path, &stat) < 0)
        sprintf(caption, "ERROR");
      else
        sprintf(caption, "%02i/%02i/%02i %02i:%02i",
          stat.st_mtime.month, stat.st_mtime.day,
          stat.st_mtime.year - (stat.st_mtime.year / 100) * 100,
          stat.st_mtime.hour, stat.st_mtime.minute);

      pspMenuSetCaption(item, caption);
      item->Icon = LoadStateIcon(path);
      pspMenuSetHelpText(item, PresentSlotText);
    }
    else
    {
      pspMenuSetCaption(item, "Empty");
      item->Icon = NoSaveIcon;
      pspMenuSetHelpText(item, EmptySlotText);
    }
  }

  free(path);
  pspUiOpenGallery(&SaveStateGallery, game_name);
  free(game_name);

  /* Destroy any icons */
  for (item = SaveStateGallery.Menu->First; item; item = item->Next)
    if (item->Icon != NULL && item->Icon != NoSaveIcon)
      pspImageDestroy((PspImage*)item->Icon);
}

/* Load options */
void LoadOptions()
{
  char *path = (char*)malloc(sizeof(char) * (strlen(ConfigPath) 
    + strlen(OptionsFile) + 1));
  sprintf(path, "%s%s", ConfigPath, OptionsFile);

  /* Initialize INI structure */
  PspInit *init = pspInitCreate();

  /* Read the file */
  pspInitLoad(init, path);

  /* Load values */
  Config.DisplayMode = pspInitGetInt(init, "Video", "Display Mode", 
    DISPLAY_MODE_UNSCALED);
  Config.Frameskip = pspInitGetInt(init, "Video", "Frameskip", 0);
  Config.VSync     = pspInitGetInt(init, "Video", "VSync", 0);
  Config.FrameSync = pspInitGetInt(init, "Video", "Frame Sync", 1);
  Config.ClockFreq = pspInitGetInt(init, "Video", "PSP Clock Frequency", 222);
  Config.ShowFps   = pspInitGetInt(init, "Video", "Show FPS", 0);
  Config.ControlMode = pspInitGetInt(init, "Menu", "Control Mode", 0);
  UiMetric.Animate = pspInitGetInt(init, "Menu",  "Animations", 1);

  CropScreen = pspInitGetInt(init, "System", "Crop screen", 0);
  machine_type = pspInitGetInt(init, "System", "Machine Type", MACHINE_XLXE);
  ram_size = pspInitGetInt(init, "System", "RAM Size", 64);
  tv_mode = pspInitGetInt(init, "System", "TV mode", TV_PAL);
  stereo_enabled = pspInitGetInt(init, "System", "Stereo", 0);

  if (GamePath) free(GamePath);
  GamePath = pspInitGetString(init, "File", "Game Path", NULL);

  /* Clean up */
  free(path);
  pspInitDestroy(init);
}

/* Save options */
static int SaveOptions()
{
  char *path = (char*)malloc(sizeof(char) * (strlen(ConfigPath) 
    + strlen(OptionsFile) + 1));
  sprintf(path, "%s%s", ConfigPath, OptionsFile);

  /* Initialize INI structure */
  PspInit *init = pspInitCreate();

  /* Set values */
  pspInitSetInt(init, "Video", "Display Mode", Config.DisplayMode);
  pspInitSetInt(init, "Video", "Frameskip", Config.Frameskip);
  pspInitSetInt(init, "Video", "VSync", Config.VSync);
  pspInitSetInt(init, "Video", "Frame Sync", Config.FrameSync);
  pspInitSetInt(init, "Video", "PSP Clock Frequency",Config.ClockFreq);
  pspInitSetInt(init, "Video", "Show FPS", Config.ShowFps);
  pspInitSetInt(init, "Menu",  "Control Mode", Config.ControlMode);
  pspInitSetInt(init, "Menu",  "Animations", UiMetric.Animate);

  pspInitSetInt(init, "System", "Crop screen", CropScreen);
  pspInitSetInt(init, "System", "Machine Type", machine_type);
  pspInitSetInt(init, "System", "RAM Size", ram_size);
  pspInitSetInt(init, "System", "TV mode", tv_mode);
  pspInitSetInt(init, "System", "Stereo", stereo_enabled);

  if (GamePath) pspInitSetString(init, "File", "Game Path", GamePath);

  /* Save INI file */
  int status = pspInitSave(init, path);

  /* Clean up */
  pspInitDestroy(init);
  free(path);

  return status;
}

int OnGenericCancel(const void *uiobject, const void* param)
{
  ResumeEmulation = 1;
  return 1;
}

void OnSplashRender(const void *splash, const void *null)
{
  int fh, i, x, y, height;
  const char *lines[] = 
  { 
    PSP_APP_NAME" version "PSP_APP_VER" ("__DATE__")",
    "\026http://psp.akop.org/atari800",
    " ",
    "2007 Akop Karapetyan (port)",
    "1997-2007 Atari800 team (emulation)",
    "1995-1997 David Firth (emulation)",
    NULL
  };

  fh = pspFontGetLineHeight(UiMetric.Font);

  for (i = 0; lines[i]; i++);
  height = fh * (i - 1);

  /* Render lines */
  for (i = 0, y = SCR_HEIGHT / 2 - height / 2; lines[i]; i++, y += fh)
  {
    x = SCR_WIDTH / 2 - pspFontGetTextWidth(UiMetric.Font, lines[i]) / 2;
    pspVideoPrint(UiMetric.Font, x, y, lines[i], PSP_COLOR_GRAY);
  }

  /* Render PSP status */
  OnGenericRender(splash, null);
}

int OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask)
{
  return OnGenericButtonPress(NULL, NULL, button_mask);
}

/* Handles drawing of generic items */
void OnGenericRender(const void *uiobject, const void *item_obj)
{
  /* Draw tabs */
  int i, x, width, height = pspFontGetLineHeight(UiMetric.Font);
  for (i = 0, x = 5; i <= TAB_MAX; i++, x += width + 10)
  {
    width = -10;

    /* Determine width of text */
    width = pspFontGetTextWidth(UiMetric.Font, TabLabel[i]);

    /* Draw background of active tab */
    if (i == TabIndex)
      pspVideoFillRect(x - 5, 0, x + width + 5, height + 1, UiMetric.TabBgColor);

    /* Draw name of tab */
    pspVideoPrint(UiMetric.Font, x, 0, TabLabel[i], PSP_COLOR_WHITE);
  }
}

int OnGenericButtonPress(const PspUiFileBrowser *browser, 
  const char *path, u32 button_mask)
{
  int tab_index;

  /* If L or R are pressed, switch tabs */
  if (button_mask & PSP_CTRL_LTRIGGER)
  {
    TabIndex--;
    do
    {
      tab_index = TabIndex;
      if (TabIndex < 0) TabIndex = TAB_MAX;
    } while (tab_index != TabIndex);
  }
  else if (button_mask & PSP_CTRL_RTRIGGER)
  {
    TabIndex++;
    do
    {
      tab_index = TabIndex;
      if (TabIndex > TAB_MAX) TabIndex = 0;
    } while (tab_index != TabIndex);
  }
  else if ((button_mask & (PSP_CTRL_START | PSP_CTRL_SELECT)) 
    == (PSP_CTRL_START | PSP_CTRL_SELECT))
  {
    if (pspUtilSaveVramSeq(ScreenshotPath, "ui"))
      pspUiAlert("Saved successfully");
    else pspUiAlert("ERROR: Screenshot not saved");
    return 0;
  }
  else return 0;

  return 1;
}

/* Handles any special drawing for the system menu */
void OnSystemRender(const void *uiobject, const void *item_obj)
{
  int w, h, x, y;
  w = Screen->Viewport.Width >> 1;
  h = Screen->Viewport.Height >> 1;
  x = SCR_WIDTH - w - 8;
  y = SCR_HEIGHT - h - 80;

  /* Draw a small representation of the screen */
  pspVideoShadowRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_BLACK, 3);
  pspVideoPutImage(Screen, x, y, w, h);
  pspVideoDrawRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_GRAY);

  OnGenericRender(uiobject, item_obj);
}

int OnMenuItemChanged(const struct PspUiMenu *uimenu, PspMenuItem* item, 
  const PspMenuOption* option)
{
  if (uimenu == &ControlUiMenu)
  {
    ActiveGameConfig.ButtonConfig[item->ID] = (unsigned int)option->Value;
  }
  else if (uimenu == &SystemUiMenu)
  {
    int reinit_controls;

    switch(item->ID)
    {
    case SYSTEM_CROP_SCREEN:
      CropScreen = (int)option->Value;
      break;

    case SYSTEM_STEREO:
      stereo_enabled = (int)option->Value;
      break;

    case SYSTEM_TV_MODE:
      if (Config.VSync
        && (((int)option->Value == TV_NTSC && pspVideoGetVSyncFreq() != 60)
          || ((int)option->Value == TV_PAL && pspVideoGetVSyncFreq() != 50)))
      {
        if (!pspUiConfirm("The frequency of your PSP's video clock\n"
          "does not match the video frequency you selected.\n"
          "Continuing will most likely result in incorrect frame rate.\n\n"
          "Are you sure you want to switch frequencies?"))
            return 0;
      }
      tv_mode = (int)option->Value;
      break;

    case SYSTEM_MACHINE_TYPE:
      if ((MACHINE((int)option->Value) == machine_type 
        && RAM((int)option->Value) == ram_size)
          || !pspUiConfirm("This will reset the system. Proceed?")) return 0;

      if ((reinit_controls = machine_type == MACHINE_5200 
        || MACHINE((int)option->Value) == MACHINE_5200))
      {
			  /* Eject disk and cartridge */
			  CART_Remove();
			  SIO_Dismount(1);
      }

      /* Reconfigure machine type & RAM size */
      machine_type = MACHINE((int)option->Value);
      ram_size = RAM((int)option->Value);

      if (reinit_controls)
      {
			  /* Reset loaded game */
			  if (LoadedGame) free(LoadedGame);
			  LoadedGame = NULL;
				LoadGameConfig(LoadedGame, &ActiveGameConfig);
			}

      Atari800_InitialiseMachine();
      break;
    }
  }
  else if (uimenu == &OptionUiMenu)
  {
    switch(item->ID)
    {
    case OPTION_DISPLAY_MODE:
      Config.DisplayMode = (int)option->Value;
      break;
    case OPTION_FRAMESKIP:
      Config.Frameskip = (int)option->Value;
      break;
    case OPTION_VSYNC:
      if ((int)option->Value 
        && ((tv_mode == TV_NTSC && pspVideoGetVSyncFreq() != 60)
          || (tv_mode == TV_PAL && pspVideoGetVSyncFreq() != 50)))
      {
        if (!pspUiConfirm("The frequency of your PSP's video clock\n"
          "does not match the video frequency of the emulated hardware.\n"
          "Enabling VSync will most likely result in incorrect\n"
          "framerate.\n\nAre you sure you want to enable VSync?"))
            return 0;
      }
      Config.VSync = (int)option->Value;
      break;
    case OPTION_CLOCK_FREQ:
      Config.ClockFreq = (int)option->Value;
      break;
    case OPTION_FRAME_SYNC:
      Config.FrameSync = (int)option->Value;
      break;
    case OPTION_SHOW_FPS:
      Config.ShowFps = (int)option->Value;
      break;
    case OPTION_ANIMATE:
      UiMetric.Animate = (int)option->Value;
      break;
    case OPTION_CONTROL_MODE:
      Config.ControlMode = (int)option->Value;
      UiMetric.OkButton = (Config.ControlMode) 
        ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
      UiMetric.CancelButton = (Config.ControlMode) 
        ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
      break;
    }
  }

  return 1;
}

int OnMenuOk(const void *uimenu, const void* sel_item)
{
  const char *game_name;

  if (uimenu == &ControlUiMenu)
  {
    /* Save to MS */
    const char *config_name = (LoadedGame) 
      ? pspFileIoGetFilename(LoadedGame) : NoCartName;

    if (SaveGameConfig(config_name, &ActiveGameConfig)) 
      pspUiAlert("Layout saved successfully");
    else pspUiAlert("ERROR: Changes not saved");
  }
  else if (uimenu == &SystemUiMenu)
  {
    switch (((const PspMenuItem*)sel_item)->ID)
    {
    case SYSTEM_EJECT:

      if (!pspUiConfirm("This will reset the system. Proceed?"))
        break;

      /* Eject cart & disk (if applicable) */
		  CART_Remove();
		  SIO_Dismount(1);

		  /* Reset loaded game */
		  if (LoadedGame) free(LoadedGame);
		  LoadedGame = NULL;
			LoadGameConfig(LoadedGame, &ActiveGameConfig);
			Atari800_InitialiseMachine();
		  break;

    case SYSTEM_RESET:

      /* Reset system */
      if (pspUiConfirm("Reset the system?"))
      {
        ResumeEmulation = 1;
        Coldstart();
        return 1;
      }
      break;

    case SYSTEM_SCRNSHOT:

      /* Save screenshot */
      game_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
      if (!pspUtilSavePngSeq(ScreenshotPath, game_name, Screen))
        pspUiAlert("ERROR: Screenshot not saved");
      else pspUiAlert("Screenshot saved successfully");
      break;
    }
  }

  return 0;
}

int OnMenuButtonPress(const struct PspUiMenu *uimenu, PspMenuItem* sel_item, 
  u32 button_mask)
{
  if (uimenu == &ControlUiMenu)
  {
    if (button_mask & PSP_CTRL_SQUARE)
    {
      const char *default_file = (machine_type == MACHINE_5200)
        ? DefaultConsoleConfigFile : DefaultComputerConfigFile;

      /* Save to MS as default mapping */
      if (!SaveGameConfig(default_file, &ActiveGameConfig))
        pspUiAlert("ERROR: Changes not saved");
      else
      {
			  GameConfig *config = (machine_type == MACHINE_5200) 
			    ? &DefaultConsoleConfig : &DefaultComputerConfig;

        /* Modify in-memory defaults */
        memcpy(config, &ActiveGameConfig, sizeof(GameConfig));
        pspUiAlert("Layout saved as default");
      }

      return 0;
    }
    else if (button_mask & PSP_CTRL_TRIANGLE)
    {
      PspMenuItem *item;
      int i;

      /* Load default mapping */
      InitGameConfig(&ActiveGameConfig);

      /* Modify the menu */
      for (item = ControlUiMenu.Menu->First, i = 0; item; item = item->Next, i++)
        pspMenuSelectOptionByValue(item, (void*)ActiveGameConfig.ButtonConfig[i]);

      return 0;
    }
  }

  return OnGenericButtonPress(NULL, NULL, button_mask);
}

/* Load state icon */
PspImage* LoadStateIcon(const char *path)
{
  /* Open file for reading */
  FILE *f = fopen(path, "r");
  if (!f) return NULL;

  /* Position pointer at the end and read the position of the image */
  long pos;
  fseek(f, -sizeof(long), SEEK_END);
  fread(&pos, sizeof(long), 1, f);

  /* Reposition to start of image */
  if (fseek(f, pos, SEEK_SET) != 0)
  {
    fclose(f);
    return 0;
  }

  /* Load image */
  PspImage *image = pspImageLoadPngFd(f);
  fclose(f);

  return image;
}

/* Load state */
int LoadState(const char *path)
{
  return ReadAtariState(path, "r");
}

/* Save state */
PspImage* SaveState(const char *path, PspImage *icon)
{
  /* Save state */
  if (!SaveAtariState(path, "w", 1))
    return NULL;

  /* Create thumbnail */
  PspImage *thumb = pspImageCreateThumbnail(icon);
  if (!thumb) return NULL;

  /* Reopen file in append mode */
  FILE *f = fopen(path, "a");
  if (!f)
  {
    pspImageDestroy(thumb);
    return NULL;
  }

  long pos = ftell(f);

  /* Write the thumbnail */
  if (!pspImageSavePngFd(f, thumb))
  {
    pspImageDestroy(thumb);
    fclose(f);
    return NULL;
  }

  /* Write the position of the icon */
  fwrite(&pos, sizeof(long), 1, f);

  fclose(f);
  return thumb;
}

int OnSaveStateOk(const void *gallery, const void *item)
{
  char *path;
  const char *config_name;
  config_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
  path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  sprintf(path, "%s%s.s%02i", SaveStatePath, config_name,
    ((const PspMenuItem*)item)->ID);

  if (pspFileIoCheckIfExists(path) && pspUiConfirm("Load state?"))
  {
    if (LoadState(path))
    {
      ResumeEmulation = 1;
      pspMenuFindItemById(((const PspUiGallery*)gallery)->Menu,
        ((const PspMenuItem*)item)->ID);
      free(path);

      return 1;
    }
    pspUiAlert("ERROR: State failed to load");
  }

  free(path);
  return 0;
}

int OnSaveStateButtonPress(const PspUiGallery *gallery, PspMenuItem *sel, 
  u32 button_mask)
{
  if (button_mask & PSP_CTRL_SQUARE || button_mask & PSP_CTRL_TRIANGLE)
  {
    char *path;
    char caption[32];
	  const char *config_name;
	  config_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
    PspMenuItem *item = pspMenuFindItemById(gallery->Menu, sel->ID);

    path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, item->ID);

    do /* not a real loop; flow control construct */
    {
      if (button_mask & PSP_CTRL_SQUARE)
      {
        if (pspFileIoCheckIfExists(path) 
          && !pspUiConfirm("Overwrite existing state?")) break;

        pspUiFlashMessage("Saving, please wait ...");

        PspImage *icon;
        if (!(icon = SaveState(path, Screen)))
        {
          pspUiAlert("ERROR: State not saved");
          break;
        }

        SceIoStat stat;

        /* Trash the old icon (if any) */
        if (item->Icon && item->Icon != NoSaveIcon)
          pspImageDestroy((PspImage*)item->Icon);

        /* Update icon, help text */
        item->Icon = icon;
        pspMenuSetHelpText(item, PresentSlotText);

        /* Get file modification time/date */
        if (sceIoGetstat(path, &stat) < 0)
          sprintf(caption, "ERROR");
        else
          sprintf(caption, "%02i/%02i/%02i %02i:%02i", 
            stat.st_mtime.month,
            stat.st_mtime.day,
            stat.st_mtime.year - (stat.st_mtime.year / 100) * 100,
            stat.st_mtime.hour,
            stat.st_mtime.minute);

        pspMenuSetCaption(item, caption);
      }
      else if (button_mask & PSP_CTRL_TRIANGLE)
      {
        if (!pspFileIoCheckIfExists(path) || !pspUiConfirm("Delete state?"))
          break;

        if (!pspFileIoDelete(path))
        {
          pspUiAlert("ERROR: State not deleted");
          break;
        }

        /* Trash the old icon (if any) */
        if (item->Icon && item->Icon != NoSaveIcon)
          pspImageDestroy((PspImage*)item->Icon);

        /* Update icon, caption */
        item->Icon = NoSaveIcon;
        pspMenuSetHelpText(item, EmptySlotText);
        pspMenuSetCaption(item, "Empty");
      }
    } while (0);

    if (path) free(path);
    return 0;
  }

  return OnGenericButtonPress(NULL, NULL, button_mask);
}

int LoadCartridge(const char *path)
{
  int type = CART_Insert(path);

  switch(type)
  {
	case CART_CANT_OPEN:
		pspUiAlert("Cannot open file");
		return 0;
	case CART_BAD_CHECKSUM:
		pspUiAlert("Invalid cartridge checksum");
		return 0;
  case CART_BAD_FORMAT:
    pspUiAlert("Unknown cartridge format");
    return 0;
  case 0:
    break;
  default:
	  {
	    int i;
	    PspMenu *menu = pspMenuCreate();

	    for (i = 0; CartType[i]; i++)
  	    if (cart_kb[i + 1] == type) 
  	    	pspMenuAppendItem(menu, CartType[i], i + 1);

	    const PspMenuItem *item = pspUiSelect("Select cartridge type", menu);
	    if (item) cart_type = item->ID;

	    pspMenuDestroy(menu);
      if (!item) return 0;
	  }
	  break;
  }

	if (cart_type != CART_NONE) 
	{
		int for5200 = CART_IsFor5200(cart_type);
		if (for5200 && machine_type != MACHINE_5200) {
			machine_type = MACHINE_5200;
			ram_size = 16;
			Atari800_InitialiseMachine();
		}
		else if (!for5200 && machine_type == MACHINE_5200) {
			machine_type = MACHINE_XLXE;
			ram_size = 64;
			Atari800_InitialiseMachine();
		}
	}

  return 1;
}

int OnQuickloadOk(const void *browser, const void *path)
{
  /* Eject disk and cartridge */
  CART_Remove();
  SIO_Dismount(1);

  switch (Atari800_DetectFileType(path))
  {
	case AFILE_CART:
	case AFILE_ROM:
	  if (!LoadCartridge(path)) return 0;
	  break;
	default:
	  {
	    int old_mach = machine_type;
	    int old_ram = ram_size;

      if (machine_type == MACHINE_5200)
      {
				machine_type = MACHINE_XLXE;
				ram_size = 64;
				Atari800_InitialiseMachine();
			}

		  if (!Atari800_OpenFile(path, 1, 1, 0))
		  {
				machine_type = old_mach;
				ram_size = old_ram;
				Atari800_InitialiseMachine();
		    pspUiAlert("Error loading file"); 
		    return 0; 
		  }
		}
	  break;
	}

  /* Reset loaded game */
  if (LoadedGame) free(LoadedGame);
  LoadedGame = strdup(path);

  /* Reset current game path */
  if (GamePath) free(GamePath);
  GamePath = pspFileIoGetParentDirectory(LoadedGame);

  /* Load control set */
  LoadGameConfig(LoadedGame, &ActiveGameConfig);

  ResumeEmulation = 1;
	Coldstart();

	return 1;
}

/* Initialize game configuration */
static void InitGameConfig(GameConfig *config)
{
  if (config != &DefaultConsoleConfig && config != &DefaultComputerConfig) 
  {
	  GameConfig *default_config = (machine_type == MACHINE_5200) 
	    ? &DefaultConsoleConfig : &DefaultComputerConfig;
    memcpy(config, default_config, sizeof(GameConfig));
  }
}

/* Load game configuration */
static int LoadGameConfig(const char *config_name, GameConfig *config)
{
  char *path;
  config_name = (config_name) ? config_name : NoCartName;
  if (!(path = (char*)malloc(sizeof(char) * (strlen(ConfigPath) 
    + strlen(config_name) + 5)))) return 0;
  sprintf(path, "%s%s.cnf", ConfigPath, config_name);

  /* Open file for reading */
  FILE *file = fopen(path, "r");
  free(path);

  /* If no configuration, load defaults */
  if (!file)
  {
    InitGameConfig(config);
    return 1;
  }

  /* Read contents of struct */
  int nread = fread(config, sizeof(GameConfig), 1, file);
  fclose(file);

  if (nread != 1)
  {
    InitGameConfig(config);
    return 0;
  }

  return 1;
}

/* Save game configuration */
static int SaveGameConfig(const char *config_name, const GameConfig *config)
{
  char *path;
  config_name = (config_name) ? config_name : NoCartName;
  if (!(path = (char*)malloc(sizeof(char) * (strlen(ConfigPath) 
    + strlen(config_name) + 5)))) return 0;
  sprintf(path, "%s%s.cnf", ConfigPath, config_name);

  /* Open file for writing */
  FILE *file = fopen(path, "w");
  free(path);
  if (!file) return 0;

  /* Write contents of struct */
  int nwritten = fwrite(config, sizeof(GameConfig), 1, file);
  fclose(file);

  return (nwritten == 1);
}

void TrashMenu()
{
  /* Save options */
  SaveOptions();

  /* Free emulation-specific resources */
  TrashEmulation();

  /* Free local resources */
  if (Background) pspImageDestroy(Background);
  if (NoSaveIcon) pspImageDestroy(NoSaveIcon);

  if (LoadedGame) free(LoadedGame);
  if (GamePath) free(GamePath);
  if (SaveStatePath) free(SaveStatePath);
  if (ScreenshotPath) free(ScreenshotPath);
  if (ConfigPath) free(ConfigPath);

  if (SaveStateGallery.Menu) pspMenuDestroy(SaveStateGallery.Menu);
  if (OptionUiMenu.Menu) pspMenuDestroy(OptionUiMenu.Menu);
  if (ControlUiMenu.Menu) pspMenuDestroy(ControlUiMenu.Menu);
}

