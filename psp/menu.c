#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>

#include "menu.h"
#include "emulate.h"

#include "akey.h"
#include "afile.h"
#include "pokeysnd.h"
#include "sio.h"
#include "cartridge.h"
#include "statesav.h"
#include "atari.h"
#include "input.h"
#include "memory.h"
#include "screen.h"

#include "video.h"
#include "image.h"
#include "ui.h"
#include "ctrl.h"
#include "pl_psp.h"
#include "pl_util.h"
#include "pl_ini.h"
#include "pl_file.h"
#include "libmz/unzip.h"

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
#define OPTION_TOGGLE_VK    9

#define SYSTEM_SCRNSHOT     1
#define SYSTEM_RESET        2
#define SYSTEM_MACHINE_TYPE 3
#define SYSTEM_TV_MODE      4
#define SYSTEM_CROP_SCREEN  5
#define SYSTEM_STEREO       6
#define SYSTEM_EJECT        7
#define SYSTEM_DRIVE        8

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
    JOY|INPUT_STICK_FORWARD,   /* Analog Up    */
    JOY|INPUT_STICK_BACK,      /* Analog Down  */
    JOY|INPUT_STICK_LEFT,      /* Analog Left  */
    JOY|INPUT_STICK_RIGHT,     /* Analog Right */
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
    CSL|INPUT_CONSOL_SELECT,   /* Select       */
    CSL|INPUT_CONSOL_START,    /* Start        */
    MET|META_SHOW_MENU,  /* L+R Triggers */
    0,                   /* Start+Select */
  }
},
DefaultConsoleConfig = 
{
  {
    JOY|INPUT_STICK_FORWARD,    /* Analog Up    */
    JOY|INPUT_STICK_BACK,       /* Analog Down  */
    JOY|INPUT_STICK_LEFT,       /* Analog Left  */
    JOY|INPUT_STICK_RIGHT,      /* Analog Right */
    JOY|INPUT_STICK_FORWARD,    /* D-pad Up     */
    JOY|INPUT_STICK_BACK,       /* D-pad Down   */
    JOY|INPUT_STICK_LEFT,       /* D-pad Left   */
    JOY|INPUT_STICK_RIGHT,      /* D-pad Right  */
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
static pl_file_path GamePath;
static pl_file_path SaveStatePath;
static pl_file_path ConfigPath;
static pl_file_path ScreenshotPath;
static pl_file_path TempFile;
static pl_file_path TempPath;

static const char 
  *BasicName = "BASIC",
  *EmptyCartName = "5200",
  *OptionsFile = "system/options.ini",
  *ConfigDir = "config",
  *SaveStateDir = "savedata",
  *DefaultComputerConfigFile = "comp_def",
  *DefaultConsoleConfigFile = "cons_def",
  *QuickloadFilter[] = 
    { "ZIP",
      "XEX", "EXE", "COM", "BIN", // Executables
      "ATR", "XFD", "ATR.GZ", "ATZ", "XFD.GZ", "XFZ", "DCM", "PRO", // Disk images
      "BAS", "LST", // Listings
      "CAR", "CART", "ROM", "A52", // Cartridges
      "CAS", // Cassette tapes
      '\0' },
  VacantText[] = "Off",
  *DiskFilter[] = 
    { "ZIP", "ATR", "XFD", "ATR.GZ", "ATZ", "XFD.GZ", "XFZ", "DCM", "PRO", '\0' },
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
PL_MENU_OPTIONS_BEGIN(ToggleOptions)
  PL_MENU_OPTION("Disabled", 0)
  PL_MENU_OPTION("Enabled", 1)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ScreenSizeOptions)
  PL_MENU_OPTION("Actual size", DISPLAY_MODE_UNSCALED)
  PL_MENU_OPTION("4:3 scaled (fit height)", DISPLAY_MODE_FIT_HEIGHT)
  PL_MENU_OPTION("16:9 scaled (fit screen)", DISPLAY_MODE_FILL_SCREEN)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(PspClockFreqOptions)
  PL_MENU_OPTION("222 MHz", 222)
  PL_MENU_OPTION("266 MHz", 266)
  PL_MENU_OPTION("300 MHz", 300)
  PL_MENU_OPTION("333 MHz", 333)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ControlModeOptions)
  PL_MENU_OPTION("\026\242\020 cancels, \026\241\020 confirms (US)", 0)
  PL_MENU_OPTION("\026\241\020 cancels, \026\242\020 confirms (Japan)", 1)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(FrameSkipOptions)
  PL_MENU_OPTION("No skipping",  0)
  PL_MENU_OPTION("Skip 1 frame", 1)
  PL_MENU_OPTION("Skip 2 frame", 2)
  PL_MENU_OPTION("Skip 3 frame", 3)
  PL_MENU_OPTION("Skip 4 frame", 4)
  PL_MENU_OPTION("Skip 5 frame", 5)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(MachineTypeOptions)
  PL_MENU_OPTION("Atari OS/A (16 KB)",MACHINE_TYPE(Atari800_MACHINE_OSA,16))
  PL_MENU_OPTION("Atari OS/A (48 KB)",MACHINE_TYPE(Atari800_MACHINE_OSA,48))
  PL_MENU_OPTION("Atari OS/A (52 KB)",MACHINE_TYPE(Atari800_MACHINE_OSA,52))
  PL_MENU_OPTION("Atari OS/B (16 KB)",MACHINE_TYPE(Atari800_MACHINE_OSB,16))
  PL_MENU_OPTION("Atari OS/B (48 KB)",MACHINE_TYPE(Atari800_MACHINE_OSB,48))
  PL_MENU_OPTION("Atari OS/B (52 KB)",MACHINE_TYPE(Atari800_MACHINE_OSB,52))
  PL_MENU_OPTION("Atari 600XL (16 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,16))
  PL_MENU_OPTION("Atari 800XL (64 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,64))
  PL_MENU_OPTION("Atari 130XE (128 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,128))
  PL_MENU_OPTION("Atari XL/XE (192 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,192))
  PL_MENU_OPTION("Atari XL/XE (320 KB RAMBO)",MACHINE_TYPE(Atari800_MACHINE_XLXE,MEMORY_RAM_320_RAMBO))
  PL_MENU_OPTION("Atari XL/XE (320 KB COMPY SHOP)",MACHINE_TYPE(Atari800_MACHINE_XLXE,MEMORY_RAM_320_COMPY_SHOP))
  PL_MENU_OPTION("Atari XL/XE (576 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,576))
  PL_MENU_OPTION("Atari XL/XE (1088 KB)",MACHINE_TYPE(Atari800_MACHINE_XLXE,1088))
  PL_MENU_OPTION("Atari 5200 (16 KB)",MACHINE_TYPE(Atari800_MACHINE_5200,16))
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(TVModeOptions)
  PL_MENU_OPTION("NTSC", Atari800_TV_NTSC)
  PL_MENU_OPTION("PAL",  Atari800_TV_PAL)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(DiskImageOptions)
  PL_MENU_OPTION(VacantText, 0)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ComputerButtonMapOptions)
  /* Unmapped */
  PL_MENU_OPTION("None", 0)
  /* Special keys */
  PL_MENU_OPTION("Special: Open Menu",     MET|META_SHOW_MENU)
  PL_MENU_OPTION("Special: Show Keyboard", MET|META_SHOW_KEYS)
  /* Console */
  PL_MENU_OPTION("Console: Reset",  SPC|-AKEY_WARMSTART)
  PL_MENU_OPTION("Console: Option", CSL|INPUT_CONSOL_OPTION)
  PL_MENU_OPTION("Console: Select", CSL|INPUT_CONSOL_SELECT)
  PL_MENU_OPTION("Console: Start",  CSL|INPUT_CONSOL_START)
  PL_MENU_OPTION("Console: Help",   KBD|AKEY_HELP)
  /* Joystick */
  PL_MENU_OPTION("Joystick: Up",   JOY|INPUT_STICK_FORWARD)
  PL_MENU_OPTION("Joystick: Down", JOY|INPUT_STICK_BACK)
  PL_MENU_OPTION("Joystick: Left", JOY|INPUT_STICK_LEFT)
  PL_MENU_OPTION("Joystick: Right",JOY|INPUT_STICK_RIGHT)
  PL_MENU_OPTION("Joystick: Fire", TRG|0)
  /* Keyboard */
  PL_MENU_OPTION("Keyboard: Up",   KBD|AKEY_UP) 
  PL_MENU_OPTION("Keyboard: Down", KBD|AKEY_DOWN)
  PL_MENU_OPTION("Keyboard: Left", KBD|AKEY_LEFT) 
  PL_MENU_OPTION("Keyboard: Right",KBD|AKEY_RIGHT)
  /* Keyboard: Function keys */
  PL_MENU_OPTION("F1", KBD|AKEY_F1) PL_MENU_OPTION("F2", KBD|AKEY_F2)
  PL_MENU_OPTION("F3", KBD|AKEY_F3) PL_MENU_OPTION("F4", KBD|AKEY_F4)
  /* Keyboard: misc */
  PL_MENU_OPTION("Space",       KBD|AKEY_SPACE) 
  PL_MENU_OPTION("Return",      KBD|AKEY_RETURN)
  PL_MENU_OPTION("Tab",         KBD|AKEY_TAB)
  PL_MENU_OPTION("Backspace",   KBD|AKEY_BACKSPACE)
  PL_MENU_OPTION("Escape",      KBD|AKEY_ESCAPE)
  PL_MENU_OPTION("Toggle CAPS", KBD|AKEY_CAPSTOGGLE)
  PL_MENU_OPTION("Break",       SPC|-AKEY_BREAK)
  PL_MENU_OPTION("Atari Key",   KBD|AKEY_ATARI)
  PL_MENU_OPTION("Shift",       STA|AKEY_SHFT)
  PL_MENU_OPTION("Control",     STA|AKEY_CTRL)
  /* Keyboard: digits */
  PL_MENU_OPTION("1", KBD|AKEY_1) PL_MENU_OPTION("2", KBD|AKEY_2)
  PL_MENU_OPTION("3", KBD|AKEY_3) PL_MENU_OPTION("4", KBD|AKEY_4)
  PL_MENU_OPTION("5", KBD|AKEY_5) PL_MENU_OPTION("6", KBD|AKEY_6)
  PL_MENU_OPTION("7", KBD|AKEY_7) PL_MENU_OPTION("8", KBD|AKEY_8)
  PL_MENU_OPTION("9", KBD|AKEY_9) PL_MENU_OPTION("0", KBD|AKEY_0)
  /* Keyboard: letters */
  PL_MENU_OPTION("A", KBD|AKEY_a) PL_MENU_OPTION("B", KBD|AKEY_b)
  PL_MENU_OPTION("C", KBD|AKEY_c) PL_MENU_OPTION("D", KBD|AKEY_d)
  PL_MENU_OPTION("E", KBD|AKEY_e) PL_MENU_OPTION("F", KBD|AKEY_f)
  PL_MENU_OPTION("G", KBD|AKEY_g) PL_MENU_OPTION("H", KBD|AKEY_h)
  PL_MENU_OPTION("I", KBD|AKEY_i) PL_MENU_OPTION("J", KBD|AKEY_j)
  PL_MENU_OPTION("K", KBD|AKEY_k) PL_MENU_OPTION("L", KBD|AKEY_l)
  PL_MENU_OPTION("M", KBD|AKEY_m) PL_MENU_OPTION("N", KBD|AKEY_n)
  PL_MENU_OPTION("O", KBD|AKEY_o) PL_MENU_OPTION("P", KBD|AKEY_p)
  PL_MENU_OPTION("Q", KBD|AKEY_q) PL_MENU_OPTION("R", KBD|AKEY_r)
  PL_MENU_OPTION("S", KBD|AKEY_s) PL_MENU_OPTION("T", KBD|AKEY_t)
  PL_MENU_OPTION("U", KBD|AKEY_u) PL_MENU_OPTION("V", KBD|AKEY_v)
  PL_MENU_OPTION("W", KBD|AKEY_w) PL_MENU_OPTION("X", KBD|AKEY_x)
  PL_MENU_OPTION("Y", KBD|AKEY_y) PL_MENU_OPTION("Z", KBD|AKEY_z)
  /* Keyboard: symbols */
  PL_MENU_OPTION("< (less than)",   KBD|AKEY_LESS)
  PL_MENU_OPTION("> (greater than)",KBD|AKEY_GREATER)
  PL_MENU_OPTION("= (equals)",      KBD|AKEY_EQUAL)
  PL_MENU_OPTION("+ (plus)",        KBD|AKEY_PLUS)
  PL_MENU_OPTION("* (asterisk)",    KBD|AKEY_ASTERISK)
  PL_MENU_OPTION("/ (slash)",       KBD|AKEY_SLASH)
  PL_MENU_OPTION(": (colon)",       KBD|AKEY_COLON)
  PL_MENU_OPTION("; (semicolon)",   KBD|AKEY_SEMICOLON)
  PL_MENU_OPTION(", (comma)",       KBD|AKEY_COMMA) 
  PL_MENU_OPTION(". (period)",      KBD|AKEY_FULLSTOP)
  PL_MENU_OPTION("_ (underscore)",  KBD|AKEY_UNDERSCORE)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(ConsoleButtonMapOptions)
  /* Unmapped */
  PL_MENU_OPTION("None", 0)
  /* Special keys */
  PL_MENU_OPTION("Special: Open Menu",     MET|META_SHOW_MENU)
  PL_MENU_OPTION("Special: Show Keyboard", MET|META_SHOW_KEYS)
  /* Console */
  PL_MENU_OPTION("Console: Start", KBD|AKEY_5200_START)
  PL_MENU_OPTION("Console: Pause", KBD|AKEY_5200_PAUSE)
  PL_MENU_OPTION("Console: Reset", KBD|AKEY_5200_RESET)
  /* Joystick */
  PL_MENU_OPTION("Joystick: Up",   JOY|INPUT_STICK_FORWARD)
  PL_MENU_OPTION("Joystick: Down", JOY|INPUT_STICK_BACK)
  PL_MENU_OPTION("Joystick: Left", JOY|INPUT_STICK_LEFT)
  PL_MENU_OPTION("Joystick: Right",JOY|INPUT_STICK_RIGHT)
  PL_MENU_OPTION("Joystick: Fire", TRG|0)
  /* Keypad */
  PL_MENU_OPTION("1", KBD|AKEY_5200_1) PL_MENU_OPTION("2", KBD|AKEY_5200_2)
  PL_MENU_OPTION("3", KBD|AKEY_5200_3) PL_MENU_OPTION("4", KBD|AKEY_5200_4)
  PL_MENU_OPTION("5", KBD|AKEY_5200_5) PL_MENU_OPTION("6", KBD|AKEY_5200_6)
  PL_MENU_OPTION("7", KBD|AKEY_5200_7) PL_MENU_OPTION("8", KBD|AKEY_5200_8)
  PL_MENU_OPTION("9", KBD|AKEY_5200_9) PL_MENU_OPTION("0", KBD|AKEY_5200_0)
  PL_MENU_OPTION("*", KBD|AKEY_5200_ASTERISK)
  PL_MENU_OPTION("#", KBD|AKEY_5200_HASH)
PL_MENU_OPTIONS_END
PL_MENU_OPTIONS_BEGIN(VkModeOptions)
  PL_MENU_OPTION("Display when button is held down (classic mode)", 0)
  PL_MENU_OPTION("Toggle display on and off when button is pressed", 1)
PL_MENU_OPTIONS_END

/* Menu definitions */
PL_MENU_ITEMS_BEGIN(OptionMenuDef)
  PL_MENU_HEADER("Video")
  PL_MENU_ITEM("Screen size", OPTION_DISPLAY_MODE, ScreenSizeOptions, 
    "\026\250\020 Change screen size")
  PL_MENU_HEADER("Input")
  PL_MENU_ITEM("Virtual keyboard mode",OPTION_TOGGLE_VK,VkModeOptions,
               "\026\250\020 Select virtual keyboard mode")
  PL_MENU_HEADER("Performance")
  PL_MENU_ITEM("Frame limiter", OPTION_FRAME_SYNC, ToggleOptions, 
    "\026\250\020 Enable to run the system at proper speed; disable to run as fast as possible")
  PL_MENU_ITEM("Frame skipping", OPTION_FRAMESKIP, FrameSkipOptions, 
    "\026\250\020 Change number of frames skipped per update")
  PL_MENU_ITEM("VSync", OPTION_VSYNC, ToggleOptions, 
    "\026\250\020 Enable to reduce tearing; disable to increase speed")
  PL_MENU_ITEM("PSP clock frequency", OPTION_CLOCK_FREQ, PspClockFreqOptions, 
    "\026\250\020 Larger values: faster emulation, faster battery depletion (default: 222MHz)")
  PL_MENU_ITEM("Show FPS counter", OPTION_SHOW_FPS, ToggleOptions, 
    "\026\250\020 Show/hide the frames-per-second counter")
  PL_MENU_HEADER("Menu")
  PL_MENU_ITEM("Button mode", OPTION_CONTROL_MODE, ControlModeOptions,  
    "\026\250\020 Change OK and Cancel button mapping")
  PL_MENU_ITEM("Animations", OPTION_ANIMATE, ToggleOptions,  
    "\026\250\020 Change Enable/disable in-menu animations")
PL_MENU_ITEMS_END
PL_MENU_ITEMS_BEGIN(ComputerControlMenuDef)
  PL_MENU_ITEM(PSP_CHAR_ANALUP, MAP_ANALOG_UP, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALRIGHT, MAP_ANALOG_RIGHT,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_UP, MAP_BUTTON_UP, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_DOWN, MAP_BUTTON_DOWN, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LEFT, MAP_BUTTON_LEFT, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_RIGHT, MAP_BUTTON_RIGHT,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_SQUARE, MAP_BUTTON_SQUARE, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_CROSS, MAP_BUTTON_CROSS,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_CIRCLE, MAP_BUTTON_CIRCLE, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_SELECT, MAP_BUTTON_SELECT, 
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_START, MAP_BUTTON_START,
    ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, 
    MAP_BUTTON_LRTRIGGERS, ComputerButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_START"+"PSP_CHAR_SELECT, MAP_BUTTON_STARTSELECT,
    ComputerButtonMapOptions, ControlHelpText)
PL_MENU_ITEMS_END
PL_MENU_ITEMS_BEGIN(ConsoleControlMenuDef)
  PL_MENU_ITEM(PSP_CHAR_ANALUP, MAP_ANALOG_UP,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_ANALRIGHT, MAP_ANALOG_RIGHT,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_UP, MAP_BUTTON_UP,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_DOWN, MAP_BUTTON_DOWN,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LEFT, MAP_BUTTON_LEFT,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_RIGHT, MAP_BUTTON_RIGHT,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_SQUARE, MAP_BUTTON_SQUARE,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_CROSS, MAP_BUTTON_CROSS,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_CIRCLE, MAP_BUTTON_CIRCLE,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_SELECT, MAP_BUTTON_SELECT,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_START, MAP_BUTTON_START,
    ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, 
    MAP_BUTTON_LRTRIGGERS, ConsoleButtonMapOptions, ControlHelpText)
  PL_MENU_ITEM(PSP_CHAR_START"+"PSP_CHAR_SELECT, MAP_BUTTON_STARTSELECT,
    ConsoleButtonMapOptions, ControlHelpText)
PL_MENU_ITEMS_END
PL_MENU_ITEMS_BEGIN(SystemMenuDef)
  PL_MENU_HEADER("Storage")
  PL_MENU_ITEM("Disk drive 0", SYSTEM_DRIVE, DiskImageOptions,
    "\026\001\020 Load another disk\t\026"PSP_CHAR_TRIANGLE"\020 Eject disk")
  PL_MENU_ITEM("Eject all", SYSTEM_EJECT, NULL, 
    "\026\001\020 Eject all disks/cartridges")
  PL_MENU_HEADER("Audio")
  PL_MENU_ITEM("Stereo sound", SYSTEM_STEREO, ToggleOptions, 
    "\026\250\020 Toggle stereo sound (dual POKEY emulation)")
  PL_MENU_HEADER("Video")
  PL_MENU_ITEM("Horizontal crop", SYSTEM_CROP_SCREEN, ToggleOptions, 
    "\026\250\020 Remove 8-pixel wide strips on each side of the screen")
  PL_MENU_ITEM("TV frequency", SYSTEM_TV_MODE, TVModeOptions, 
    "\026\250\020 Change video frequency of emulated machine")
  PL_MENU_HEADER("Hardware")
  PL_MENU_ITEM("Machine type", SYSTEM_MACHINE_TYPE, MachineTypeOptions, 
    "\026\250\020 Change emulated machine")
  PL_MENU_HEADER("System")
  PL_MENU_ITEM("Reset", SYSTEM_RESET, NULL, "\026\001\020 Reset")
  PL_MENU_ITEM("Save screenshot",  SYSTEM_SCRNSHOT, NULL, 
    "\026\001\020 Save screenshot")
PL_MENU_ITEMS_END

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

int LoadDiskImage(const char *path);
static const char *PrepareFile(const char *path);

/* Interface callbacks */
static int  OnGenericCancel(const void *uiobject, const void *param);
static void OnGenericRender(const void *uiobject, const void *item_obj);
static int  OnGenericButtonPress(const PspUiFileBrowser *browser, 
  const char *path, u32 button_mask);

static int OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask);
static void OnSplashRender(const void *uiobject, const void *null);

static int OnMenuButtonPress(const struct PspUiMenu *uimenu, pl_menu_item* item,
                             u32 button_mask);
static int OnMenuItemChanged(const struct PspUiMenu *uimenu, pl_menu_item* item,
                             const pl_menu_option* option);
static int OnMenuOk(const void *uimenu, const void* sel_item);

static int OnSaveStateOk(const void *gallery, const void *item);
static int OnSaveStateButtonPress(const PspUiGallery *gallery,
                                  pl_menu_item *sel,
                                  u32 button_mask);

static void OnSystemRender(const void *uiobject, const void *item_obj);

static int OnQuickloadOk(const void *browser, const void *path);
static int OnSwitchDiskImageOk(const void *browser, const void *path);

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

PspUiFileBrowser DiskBrowser = 
{
  OnGenericRender,
  OnSwitchDiskImageOk,
  OnGenericCancel,
  OnGenericButtonPress,
  DiskFilter,
  0
};

PspUiMenu OptionUiMenu =
{
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiGallery SaveStateGallery = 
{
  OnGenericRender,             /* OnRender() */
  OnSaveStateOk,               /* OnOk() */
  OnGenericCancel,             /* OnCancel() */
  OnSaveStateButtonPress,      /* OnButtonPress() */
  NULL                         /* Userdata */
};

PspUiMenu SystemUiMenu =
{
  OnSystemRender,        /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

PspUiMenu ControlUiMenu =
{
  OnGenericRender,       /* OnRender() */
  OnMenuOk,              /* OnOk() */
  OnGenericCancel,       /* OnCancel() */
  OnMenuButtonPress,     /* OnButtonPress() */
  OnMenuItemChanged,     /* OnItemChanged() */
};

void InitMenu()
{
  /* Reset variables */
  TabIndex = TAB_ABOUT;
  Background = NULL;
  NoSaveIcon = NULL;
  LoadedGame = NULL;

  /* Initialize emulation */
  if (!InitEmulation()) return;

  /* Initialize paths */
  sprintf(SaveStatePath, "%s%s/", pl_psp_get_app_directory(), SaveStateDir);
  sprintf(ScreenshotPath, "ms0:/PSP/PHOTO/%s/", PSP_APP_NAME);
  sprintf(ConfigPath, "%s%s/", pl_psp_get_app_directory(), ConfigDir);
  sprintf(TempPath, "%stemp/", pl_psp_get_app_directory());

  /* Create paths */
  if (!pl_file_exists(SaveStatePath))
    pl_file_mkdir_recursive(SaveStatePath);
  if (!pl_file_exists(ConfigPath))
    pl_file_mkdir_recursive(ConfigPath);

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
  UiMetric.SelectedBgColor = COLOR(0xff,0xff,0xff,0x88);
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
  UiMetric.BrowserScreenshotPath = ScreenshotPath;
  UiMetric.BrowserScreenshotDelay = 30;

  /* Initialize options */
  LoadOptions();
  Atari800_InitialiseMachine();

  /* Load the background image */
  Background = pspImageLoadPng("system/background.png");
  UiMetric.Background = Background;

  /* Init NoSaveState icon image */
  NoSaveIcon=pspImageCreate(168, 120, PSP_IMAGE_16BPP);
  pspImageClear(NoSaveIcon, RGB(0x29,0x29,0x29));

  /* Initialize options menu */
  pl_menu_create(&OptionUiMenu.Menu, OptionMenuDef);

  /* Initialize state menu */
  int i;
  pl_menu_item *item;
  for (i = 0; i < 10; i++)
  {
    item = pl_menu_append_item(&SaveStateGallery.Menu, i, NULL);
    pl_menu_set_item_help_text(item, EmptySlotText);
  }

  /* Initialize system menu */
  pl_menu_create(&SystemUiMenu.Menu, SystemMenuDef);

  /* Initialize control menu */
  pl_menu_create(&ControlUiMenu.Menu, ComputerControlMenuDef);

  /* Load default configuration */
  LoadGameConfig(DefaultConsoleConfigFile, &DefaultConsoleConfig);
  LoadGameConfig(DefaultComputerConfigFile, &DefaultComputerConfig);
  LoadGameConfig(NULL, &ActiveGameConfig);
}

void DisplayMenu()
{
  pl_menu_item *item;

  /* Menu loop */
  do
  {
    ResumeEmulation = 0;

    /* Set normal clock frequency */
    pl_psp_set_clock_freq(222);
    /* Set buttons to autorepeat */
    pspCtrlSetPollingMode(PSP_CTRL_AUTOREPEAT);

    /* Reset viewport */
    Screen->Viewport.Width = 336;
    Screen->Viewport.X = (Screen_WIDTH-336)/2;

    /* Display appropriate tab */
    switch (TabIndex)
    {
    case TAB_QUICKLOAD:
      pspUiOpenBrowser(&QuickloadBrowser, (LoadedGame) 
        ? LoadedGame : ((GamePath[0]) ? GamePath : NULL));
      break;

    case TAB_STATE:
      DisplayStateTab();
      break;

    case TAB_CONTROL:
      DisplayControlTab();
      break;

    case TAB_OPTION:
      /* Init menu options */
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_DISPLAY_MODE);
      pl_menu_select_option_by_value(item, (void*)Config.DisplayMode);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_FRAMESKIP);
      pl_menu_select_option_by_value(item, (void*)(int)Config.Frameskip);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_VSYNC);
      pl_menu_select_option_by_value(item, (void*)Config.VSync);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_CLOCK_FREQ);
      pl_menu_select_option_by_value(item, (void*)Config.ClockFreq);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_SHOW_FPS);
      pl_menu_select_option_by_value(item, (void*)Config.ShowFps);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_CONTROL_MODE);
      pl_menu_select_option_by_value(item, (void*)Config.ControlMode);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_ANIMATE);
      pl_menu_select_option_by_value(item, (void*)UiMetric.Animate);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_FRAME_SYNC);
      pl_menu_select_option_by_value(item, (void*)Config.FrameSync);
      item = pl_menu_find_item_by_id(&OptionUiMenu.Menu, OPTION_TOGGLE_VK);
      pl_menu_select_option_by_value(item, (void*)Config.ToggleVK);
      pspUiOpenMenu(&OptionUiMenu, NULL);
      break;

    case TAB_SYSTEM:
      item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_STEREO);
      pl_menu_select_option_by_value(item, (void*)POKEYSND_stereo_enabled);
      item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_DRIVE);
      pl_menu_update_option(item->options,
        pl_file_get_filename(SIO_filename[0]), NULL);
      item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_MACHINE_TYPE);
      pl_menu_select_option_by_value(item, (void*)(MACHINE_TYPE(Atari800_machine_type, MEMORY_ram_size)));
      item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_CROP_SCREEN);
      pl_menu_select_option_by_value(item, (void*)CropScreen);
      item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_TV_MODE);
      pl_menu_select_option_by_value(item, (void*)Atari800_tv_mode);
      pspUiOpenMenu(&SystemUiMenu, NULL);
      break;

    case TAB_ABOUT:
      pspUiSplashScreen(&SplashScreen);
      break;
    }

    if (!ExitPSP)
    {
      /* Set clock frequency during emulation */
      pl_psp_set_clock_freq(Config.ClockFreq);
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
  pl_menu_item *item;
  const char *config_name;
  config_name = (LoadedGame) ? pl_file_get_filename(LoadedGame)
    : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
  char *game_name = strdup(config_name);
  char *dot = strrchr(game_name, '.');
  if (dot) *dot='\0';

  /* Reinitialize the control menu to reflect console or computer */
  pl_menu_destroy(&ControlUiMenu.Menu);
  pl_menu_create(&ControlUiMenu.Menu, (Atari800_machine_type == Atari800_MACHINE_5200) 
    ? ConsoleControlMenuDef : ComputerControlMenuDef);

  /* Load current button mappings */
  for (item = ControlUiMenu.Menu.items, i = 0; item; item = item->next, i++)
    pl_menu_select_option_by_value(item, (void*)ActiveGameConfig.ButtonConfig[i]);

  pspUiOpenMenu(&ControlUiMenu, game_name);
  free(game_name);
}

