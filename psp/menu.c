#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>

#include "menu.h"
#include "emulate.h"

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

#define SYSTEM_SCRNSHOT     1
#define SYSTEM_RESET        2
#define SYSTEM_MACHINE_TYPE 3
#define SYSTEM_TV_MODE      4

#define M_TYPE(machine, ram) ((ram) << 8 | ((machine) & 0xff))
#define MACHINE(mtype)       ((mtype) & 0xff)
#define RAM(mtype)           ((mtype) >> 8)

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
char *ScreenshotPath;

static const char 
  *NoCartName = "BASIC",
  *OptionsFile = "options.ini",
  *ScreenshotDir = "screens",
  *SaveStateDir = "savedata",
  *QuickloadFilter[] = 
    { "XEX", "EXE", "COM", "BIN", /* Executables */
      "ATR", "XFD", "ATR.GZ", "ATZ", "XFD.GZ", "XFZ", "DCM", /* Disk images */
      "BAS", "LST", /* Listings */
      "CAR", "CART", "ROM", /* Cartridges */
      "CAS", /* Cassette tapes */
      '\0' },
  PresentSlotText[] = "\026\244\020 Save\t\026\001\020 Load\t\026\243\020 Delete",
  EmptySlotText[] = "\026\244\020 Save",
  ControlHelpText[] = "\026\250\020 Change mapping\t\026\001\020 Save to \271\t\026\243\020 Load defaults";

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
    { "Disabled", (void*)0 },
    { "Enabled", (void*)1 },
    { NULL, NULL } },
  ScreenSizeOptions[] = {
    { "Actual size", (void*)DISPLAY_MODE_UNSCALED },
    { "4:3 scaled (fit height)", (void*)DISPLAY_MODE_FIT_HEIGHT },
    { "16:9 scaled (fit screen)", (void*)DISPLAY_MODE_FILL_SCREEN },
    { NULL, NULL } },
  FrameSkipOptions[] = {
    { "No skipping", (void*)0 },
    { "Skip 1 frame", (void*)1 },
    { "Skip 2 frames", (void*)2 },
    { "Skip 3 frames", (void*)3 },
    { "Skip 4 frames", (void*)4 },
    { "Skip 5 frames", (void*)5 },
    { NULL, NULL } },
  PspClockFreqOptions[] = {
    { "222 MHz", (void*)222 },
    { "266 MHz", (void*)266 },
    { "300 MHz", (void*)300 },
    { "333 MHz", (void*)333 },
    { NULL, NULL } },
  ControlModeOptions[] = {
    { "\026\242\020 cancels, \026\241\020 confirms (US)", (void*)0 },
    { "\026\241\020 cancels, \026\242\020 confirms (Japan)", (void*)1 },
    { NULL, NULL } },
  MachineTypeOptions[] = {
    { "800",                 (void*)M_TYPE(MACHINE_OSB,  48) },
    { "800 XL",              (void*)M_TYPE(MACHINE_XLXE, 64) },
    { "130 XE",              (void*)M_TYPE(MACHINE_XLXE, 128) },
    { "320 XE (Compy Shop)", (void*)M_TYPE(MACHINE_XLXE, RAM_320_COMPY_SHOP) },
    { "320 XE (Rambo)",      (void*)M_TYPE(MACHINE_XLXE, RAM_320_RAMBO) },
    { "5200",                (void*)M_TYPE(MACHINE_5200, 16) },
    { NULL, NULL } },
  TVModeOptions[] = {
    { "NTSC", (void*)TV_NTSC },
    { "PAL",  (void*)TV_PAL },
    { NULL, NULL } },
  ComputerButtonMapOptions[] = {
    /* Unmapped */
    { "None", (void*)0 },
    /* Special keys */
    { "Special: Open Menu",            (void*)(MET|META_SHOW_MENU) },
    { "Special: Show Keyboard/Keypad", (void*)(MET|META_SHOW_KEYS) },
    /* Console */
    { "Console: Reset",  (void*)(SPC|-AKEY_WARMSTART) },
    { "Console: Option", (void*)(CSL|CONSOL_OPTION)   },
    { "Console: Select", (void*)(CSL|CONSOL_SELECT)   },
    { "Console: Start",  (void*)(CSL|CONSOL_START)    },
    { "Console: Help",   (void*)(KBD|AKEY_HELP)      },
    /* Joystick */
    { "Joystick: Up",   (void*)(JOY|STICK_FORWARD) },
    { "Joystick: Down", (void*)(JOY|STICK_BACK)    },
    { "Joystick: Left", (void*)(JOY|STICK_LEFT)    },
    { "Joystick: Up",   (void*)(JOY|STICK_RIGHT)   },
    { "Joystick: Fire", (void*)(TRG|0) },
    /* Keyboard */
    { "Keyboard: Up",   (void*)(KBD|AKEY_UP)    }, 
    { "Keyboard: Down", (void*)(KBD|AKEY_DOWN)  },
    { "Keyboard: Left", (void*)(KBD|AKEY_LEFT)  }, 
    { "Keyboard: Right",(void*)(KBD|AKEY_RIGHT) },
    /* Keyboard: Function keys */
    { "F1",(void*)(KBD|AKEY_F1) }, { "F2",(void*)(KBD|AKEY_F2) },
    { "F3",(void*)(KBD|AKEY_F3) }, { "F4",(void*)(KBD|AKEY_F4) },
    /* Keyboard: misc */
    { "Space",       (void*)(KBD|AKEY_SPACE) }, 
    { "Return",      (void*)(KBD|AKEY_RETURN) },
    { "Tab",         (void*)(KBD|AKEY_TAB) }, 
    { "Backspace",   (void*)(KBD|AKEY_BACKSPACE) },
    { "Escape",      (void*)(KBD|AKEY_ESCAPE) },
    { "Toggle CAPS", (void*)(KBD|AKEY_CAPSTOGGLE) },
    { "Break",       (void*)(SPC|-AKEY_BREAK) },
    { "Atari Key",   (void*)(KBD|AKEY_ATARI) },
    { "Shift",       (void*)(STA|AKEY_SHFT) },
    { "Control",     (void*)(STA|AKEY_CTRL) },
    /* Keyboard: digits */
    { "1",(void*)(KBD|AKEY_1) }, { "2",(void*)(KBD|AKEY_2) },
    { "3",(void*)(KBD|AKEY_3) }, { "4",(void*)(KBD|AKEY_4) },
    { "5",(void*)(KBD|AKEY_5) }, { "6",(void*)(KBD|AKEY_6) },
    { "7",(void*)(KBD|AKEY_7) }, { "8",(void*)(KBD|AKEY_8) },
    { "9",(void*)(KBD|AKEY_9) }, { "0",(void*)(KBD|AKEY_0) },
    /* Keyboard: letters */
    { "A",(void*)(KBD|AKEY_a) }, { "B",(void*)(KBD|AKEY_b) },
    { "C",(void*)(KBD|AKEY_c) }, { "D",(void*)(KBD|AKEY_d) },
    { "E",(void*)(KBD|AKEY_e) }, { "F",(void*)(KBD|AKEY_f) },
    { "G",(void*)(KBD|AKEY_g) }, { "H",(void*)(KBD|AKEY_h) },
    { "I",(void*)(KBD|AKEY_i) }, { "J",(void*)(KBD|AKEY_j) },
    { "K",(void*)(KBD|AKEY_k) }, { "L",(void*)(KBD|AKEY_l) },
    { "M",(void*)(KBD|AKEY_m) }, { "N",(void*)(KBD|AKEY_n) },
    { "O",(void*)(KBD|AKEY_o) }, { "P",(void*)(KBD|AKEY_p) },
    { "Q",(void*)(KBD|AKEY_q) }, { "R",(void*)(KBD|AKEY_r) },
    { "S",(void*)(KBD|AKEY_s) }, { "T",(void*)(KBD|AKEY_t) },
    { "U",(void*)(KBD|AKEY_u) }, { "V",(void*)(KBD|AKEY_v) },
    { "W",(void*)(KBD|AKEY_w) }, { "X",(void*)(KBD|AKEY_x) },
    { "Y",(void*)(KBD|AKEY_y) }, { "Z",(void*)(KBD|AKEY_z) },
    /* Keyboard: symbols */
    {"< (less than)",   (void*)(KBD|AKEY_LESS) },
    {"> (greater than)",(void*)(KBD|AKEY_GREATER) },
    {"= (equals)",      (void*)(KBD|AKEY_EQUAL) },
    {"+ (plus)",        (void*)(KBD|AKEY_PLUS) },
    {"* (asterisk)",    (void*)(KBD|AKEY_ASTERISK) },
    {"/ (slash)",       (void*)(KBD|AKEY_SLASH) },
    {": (colon)",       (void*)(KBD|AKEY_COLON) },
    {"; (semicolon)",   (void*)(KBD|AKEY_SEMICOLON) },
    {", (comma)",       (void*)(KBD|AKEY_COMMA) }, 
    {". (period)",      (void*)(KBD|AKEY_FULLSTOP) },
    {"_ (underscore)",  (void*)(KBD|AKEY_UNDERSCORE) },
    /* End */
    { NULL, NULL } },
  ConsoleButtonMapOptions[] = {
    /* Unmapped */
    { "None", (void*)0 },
    /* Special keys */
    { "Special: Open Menu", (void*)(SPC|-AKEY_EXIT)  },
    /* Console */
    { "Console: Start", (void*)(KBD|AKEY_5200_START) },
    { "Console: Pause", (void*)(KBD|AKEY_5200_PAUSE) },
    { "Console: Reset", (void*)(KBD|AKEY_5200_RESET) },
    /* Joystick */
    { "Joystick: Up",   (void*)(JOY|STICK_FORWARD) },
    { "Joystick: Down", (void*)(JOY|STICK_BACK)    },
    { "Joystick: Left", (void*)(JOY|STICK_LEFT)    },
    { "Joystick: Up",   (void*)(JOY|STICK_RIGHT)   },
    { "Joystick: Fire", (void*)(TRG|0) },
    /* Keypad */
    { "1",(void*)(KBD|AKEY_5200_1) }, { "2",(void*)(KBD|AKEY_5200_2) },
    { "3",(void*)(KBD|AKEY_5200_3) }, { "4",(void*)(KBD|AKEY_5200_4) },
    { "5",(void*)(KBD|AKEY_5200_5) }, { "6",(void*)(KBD|AKEY_5200_6) },
    { "7",(void*)(KBD|AKEY_5200_7) }, { "8",(void*)(KBD|AKEY_5200_8) },
    { "9",(void*)(KBD|AKEY_5200_9) }, { "0",(void*)(KBD|AKEY_5200_0) },
    { "*",(void*)(KBD|AKEY_5200_ASTERISK) },
    { "#",(void*)(KBD|AKEY_5200_HASH) },
    /* End */
    { NULL, NULL } };

/* Menu definitions */
static const PspMenuItemDef
  OptionMenuDef[] = {
    { "\tVideo", 0, NULL, -1, NULL },
    { "Screen size", OPTION_DISPLAY_MODE, ScreenSizeOptions, -1, 
      "\026\250\020 Change screen size" },
    { "\tPerformance", 0, NULL, -1, NULL },
    { "Frame skipping", OPTION_FRAMESKIP, FrameSkipOptions, -1, 
      "\026\250\020 Change number of frames skipped per update" },
    { "VSync", OPTION_VSYNC, ToggleOptions, -1, 
      "\026\250\020 Enable to reduce tearing; disable to increase speed" },
    { "PSP clock frequency", OPTION_CLOCK_FREQ, PspClockFreqOptions, -1, 
      "\026\250\020 Larger values: faster emulation, faster battery depletion (default: 222MHz)" },
    { "Show FPS counter", OPTION_SHOW_FPS, ToggleOptions, -1,
      "\026\250\020 Show/hide the frames-per-second counter" },
    { "\tMenu", 0, NULL, -1, NULL },
    { "Button mode", OPTION_CONTROL_MODE, ControlModeOptions,  -1,
      "\026\250\020 Change OK and Cancel button mapping" },
    { NULL, 0 }
  },
  ComputerControlMenuDef[] = {
    { "\026"PSP_CHAR_ANALUP,   MAP_ANALOG_UP,     ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_ANALDOWN, MAP_ANALOG_DOWN,   ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_ANALLEFT, MAP_ANALOG_LEFT,   ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_ANALRIGHT,MAP_ANALOG_RIGHT,  ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_UP,       MAP_BUTTON_UP,     ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_DOWN,     MAP_BUTTON_DOWN,   ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_LEFT,     MAP_BUTTON_LEFT,   ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_RIGHT,    MAP_BUTTON_RIGHT,  ComputerButtonMapOptions, -1, 
      ControlHelpText },
    { "\026"PSP_CHAR_SQUARE,   MAP_BUTTON_SQUARE, ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_CROSS,    MAP_BUTTON_CROSS,  ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_CIRCLE,   MAP_BUTTON_CIRCLE, ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_TRIANGLE, MAP_BUTTON_TRIANGLE,ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER, MAP_BUTTON_LTRIGGER,ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_RTRIGGER, MAP_BUTTON_RTRIGGER,ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_SELECT,   MAP_BUTTON_SELECT,  ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_START,    MAP_BUTTON_START,   ComputerButtonMapOptions, -1,
      ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER,
      MAP_BUTTON_LRTRIGGERS, ComputerButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_START"+"PSP_CHAR_SELECT,
      MAP_BUTTON_STARTSELECT,ComputerButtonMapOptions, -1, ControlHelpText },
    { NULL, 0 }
  },
  ConsoleControlMenuDef[] = {
    { "\026"PSP_CHAR_ANALUP,     MAP_ANALOG_UP,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALDOWN,   MAP_ANALOG_DOWN,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALLEFT,   MAP_ANALOG_LEFT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALRIGHT,  MAP_ANALOG_RIGHT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_UP,         MAP_BUTTON_UP,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_DOWN,       MAP_BUTTON_DOWN,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LEFT,       MAP_BUTTON_LEFT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_RIGHT,      MAP_BUTTON_RIGHT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_SQUARE,     MAP_BUTTON_SQUARE,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_CROSS,      MAP_BUTTON_CROSS,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_CIRCLE,     MAP_BUTTON_CIRCLE,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_TRIANGLE,   MAP_BUTTON_TRIANGLE,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER,   MAP_BUTTON_LTRIGGER,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_RTRIGGER,   MAP_BUTTON_RTRIGGER,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_SELECT,     MAP_BUTTON_SELECT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_START,      MAP_BUTTON_START,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER, MAP_BUTTON_LRTRIGGERS,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_START"+"PSP_CHAR_SELECT, MAP_BUTTON_STARTSELECT,
      ConsoleButtonMapOptions, -1, ControlHelpText },
    { NULL, 0 }
  },
  SystemMenuDef[] = {
    { "\tHardware", 0, NULL, -1, NULL },
    { "TV frequency",     SYSTEM_TV_MODE, TVModeOptions, -1, 
      "\026\250\020 Change emulated machine" },
    { "Machine type",     SYSTEM_MACHINE_TYPE, MachineTypeOptions, -1, 
      "\026\250\020 Change emulated machine" },
    { "\tSystem", 0, NULL, -1, NULL },
    { "Reset", SYSTEM_RESET, NULL, -1, "\026\001\020 Reset" },
    { "Save screenshot",  SYSTEM_SCRNSHOT, NULL, -1,
      "\026\001\020 Save screenshot" },
    { NULL, 0 }
  };