static void DisplayStateTab()
{
  pl_menu_item *item;
  SceIoStat stat;
  char caption[128];

  const char *config_name;
  config_name = (LoadedGame) ? pl_file_get_filename(LoadedGame)
    : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
  char *path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  char *game_name = strdup(config_name);
  char *dot = strrchr(game_name, '.');
  if (dot) *dot='\0';

  /* Initialize icons */
  for (item = SaveStateGallery.Menu.items; item; item = item->next)
  {
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, item->id);

    if (pl_file_exists(path))
    {
      if (sceIoGetstat(path, &stat) < 0)
        sprintf(caption, "ERROR");
      else
        sprintf(caption, "%02i/%02i/%02i %02i:%02i",
          stat.st_mtime.month, stat.st_mtime.day,
          stat.st_mtime.year - (stat.st_mtime.year / 100) * 100,
          stat.st_mtime.hour, stat.st_mtime.minute);

      pl_menu_set_item_caption(item, caption);
      item->param = LoadStateIcon(path);
      pl_menu_set_item_help_text(item, PresentSlotText);
    }
    else
    {
      pl_menu_set_item_caption(item, "Empty");
      item->param = NoSaveIcon;
      pl_menu_set_item_help_text(item, EmptySlotText);
    }
  }

  free(path);
  pspUiOpenGallery(&SaveStateGallery, game_name);
  free(game_name);

  /* Destroy any icons */
  for (item = SaveStateGallery.Menu.items; item; item = item->next)
    if (item->param != NULL && item->param != NoSaveIcon)
      pspImageDestroy((PspImage*)item->param);
}

/* Load options */
void LoadOptions()
{
  pl_file_path path;
  sprintf(path, "%s%s", pl_psp_get_app_directory(), OptionsFile);

  /* Read the file */
  pl_ini_file init;
  pl_ini_load(&init, path);

  /* Load values */
  Config.DisplayMode = pl_ini_get_int(&init, "Video", "Display Mode", 
    DISPLAY_MODE_UNSCALED);
  Config.Frameskip = pl_ini_get_int(&init, "Video", "Frameskip", 0);
  Config.VSync     = pl_ini_get_int(&init, "Video", "VSync", 0);
  Config.FrameSync = pl_ini_get_int(&init, "Video", "Frame Sync", 1);
  Config.ClockFreq = pl_ini_get_int(&init, "Video", "PSP Clock Frequency", 222);
  Config.ShowFps   = pl_ini_get_int(&init, "Video", "Show FPS", 0);
  Config.ControlMode = pl_ini_get_int(&init, "Menu", "Control Mode", 0);
  UiMetric.Animate = pl_ini_get_int(&init, "Menu",  "Animations", 1);
  Config.ToggleVK = pl_ini_get_int(&init, "Input", "VK Mode", 0);

  CropScreen = pl_ini_get_int(&init, "System", "Crop screen", 0);
  Atari800_machine_type = pl_ini_get_int(&init, "System", "Machine Type", Atari800_MACHINE_XLXE);
  MEMORY_ram_size = pl_ini_get_int(&init, "System", "RAM Size", 64);
  Atari800_tv_mode = pl_ini_get_int(&init, "System", "TV mode", Atari800_TV_PAL);
  POKEYSND_stereo_enabled = pl_ini_get_int(&init, "System", "Stereo", 0);

  pl_ini_get_string(&init, "File", "Game Path", NULL, GamePath, sizeof(GamePath));

  /* Clean up */
  pl_ini_destroy(&init);
}