/* Function declarations */
static void InitGameConfig();
static int LoadGameConfig();
static int SaveGameConfig();

static void LoadOptions();
static int  SaveOptions();
static void InitOptionDefaults();

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
  UiMetric.DialogBorderColor = PSP_COLOR_GRAY;
  UiMetric.DialogBgColor = PSP_COLOR_DARKGRAY;
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
  UiMetric.DialogFogColor = COLOR(0, 0, 0, 88);
  UiMetric.TitlePadding = 4;
  UiMetric.TitleColor = PSP_COLOR_WHITE;
  UiMetric.MenuFps = 30;
  UiMetric.TabBgColor = COLOR(0x74,0x74,0xbe,0xff);

  /* Reset variables */
  TabIndex = TAB_ABOUT;
  Background = NULL;
  NoSaveIcon = NULL;
  LoadedGame = NULL;
  GamePath = NULL;

  /* Initialize options */
  LoadOptions();

  /* Initialize emulation */
  if (!InitEmulation()) return;

  /* Load the background image */
  Background = pspImageLoadPng("background.png");
  UiMetric.Background = Background;

  /* Init NoSaveState icon image */
  NoSaveIcon=pspImageCreate(168, 120, PSP_IMAGE_16BPP);
  pspImageClear(NoSaveIcon, RGB(0x0c,0,0x3f));

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

  /* Initialize paths */
  SaveStatePath = (char*)malloc(sizeof(char) 
    * (strlen(pspGetAppDirectory()) + strlen(SaveStateDir) + 2));
  sprintf(SaveStatePath, "%s%s/", pspGetAppDirectory(), SaveStateDir);
  ScreenshotPath = (char*)malloc(sizeof(char) 
    * (strlen(pspGetAppDirectory()) + strlen(ScreenshotDir) + 2));
  sprintf(ScreenshotPath, "%s%s/", pspGetAppDirectory(), ScreenshotDir);

  /* Load default configuration */
  LoadGameConfig();
}