/* Save options */
static int SaveOptions()
{
  pl_file_path path;
  sprintf(path, "%s%s", pl_psp_get_app_directory(), OptionsFile);

  /* Initialize INI structure */
  pl_ini_file init;
  pl_ini_create(&init);

  /* Set values */
  pl_ini_set_int(&init, "Video", "Display Mode", Config.DisplayMode);
  pl_ini_set_int(&init, "Video", "Frameskip", Config.Frameskip);
  pl_ini_set_int(&init, "Video", "VSync", Config.VSync);
  pl_ini_set_int(&init, "Video", "Frame Sync", Config.FrameSync);
  pl_ini_set_int(&init, "Video", "PSP Clock Frequency",Config.ClockFreq);
  pl_ini_set_int(&init, "Video", "Show FPS", Config.ShowFps);
  pl_ini_set_int(&init, "Menu",  "Control Mode", Config.ControlMode);
  pl_ini_set_int(&init, "Menu",  "Animations", UiMetric.Animate);
  pl_ini_set_int(&init, "Input",  "VK Mode", Config.ToggleVK);

  pl_ini_set_int(&init, "System", "Crop screen", CropScreen);
  pl_ini_set_int(&init, "System", "Machine Type", Atari800_machine_type);
  pl_ini_set_int(&init, "System", "RAM Size", MEMORY_ram_size);
  pl_ini_set_int(&init, "System", "TV mode", Atari800_tv_mode);
  pl_ini_set_int(&init, "System", "Stereo", POKEYSND_stereo_enabled);

  pl_ini_set_string(&init, "File", "Game Path", GamePath);

  /* Save INI file */
  int status = pl_ini_save(&init, path);

  /* Clean up */
  pl_ini_destroy(&init);

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
    "2007 Akop Karapetyan",
    "1997-2007 Atari800 team",
    "1995-1997 David Firth",
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
  else return 0;

  return 1;
}

/* Handles any special drawing for the system menu */
void OnSystemRender(const void *uiobject, const void *item_obj)
{
  int w, h, x, y;

  w = Screen->Viewport.Width>>1;
  h = Screen->Viewport.Height>>1;
  x = SCR_WIDTH-w-8;
  y = (SCR_HEIGHT>>1)-(h>>1);

  /* Draw a small representation of the screen */
  pspVideoShadowRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_BLACK, 3);
  pspVideoPutImage(Screen, x, y, w, h);
  pspVideoDrawRect(x, y, x + w - 1, y + h - 1, PSP_COLOR_GRAY);

  OnGenericRender(uiobject, item_obj);
}

static int OnMenuItemChanged(const struct PspUiMenu *uimenu, pl_menu_item* item,
                             const pl_menu_option* option)
{
  if (uimenu == &ControlUiMenu)
  {
    ActiveGameConfig.ButtonConfig[item->id] = (unsigned int)option->value;
  }
  else if (uimenu == &SystemUiMenu)
  {
    int reinit_controls;

    switch(item->id)
    {
    case SYSTEM_CROP_SCREEN:
      CropScreen = (int)option->value;
      break;

    case SYSTEM_STEREO:
      POKEYSND_stereo_enabled = (int)option->value;
      break;

    case SYSTEM_TV_MODE:
      if (Config.VSync
        && (((int)option->value == Atari800_TV_NTSC && pspVideoGetVSyncFreq() != 60)
          || ((int)option->value == Atari800_TV_PAL && pspVideoGetVSyncFreq() != 50)))
      {
        if (!pspUiConfirm("The frequency of your PSP's video clock\n"
          "does not match the video frequency you selected.\n"
          "Continuing will most likely result in incorrect frame rate.\n\n"
          "Are you sure you want to switch frequencies?"))
            return 0;
      }
      Atari800_tv_mode = (int)option->value;
      break;

    case SYSTEM_MACHINE_TYPE:
      if ((MACHINE((int)option->value) == Atari800_machine_type 
        && RAM((int)option->value) == MEMORY_ram_size)
          || !pspUiConfirm("This will reset the system. Proceed?")) return 0;

      if ((reinit_controls = Atari800_machine_type == Atari800_MACHINE_5200 
        || MACHINE((int)option->value) == Atari800_MACHINE_5200))
      {
			  /* Eject disk and cartridge */
			  CARTRIDGE_Remove();
			  SIO_Dismount(1);
      }

      /* Reconfigure machine type & RAM size */
      Atari800_machine_type = MACHINE((int)option->value);
      MEMORY_ram_size = RAM((int)option->value);

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
    
    /* Refresh disk image indicator */
    pl_menu_item *item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_DRIVE);
    pl_menu_update_option(item->options, pl_file_get_filename(SIO_filename[0]),
      NULL);
  }
  else if (uimenu == &OptionUiMenu)
  {
    switch(item->id)
    {
    case OPTION_DISPLAY_MODE:
      Config.DisplayMode = (int)option->value;
      break;
    case OPTION_FRAMESKIP:
      Config.Frameskip = (int)option->value;
      break;
    case OPTION_VSYNC:
      if ((int)option->value 
        && ((Atari800_tv_mode == Atari800_TV_NTSC && pspVideoGetVSyncFreq() != 60)
          || (Atari800_tv_mode == Atari800_TV_PAL && pspVideoGetVSyncFreq() != 50)))
      {
        if (!pspUiConfirm("The frequency of your PSP's video clock\n"
          "does not match the video frequency of the emulated hardware.\n"
          "Enabling VSync will most likely result in incorrect\n"
          "framerate.\n\nAre you sure you want to enable VSync?"))
            return 0;
      }
      Config.VSync = (int)option->value;
      break;
    case OPTION_CLOCK_FREQ:
      Config.ClockFreq = (int)option->value;
      break;
    case OPTION_TOGGLE_VK:
      Config.ToggleVK = (int)option->value;
      break;
    case OPTION_FRAME_SYNC:
      Config.FrameSync = (int)option->value;
      break;
    case OPTION_SHOW_FPS:
      Config.ShowFps = (int)option->value;
      break;
    case OPTION_ANIMATE:
      UiMetric.Animate = (int)option->value;
      break;
    case OPTION_CONTROL_MODE:
      Config.ControlMode = (int)option->value;
      UiMetric.OkButton = (Config.ControlMode) 
        ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
      UiMetric.CancelButton = (Config.ControlMode) 
        ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
      break;
    }
  }

  return 1;
}