void DisplayMenu()
{
  int i;
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

		  /* Reinitialize the control menu to reflect console or computer */
		  pspMenuLoad(ControlUiMenu.Menu, 
		    (machine_type == MACHINE_5200) 
		      ? ConsoleControlMenuDef : ComputerControlMenuDef);

      /* Load current button mappings */
      for (item = ControlUiMenu.Menu->First, i = 0; item; item = item->Next, i++)
        pspMenuSelectOptionByValue(item, (void*)ActiveGameConfig.ButtonConfig[i]);
      pspUiOpenMenu(&ControlUiMenu, NULL);

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

      pspUiOpenMenu(&OptionUiMenu, NULL);
      break;
    case TAB_SYSTEM:
      item = pspMenuFindItemById(SystemUiMenu.Menu, SYSTEM_MACHINE_TYPE);
      pspMenuSelectOptionByValue(item, (void*)(M_TYPE(machine_type, ram_size)));
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
      if (ResumeEmulation) RunEmulation();
    }
  } while (!ExitPSP);
}

static void DisplayStateTab()
{
  PspMenuItem *item;
  SceIoStat stat;
  char caption[32];

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
  char *path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) 
    + strlen(OptionsFile) + 1));
  sprintf(path, "%s%s", pspGetAppDirectory(), OptionsFile);

  /* Initialize INI structure */
  PspInit *init = pspInitCreate();

  /* Read the file */
  if (!pspInitLoad(init, path))
  {
    /* File does not exist; load defaults */
    InitOptionDefaults();
  }
  else
  {
    /* Load values */
    Config.DisplayMode = pspInitGetInt(init, "Video", "Display Mode", 
      DISPLAY_MODE_UNSCALED);
    Config.Frameskip = pspInitGetInt(init, "Video", "Frameskip", 1);
    Config.VSync = pspInitGetInt(init, "Video", "VSync", 0);
    Config.ClockFreq = pspInitGetInt(init, "Video", "PSP Clock Frequency", 222);
    Config.ShowFps = pspInitGetInt(init, "Video", "Show FPS", 0);
    Config.ControlMode = pspInitGetInt(init, "Menu", "Control Mode", 0);

    machine_type = pspInitGetInt(init, "System", "Machine Type", MACHINE_XLXE);
    ram_size = pspInitGetInt(init, "System", "Machine Type", 64);
    tv_mode = pspInitGetInt(init, "System", "TV mode", TV_NTSC);

    if (GamePath) free(GamePath);
    GamePath = pspInitGetString(init, "File", "Game Path", NULL);
  }

  /* Clean up */
  free(path);
  pspInitDestroy(init);
}