static int OnSwitchDiskImageOk(const void *browser, const void *file_path)
{
  const char *path = PrepareFile(file_path);

  /* Detect disk type */
  switch(AFILE_DetectFileType((char*)path))
  {
  case AFILE_ATR:
  case AFILE_XFD:
  case AFILE_ATR_GZ:
  case AFILE_XFD_GZ:
  case AFILE_DCM:
    break;
  default:
    pspUiAlert("Unrecognized or unsupported disk image");
    return 0;
  }

  /* Eject disk */
  SIO_Dismount(1);

  /* Switch system if necessary */
  int old_mach = Atari800_machine_type;
  int old_ram = MEMORY_ram_size;
  int reset = 0;
  if (Atari800_machine_type == Atari800_MACHINE_5200)
  {
    reset = 1;
    Atari800_machine_type = Atari800_MACHINE_XLXE;
    MEMORY_ram_size = 64;
    Atari800_InitialiseMachine();
  }

  /* Load image */
  if (!AFILE_OpenFile((char*)path, reset, 1, 0))
  {
    if (reset)
    {
      Atari800_machine_type = old_mach;
      MEMORY_ram_size = old_ram;
      Atari800_InitialiseMachine();
      pspUiAlert("Error loading file");
      return 0;
    }
  }

  /* Reset loaded game */
  if (LoadedGame) free(LoadedGame);
  LoadedGame = strdup((char*)file_path);

  /* Reset current game path */
  pl_file_get_parent_directory(LoadedGame, GamePath, sizeof(GamePath));

  /* Load control set */
  LoadGameConfig(LoadedGame, &ActiveGameConfig);

  ResumeEmulation = 1;
  return 1;
}

int OnMenuOk(const void *uimenu, const void* sel_item)
{
  const char *game_name;

  if (uimenu == &ControlUiMenu)
  {
    /* Save to MS */
    const char *config_name = (LoadedGame) 
      ? pl_file_get_filename(LoadedGame)
      : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;

    if (SaveGameConfig(config_name, &ActiveGameConfig)) 
      pspUiAlert("Layout saved successfully");
    else pspUiAlert("ERROR: Changes not saved");
  }
  else if (uimenu == &SystemUiMenu)
  {
    switch (((const pl_menu_item*)sel_item)->id)
    {
    case SYSTEM_DRIVE:
      pspUiOpenBrowser(&DiskBrowser, (LoadedGame) 
        ? LoadedGame : ((GamePath[0]) ? GamePath : NULL));
      break;

    case SYSTEM_EJECT:

      if (!pspUiConfirm("This will reset the system. Proceed?"))
        break;

      /* Eject cart & disk (if applicable) */
		  CARTRIDGE_Remove();
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
        Atari800_Coldstart();
        return 1;
      }
      break;

    case SYSTEM_SCRNSHOT:

      /* Save screenshot */
      game_name = (LoadedGame) ? pl_file_get_filename(LoadedGame)
        : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
      if (!pl_util_save_image_seq(ScreenshotPath, game_name, Screen))
        pspUiAlert("ERROR: Screenshot not saved");
      else pspUiAlert("Screenshot saved successfully");
      break;
    }

    /* Refresh disk image indicator */
    pl_menu_item *item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_DRIVE);
    pl_menu_update_option(item->options, pl_file_get_filename(SIO_filename[0]),
      NULL);
  }

  return 0;
}