/* Save options */
static int SaveOptions()
{
  char *path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) 
    + strlen(OptionsFile) + 1));
  sprintf(path, "%s%s", pspGetAppDirectory(), OptionsFile);

  /* Initialize INI structure */
  PspInit *init = pspInitCreate();

  /* Set values */
  pspInitSetInt(init, "Video", "Display Mode", Config.DisplayMode);
  pspInitSetInt(init, "Video", "Frameskip", Config.Frameskip);
  pspInitSetInt(init, "Video", "VSync", Config.VSync);
  pspInitSetInt(init, "Video", "PSP Clock Frequency",Config.ClockFreq);
  pspInitSetInt(init, "Video", "Show FPS", Config.ShowFps);
  pspInitSetInt(init, "Menu",  "Control Mode", Config.ControlMode);

  pspInitSetInt(init, "System", "Machine Type", machine_type);
  pspInitSetInt(init, "System", "RAM Size", ram_size);
  pspInitSetInt(init, "System", "TV mode", tv_mode);

  if (GamePath) pspInitSetString(init, "File", "Game Path", GamePath);

  /* Save INI file */
  int status = pspInitSave(init, path);

  /* Clean up */
  pspInitDestroy(init);
  free(path);

  return status;
}

/* Initialize options to system defaults */
void InitOptionDefaults()
{
  Config.ControlMode = 0;
  Config.DisplayMode = DISPLAY_MODE_UNSCALED;
  Config.Frameskip = 1;
  Config.VSync = 0;
  Config.ClockFreq = 222;
  Config.ShowFps = 0;

  machine_type = MACHINE_XLXE;
  ram_size = 64;
  tv_mode = TV_NTSC;

  GamePath = NULL;
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

int  OnSplashButtonPress(const struct PspUiSplash *splash, 
  u32 button_mask)
{
  return OnGenericButtonPress(NULL, NULL, button_mask);
}

/* Handles drawing of generic items */
void OnGenericRender(const void *uiobject, const void *item_obj)
{
  static char status[128];
  pspUiGetStatusString(status, sizeof(status));

  /* Draw tabs */
  int height = pspFontGetLineHeight(UiMetric.Font);
  int width = pspFontGetTextWidth(UiMetric.Font, status);
  pspVideoPrint(UiMetric.Font, SCR_WIDTH - width, 0, status, PSP_COLOR_WHITE);

  int i, x;
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
    else
      pspUiAlert("ERROR: Not saved");
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
    unsigned int curr_system;

    switch(item->ID)
    {
    case SYSTEM_TV_MODE:
      tv_mode = (int)option->Value;
      break;
    case SYSTEM_MACHINE_TYPE:
      if ((MACHINE((int)option->Value) == machine_type 
        && RAM((int)option->Value) == ram_size)
          || !pspUiConfirm("This will reset the system. Proceed?")) return 0;

      curr_system = M_TYPE(machine_type, ram_size);

      /* Reconfigure machine type & RAM size */
      machine_type = MACHINE((int)option->Value);
      ram_size = RAM((int)option->Value);

      /* Attempt reinitializing the system */
      if (!Atari800_InitialiseMachine())
      {
        pspUiAlert("Cannot switch to selected system - reverting back");
        machine_type = MACHINE(curr_system);
        ram_size = RAM(curr_system);
	      Coldstart();
        return 0;
      }

      Coldstart();
      InitGameConfig();
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
      Config.VSync = (int)option->Value;
      break;
    case OPTION_CLOCK_FREQ:
      Config.ClockFreq = (int)option->Value;
      break;
    case OPTION_SHOW_FPS:
      Config.ShowFps = (int)option->Value;
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
    if (SaveGameConfig()) pspUiAlert("Changes saved");
    else pspUiAlert("ERROR: Changes not saved");
  }
  else if (uimenu == &SystemUiMenu)
  {
    switch (((const PspMenuItem*)sel_item)->ID)
    {
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

int  OnMenuButtonPress(const struct PspUiMenu *uimenu, 
  PspMenuItem* sel_item, 
  u32 button_mask)
{
  if (uimenu == &ControlUiMenu)
  {
    if (button_mask & PSP_CTRL_TRIANGLE)
    {
      PspMenuItem *item;
      int i;

      /* Load default mapping */
      InitGameConfig();

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

int OnSaveStateButtonPress(const PspUiGallery *gallery, 
      PspMenuItem *sel, 
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

PspMenuItemDef dummy_menu[] = {
  { "Item one", 1, NULL, -1, "Item one select" },
  { "Item two", 2, NULL, -1, "Item two select" },
  { "Item three", 3, NULL, -1, "Item 3 select" },
  { "Item 4our", 4, NULL, -1, "Item 4our select" },
  { "Item 2one", 21, NULL, -1, "Item one select" },
  { "Item 2two", 22, NULL, -1, "Item two select" },
  { "Item 2three", 23, NULL, -1, "Item 3 select" },
  { "Item 24our", 24, NULL, -1, "Item 4our select" },
  { "Item 3one", 31, NULL, -1, "Item one select" },
  { "Item 3two", 32, NULL, -1, "Item two select" },
  { "Item 3three", 33, NULL, -1, "Item 3 select" },
  { "Item 34our", 34, NULL, -1, "Item 4our select" },
  { "Item 4one", 41, NULL, -1, "Item one select" },
  { "Item 4two", 42, NULL, -1, "Item two select" },
  { "Item 4three", 43, NULL, -1, "Item 3 select" },
  { "Item 44our", 44, NULL, -1, "Item 4our select" },
  { "Item 5one", 51, NULL, -1, "Item one select" },
  { "Item 5two", 52, NULL, -1, "Item two select" },
  { "Item 5three", 53, NULL, -1, "Item 3 select" },
  { "Item 54our", 54, NULL, -1, "Item 4our select" },
  { "Item 6one", 61, NULL, -1, "Item one select" },
  { "Item 6two", 62, NULL, -1, "Item two select" },
  { "Item 6three", 63, NULL, -1, "Item 3 select" },
  { "Item 64our", 64, NULL, -1, "Item 4our select" },
  { "Item 7one", 71, NULL, -1, "Item one select" },
  { "Item 7two", 72, NULL, -1, "Item two select" },
  { "Item 7three", 73, NULL, -1, "Item 3 select" },
  { "Item 74our", 74, NULL, -1, "Item 4our select" },
  { "Item 8one", 81, NULL, -1, "Item one select" },
  { "Item 8two", 82, NULL, -1, "Item two select" },
  { "Item 8three", 83, NULL, -1, "Item 3 select" },
  { "Item 84our", 84, NULL, -1, "Item 4our select" },
  { NULL, 0 }
};

int OnQuickloadOk(const void *browser, const void *path)
{
/* Dummy menu */
PspMenu *menu = pspMenuCreate();
pspMenuLoad(menu, dummy_menu);
const PspMenuItem *item = pspUiSelect(menu);
char foo[326];
if (item) sprintf(foo,"you selected '%s' (%i)", item->Caption, item->ID);
else sprintf(foo, "you dina select nuttin'!");
pspMenuDestroy(menu);
pspUiAlert(foo);
  /* Load the game */
  if (!Atari800_OpenFile(path, 1, 1, 0))
  { 
    pspUiAlert("Error loading file"); 
    return 0; 
  }

  /* TODO: load proper control set */
  InitGameConfig();

  /* Reset loaded game */
  if (LoadedGame) free(LoadedGame);
  LoadedGame = strdup(path);

  /* Reset current game path */
  if (GamePath) free(GamePath);
  GamePath = pspFileIoGetParentDirectory(LoadedGame);

  /* Return to the emulator */
  ResumeEmulation = 1;
  return 1;
}

/* Initialize game configuration */
static void InitGameConfig()
{
  GameConfig *config = (machine_type == MACHINE_5200) 
    ? &DefaultConsoleConfig : &DefaultComputerConfig;

  memcpy(&ActiveGameConfig, config, sizeof(GameConfig));
}

/* Load game configuration */
static int LoadGameConfig()
{
  char *path;
  if (!(path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) 
    + strlen("controlhack.bin") + 6))))
      return 0;
  sprintf(path, "%s%s.cnf", pspGetAppDirectory(), "controlhack.bin");

  /* Open file for reading */
  FILE *file = fopen(path, "r");
  free(path);

  /* If no configuration, load defaults */
  if (!file)
  {
    InitGameConfig();
    return 1;
  }

  /* Read contents of struct */
  int nread = fread(&ActiveGameConfig, sizeof(GameConfig), 1, file);
  fclose(file);

  if (nread != 1)
  {
    InitGameConfig();
    return 0;
  }

  return 1;
}

/* Save game configuration */
static int SaveGameConfig()
{
  char *path;
  if (!(path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) 
    + strlen("controlhack.bin") + 6))))
      return 0;
  sprintf(path, "%s%s.cnf", pspGetAppDirectory(), "controlhack.bin");

  /* Open file for writing */
  FILE *file = fopen(path, "w");
  free(path);
  if (!file) return 0;

  /* Write contents of struct */
  int nwritten = fwrite(&ActiveGameConfig, sizeof(GameConfig), 1, file);
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

  if (SaveStateGallery.Menu) pspMenuDestroy(SaveStateGallery.Menu);
  if (OptionUiMenu.Menu) pspMenuDestroy(OptionUiMenu.Menu);
  if (ControlUiMenu.Menu) pspMenuDestroy(ControlUiMenu.Menu);
}