int OnMenuButtonPress(const struct PspUiMenu *uimenu, pl_menu_item *sel_item, 
  u32 button_mask)
{
  if (uimenu == &ControlUiMenu)
  {
    if (button_mask & PSP_CTRL_SQUARE)
    {
      const char *default_file = (Atari800_machine_type == Atari800_MACHINE_5200)
        ? DefaultConsoleConfigFile : DefaultComputerConfigFile;

      /* Save to MS as default mapping */
      if (!SaveGameConfig(default_file, &ActiveGameConfig))
        pspUiAlert("ERROR: Changes not saved");
      else
      {
			  GameConfig *config = (Atari800_machine_type == Atari800_MACHINE_5200) 
			    ? &DefaultConsoleConfig : &DefaultComputerConfig;

        /* Modify in-memory defaults */
        memcpy(config, &ActiveGameConfig, sizeof(GameConfig));
        pspUiAlert("Layout saved as default");
      }

      return 0;
    }
    else if (button_mask & PSP_CTRL_TRIANGLE)
    {
      pl_menu_item *item;
      int i;

      /* Load default mapping */
      InitGameConfig(&ActiveGameConfig);

      /* Modify the menu */
      for (item = ControlUiMenu.Menu.items, i = 0; item; item = item->next, i++)
        pl_menu_select_option_by_value(item, (void*)ActiveGameConfig.ButtonConfig[i]);

      return 0;
    }
  }
  else if (uimenu == &SystemUiMenu)
  {
    if (sel_item->id == SYSTEM_DRIVE && (button_mask & PSP_CTRL_TRIANGLE)
      && (SIO_drive_status[0] != SIO_OFF && SIO_drive_status[0] != SIO_NO_DISK)
      && pspUiConfirm("Eject disk?"))
    {
      /* Eject disk */
      SIO_Dismount(1);

      /* Refresh disk image indicator */
      pl_menu_item *item = pl_menu_find_item_by_id(&SystemUiMenu.Menu, SYSTEM_DRIVE);
      pl_menu_update_option(item->options, pl_file_get_filename(SIO_filename[0]),
        NULL);
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
  return StateSav_ReadAtariState(path, "r");
}

/* Save state */
PspImage* SaveState(const char *path, PspImage *icon)
{
  /* Save state */
  if (!StateSav_SaveAtariState(path, "w", 1))
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
  config_name = (LoadedGame) ? pl_file_get_filename(LoadedGame)
    : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
  path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
  sprintf(path, "%s%s.s%02i", SaveStatePath, config_name,
    ((const pl_menu_item*)item)->id);

  if (pl_file_exists(path) && pspUiConfirm("Load state?"))
  {
    if (LoadState(path))
    {
      ResumeEmulation = 1;
      pl_menu_find_item_by_id(&((const PspUiGallery*)gallery)->Menu,
        ((const pl_menu_item*)item)->id);
      free(path);

      return 1;
    }
    pspUiAlert("ERROR: State failed to load");
  }

  free(path);
  return 0;
}

int OnSaveStateButtonPress(const PspUiGallery *gallery, pl_menu_item *sel, 
  u32 button_mask)
{
  if (button_mask & PSP_CTRL_SQUARE || button_mask & PSP_CTRL_TRIANGLE)
  {
    char *path;
    char caption[32];
	  const char *config_name;
	  config_name = (LoadedGame) ? pl_file_get_filename(LoadedGame)
      : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
    pl_menu_item *item = pl_menu_find_item_by_id(&gallery->Menu, sel->id);

    path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, item->id);

    do /* not a real loop; flow control construct */
    {
      if (button_mask & PSP_CTRL_SQUARE)
      {
        if (pl_file_exists(path) 
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
        if (item->param && item->param != NoSaveIcon)
          pspImageDestroy((PspImage*)item->param);

        /* Update icon, help text */
        item->param = icon;
        pl_menu_set_item_help_text(item, PresentSlotText);

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

        pl_menu_set_item_caption(item, caption);
      }
      else if (button_mask & PSP_CTRL_TRIANGLE)
      {
        if (!pl_file_exists(path) || !pspUiConfirm("Delete state?"))
          break;

        if (!pl_file_rm(path))
        {
          pspUiAlert("ERROR: State not deleted");
          break;
        }

        /* Trash the old icon (if any) */
        if (item->param && item->param != NoSaveIcon)
          pspImageDestroy((PspImage*)item->param);

        /* Update icon, caption */
        item->param = NoSaveIcon;
        pl_menu_set_item_help_text(item, EmptySlotText);
        pl_menu_set_item_caption(item, "Empty");
      }
    } while (0);

    if (path) free(path);
    return 0;
  }

  return OnGenericButtonPress(NULL, NULL, button_mask);
}

int LoadCartridge(const char *path)
{
  int type = CARTRIDGE_Insert(path);

  switch(type)
  {
	case CARTRIDGE_CANT_OPEN:
		pspUiAlert("Cannot open file");
		return 0;
	case CARTRIDGE_BAD_CHECKSUM:
		pspUiAlert("Invalid cartridge checksum");
		return 0;
  case CARTRIDGE_BAD_FORMAT:
    pspUiAlert("Unknown cartridge format");
    return 0;
  case 0:
    break;
  default:
	  {
	    int i;
	    pl_menu menu;
      pl_menu_create(&menu, NULL);

	    for (i = 0; CartType[i]; i++)
  	    if (CARTRIDGE_kb[i + 1] == type) 
          pl_menu_append_item(&menu, i + 1, CartType[i]);

	    const pl_menu_item *item = pspUiSelect("Select cartridge type", &menu);
	    if (item) CARTRIDGE_type = item->id;

	    pl_menu_destroy(&menu);
      if (!item) return 0;
	  }
	  break;
  }

	if (CARTRIDGE_type != CARTRIDGE_NONE) 
	{
		int for5200 = CARTRIDGE_IsFor5200(CARTRIDGE_type);
		if (for5200 && Atari800_machine_type != Atari800_MACHINE_5200) {
			Atari800_machine_type = Atari800_MACHINE_5200;
			MEMORY_ram_size = 16;
			Atari800_InitialiseMachine();
		}
		else if (!for5200 && Atari800_machine_type == Atari800_MACHINE_5200) {
			Atari800_machine_type = Atari800_MACHINE_XLXE;
			MEMORY_ram_size = 64;
			Atari800_InitialiseMachine();
		}
	}

  return 1;
}

static const char *PrepareFile(const char *path)
{
  const char *game_path = path;
  void *file_buffer = NULL;
  int file_size = 0;

  if (pl_file_is_of_type(path, "ZIP"))
  {
    pspUiFlashMessage("Loading compressed file, please wait...");

    char archived_file[512];
    unzFile zipfile = NULL;
    unz_global_info gi;
    unz_file_info fi;

    /* Open archive for reading */
    if (!(zipfile = unzOpen(path)))
      return NULL;

    /* Get global ZIP file information */
    if (unzGetGlobalInfo(zipfile, &gi) != UNZ_OK)
    {
      unzClose(zipfile);
      return NULL;
    }

    const char *extension;
    int i, j;

    for (i = 0; i < (int)gi.number_entry; i++)
    {
      /* Get name of the archived file */
      if (unzGetCurrentFileInfo(zipfile, &fi, archived_file, 
          sizeof(archived_file), NULL, 0, NULL, 0) != UNZ_OK)
      {
        unzClose(zipfile);
        return NULL;
      }

      extension = pl_file_get_extension(archived_file);
      for (j = 1; QuickloadFilter[j]; j++)
      {
        if (strcasecmp(QuickloadFilter[j], extension) == 0)
        {
          file_size = fi.uncompressed_size;

          /* Open archived file for reading */
          if(unzOpenCurrentFile(zipfile) != UNZ_OK)
          {
            unzClose(zipfile); 
            return NULL;
          }

          if (!(file_buffer = malloc(file_size)))
          {
            unzCloseCurrentFile(zipfile);
            unzClose(zipfile); 
            return NULL;
          }

          unzReadCurrentFile(zipfile, file_buffer, file_size);
          unzCloseCurrentFile(zipfile);

          goto close_archive;
        }
      }

      /* Go to the next file in the archive */
      if (i + 1 < (int)gi.number_entry)
      {
        if (unzGoToNextFile(zipfile) != UNZ_OK)
        {
          unzClose(zipfile);
          return NULL;
        }
      }
    }

    /* No eligible files */
    return NULL;

close_archive:
    unzClose(zipfile);

    /* Create temp. path */
		if (!pl_file_exists(TempPath))
		  pl_file_mkdir_recursive(TempPath);

    /* Remove temp. file (if any) */
    if (*TempFile && pl_file_exists(TempFile))
      pl_file_rm(TempFile);

    /* Define temp filename */
    sprintf(TempFile, "%s%s", TempPath, archived_file);

    /* Write file to stick */
    FILE *file = fopen(TempFile, "w");
    if (!file)
    {
      *TempFile = '\0';
      return NULL;
    }
    if (fwrite(file_buffer, 1, file_size, file) < file_size)
    {
      fclose(file);
      *TempFile = '\0';
      return NULL;
    }
    fclose(file);

    game_path = TempFile;
  }

  return game_path;
}

int OnQuickloadOk(const void *browser, const void *file_path)
{
  /* Eject disk and cartridge */
  CARTRIDGE_Remove();
  SIO_Dismount(1);

  const char *path = PrepareFile(file_path);

  switch (AFILE_DetectFileType(path))
  {
	case AFILE_CART:
	case AFILE_ROM:
	  if (!LoadCartridge(path)) return 0;
	  break;
	default:
	  {
	    int old_mach = Atari800_machine_type;
	    int old_ram = MEMORY_ram_size;

      if (Atari800_machine_type == Atari800_MACHINE_5200)
      {
				Atari800_machine_type = Atari800_MACHINE_XLXE;
				MEMORY_ram_size = 64;
				Atari800_InitialiseMachine();
			}

		  if (!AFILE_OpenFile(path, 1, 1, 0))
		  {
				Atari800_machine_type = old_mach;
				MEMORY_ram_size = old_ram;
				Atari800_InitialiseMachine();
		    pspUiAlert("Error loading file"); 
		    return 0; 
		  }
		}
	  break;
	}

  /* Reset loaded game */
  if (LoadedGame) free(LoadedGame);
  LoadedGame = strdup(file_path);

  /* Reset current game path */
  pl_file_get_parent_directory(LoadedGame, GamePath, sizeof(GamePath));

  /* Load control set */
  LoadGameConfig(LoadedGame, &ActiveGameConfig);

  ResumeEmulation = 1;
	Atari800_Coldstart();

	return 1;
}

/* Initialize game configuration */
static void InitGameConfig(GameConfig *config)
{
  if (config != &DefaultConsoleConfig && config != &DefaultComputerConfig) 
  {
	  GameConfig *default_config = (Atari800_machine_type == Atari800_MACHINE_5200) 
	    ? &DefaultConsoleConfig : &DefaultComputerConfig;
    memcpy(config, default_config, sizeof(GameConfig));
  }
}

/* Load game configuration */
static int LoadGameConfig(const char *config_name, GameConfig *config)
{
  char *path;
  config_name = (config_name) ? pl_file_get_filename(config_name)
    : ((Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName);
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
  config_name = (config_name) ? config_name
    : (Atari800_machine_type == Atari800_MACHINE_5200) ? EmptyCartName : BasicName;
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

  /* Remove temp. files (if any) */
  if (*TempFile && pl_file_exists(TempFile))
    pl_file_rm(TempFile);
}

