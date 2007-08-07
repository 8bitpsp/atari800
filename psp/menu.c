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
#define OPTION_SYNC_FREQ    2
#define OPTION_FRAMESKIP    3
#define OPTION_VSYNC        4
#define OPTION_CLOCK_FREQ   5
#define OPTION_SHOW_FPS     6
#define OPTION_CONTROL_MODE 7

#define SYSTEM_SCRNSHOT     1
#define SYSTEM_RESET        2

EmulatorConfig Config;
GameConfig ActiveGameConfig;

/* Default configuration */
GameConfig DefaultGameConfig =
{
  {
    0,     /* Analog Up    */
    0,   /* Analog Down  */
    0,   /* Analog Left  */
    0,  /* Analog Right */
    0,     /* D-pad Up     */
    0,   /* D-pad Down   */
    0,   /* D-pad Left   */
    0,  /* D-pad Right  */
    0,/* Square       */
    0,/* Cross        */
    0,                /* Circle       */
    0,                /* Triangle     */
    0,                /* L Trigger    */
    0,                /* R Trigger    */
    0,                /* Select       */
    0,                /* Start        */
    SPC|-AKEY_EXIT,    /* L+R Triggers */
    0,                /* Start+Select */
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
  FrameLimitOptions[] = {
    { "Disabled", (void*)0 },
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
  ButtonMapOptions[] = {
    /* Unmapped */
    { "None", (void*)0 },
    /* Special keys */
    { "Special: Open Menu",   (void*)(SPC|-AKEY_EXIT)      },
    { "Special: Warm Reboot", (void*)(SPC|-AKEY_WARMSTART) },
    { "Special: Cold Reboot", (void*)(SPC|-AKEY_COLDSTART) },
    { "Special: Start",       (void*)(SPC|-AKEY_START)     },
    { "Special: Select",      (void*)(SPC|-AKEY_SELECT)    },
    { "Special: Option",      (void*)(SPC|-AKEY_OPTION)    },
    /* Keyboard */
    { "Keyboard Up",   (void*)(KBD|AKEY_UP)    }, 
    { "Keyboard Down", (void*)(KBD|AKEY_DOWN)  },
    { "Keyboard Left", (void*)(KBD|AKEY_LEFT)  }, 
    { "Keyboard Right",(void*)(KBD|AKEY_RIGHT) },
    /* Keyboard: digits */
    { "1",(void*)(KBD|AKEY_1) }, { "2",(void*)(KBD|AKEY_2) },
    { "3",(void*)(KBD|AKEY_3) }, { "4",(void*)(KBD|AKEY_4) },
    { "5",(void*)(KBD|AKEY_5) }, { "6",(void*)(KBD|AKEY_6) },
    { "7",(void*)(KBD|AKEY_7) }, { "8",(void*)(KBD|AKEY_8) },
    { "9",(void*)(KBD|AKEY_9) }, { "0",(void*)(KBD|AKEY_0) },
    /* Keyboard: letters */
    { "A",(void*)(KBD|AKEY_A) }, { "B",(void*)(KBD|AKEY_B) },
    { "C",(void*)(KBD|AKEY_C) }, { "D",(void*)(KBD|AKEY_D) },
    { "E",(void*)(KBD|AKEY_E) }, { "F",(void*)(KBD|AKEY_F) },
    { "G",(void*)(KBD|AKEY_G) }, { "H",(void*)(KBD|AKEY_H) },
    { "I",(void*)(KBD|AKEY_I) }, { "J",(void*)(KBD|AKEY_J) },
    { "K",(void*)(KBD|AKEY_K) }, { "L",(void*)(KBD|AKEY_L) },
    { "M",(void*)(KBD|AKEY_M) }, { "N",(void*)(KBD|AKEY_N) },
    { "O",(void*)(KBD|AKEY_O) }, { "P",(void*)(KBD|AKEY_P) },
    { "Q",(void*)(KBD|AKEY_Q) }, { "R",(void*)(KBD|AKEY_R) },
    { "S",(void*)(KBD|AKEY_S) }, { "T",(void*)(KBD|AKEY_T) },
    { "U",(void*)(KBD|AKEY_U) }, { "V",(void*)(KBD|AKEY_V) },
    { "W",(void*)(KBD|AKEY_W) }, { "X",(void*)(KBD|AKEY_X) },
    { "Y",(void*)(KBD|AKEY_Y) }, { "Z",(void*)(KBD|AKEY_Z) },
    /* End */
    { NULL, NULL } };

/* Menu definitions */
static const PspMenuItemDef
  OptionMenuDef[] = {
    { "\tVideo", NULL, NULL, -1, NULL },
    { "Screen size",         (void*)OPTION_DISPLAY_MODE, 
      ScreenSizeOptions,   -1, "\026\250\020 Change screen size" },
    { "\tPerformance", NULL, NULL, -1, NULL },
    { "Frame limiter",       (void*)OPTION_SYNC_FREQ, 
      FrameLimitOptions,   -1, "\026\250\020 Change screen update frequency" },
    { "Frame skipping",      (void*)OPTION_FRAMESKIP,
      FrameSkipOptions,    -1, "\026\250\020 Change number of frames skipped per update" },
    { "VSync",               (void*)OPTION_VSYNC,
      ToggleOptions,       -1, "\026\250\020 Enable to reduce tearing; disable to increase speed" },
    { "PSP clock frequency", (void*)OPTION_CLOCK_FREQ,
      PspClockFreqOptions, -1, 
      "\026\250\020 Larger values: faster emulation, faster battery depletion (default: 222MHz)" },
    { "Show FPS counter",    (void*)OPTION_SHOW_FPS,
      ToggleOptions,       -1, "\026\250\020 Show/hide the frames-per-second counter" },
    { "\tMenu", NULL, NULL, -1, NULL },
    { "Button mode",        (void*)OPTION_CONTROL_MODE,
      ControlModeOptions,  -1, "\026\250\020 Change OK and Cancel button mapping" },
    { NULL, NULL }
  },
  ControlMenuDef[] = {
    { "\026"PSP_CHAR_ANALUP,     (void*)MAP_ANALOG_UP,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALDOWN,   (void*)MAP_ANALOG_DOWN,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALLEFT,   (void*)MAP_ANALOG_LEFT,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_ANALRIGHT,  (void*)MAP_ANALOG_RIGHT,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_UP,         (void*)MAP_BUTTON_UP,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_DOWN,       (void*)MAP_BUTTON_DOWN,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LEFT,       (void*)MAP_BUTTON_LEFT,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_RIGHT,      (void*)MAP_BUTTON_RIGHT,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_SQUARE,     (void*)MAP_BUTTON_SQUARE,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_CROSS,      (void*)MAP_BUTTON_CROSS,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_CIRCLE,     (void*)MAP_BUTTON_CIRCLE,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_TRIANGLE,   (void*)MAP_BUTTON_TRIANGLE,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER,   (void*)MAP_BUTTON_LTRIGGER,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_RTRIGGER,   (void*)MAP_BUTTON_RTRIGGER,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_SELECT,     (void*)MAP_BUTTON_SELECT,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_START,      (void*)MAP_BUTTON_START,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_LTRIGGER"+"PSP_CHAR_RTRIGGER,
                           (void*)MAP_BUTTON_LRTRIGGERS,
      ButtonMapOptions, -1, ControlHelpText },
    { "\026"PSP_CHAR_START"+"PSP_CHAR_SELECT,
                           (void*)MAP_BUTTON_STARTSELECT,
      ButtonMapOptions, -1, ControlHelpText },
    { NULL, NULL }
  },
  SystemMenuDef[] = {
    { "\tSystem", NULL, NULL, -1, NULL },
    { "Reset",            (void*)SYSTEM_RESET,
      NULL,             -1, "\026\001\020 Reset" },
    { "Save screenshot",  (void*)SYSTEM_SCRNSHOT,
      NULL,             -1, "\026\001\020 Save screenshot" },
    { NULL, NULL }
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

void InitMenu(int *argc, char **argv)
{
  /* Reset variables */
  TabIndex = TAB_ABOUT;
  Background = NULL;
  NoSaveIcon = NULL;
  LoadedGame = NULL;
  GamePath = NULL;

  /* Initialize options */
  LoadOptions();

  if (!InitEmulation(argc, argv))
    return;

  /* Load the background image */
  Background = pspImageLoadPng("background.png");

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
    item = pspMenuAppendItem(SaveStateGallery.Menu, NULL, (void*)i);
    pspMenuSetHelpText(item, EmptySlotText);
  }

  /* Initialize system menu */
  SystemUiMenu.Menu = pspMenuCreate();
  pspMenuLoad(SystemUiMenu.Menu, SystemMenuDef);

  /* Initialize control menu */
  ControlUiMenu.Menu = pspMenuCreate();
  pspMenuLoad(ControlUiMenu.Menu, ControlMenuDef);

  /* Initialize paths */
  SaveStatePath 
    = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(SaveStateDir) + 2));
  sprintf(SaveStatePath, "%s%s/", pspGetAppDirectory(), SaveStateDir);
  ScreenshotPath 
    = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(ScreenshotDir) + 2));
  sprintf(ScreenshotPath, "%s%s/", pspGetAppDirectory(), ScreenshotDir);

  /* Load default configuration */
  LoadGameConfig();

  /* Initialize UI components */
  UiMetric.Background = Background;
  UiMetric.Font = &PspStockFont;
  UiMetric.Left = 8;
  UiMetric.Top = 24;
  UiMetric.Right = 472;
  UiMetric.Bottom = 250;
  UiMetric.OkButton = (!Config.ControlMode) ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
  UiMetric.CancelButton = (!Config.ControlMode) ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
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
      /* Load current button mappings */
      for (item = ControlUiMenu.Menu->First, i = 0; item; item = item->Next, i++)
        pspMenuSelectOptionByValue(item, (void*)ActiveGameConfig.ButtonConfig[i]);
      pspUiOpenMenu(&ControlUiMenu, NULL);
      break;
    case TAB_OPTION:
      /* Init menu options */
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_DISPLAY_MODE);
      pspMenuSelectOptionByValue(item, (void*)Config.DisplayMode);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu,
        (void*)OPTION_SYNC_FREQ);
      pspMenuSelectOptionByValue(item, (void*)Config.UpdateFreq);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_FRAMESKIP);
      pspMenuSelectOptionByValue(item, (void*)(int)Config.Frameskip);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu,
        (void*)OPTION_VSYNC);
      pspMenuSelectOptionByValue(item, (void*)Config.VSync);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_CLOCK_FREQ);
      pspMenuSelectOptionByValue(item, (void*)Config.ClockFreq);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_SHOW_FPS);
      pspMenuSelectOptionByValue(item, (void*)Config.ShowFps);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_CONTROL_MODE);
      pspMenuSelectOptionByValue(item, (void*)Config.ControlMode);

      pspUiOpenMenu(&OptionUiMenu, NULL);
      break;
    case TAB_SYSTEM:
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
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name,
      (int)item->Userdata);

    if (pspFileIoCheckIfExists(path))
    {
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
  char *path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(OptionsFile) + 1));
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
    Config.DisplayMode = pspInitGetInt(init, "Video", "Display Mode", DISPLAY_MODE_UNSCALED);
    Config.UpdateFreq = pspInitGetInt(init, "Video", "Update Frequency", 0);
    Config.Frameskip = pspInitGetInt(init, "Video", "Frameskip", 1);
    Config.VSync = pspInitGetInt(init, "Video", "VSync", 0);
    Config.ClockFreq = pspInitGetInt(init, "Video", "PSP Clock Frequency", 222);
    Config.ShowFps = pspInitGetInt(init, "Video", "Show FPS", 0);
    Config.ControlMode = pspInitGetInt(init, "Menu", "Control Mode", 0);

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
  char *path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(OptionsFile) + 1));
  sprintf(path, "%s%s", pspGetAppDirectory(), OptionsFile);

  /* Initialize INI structure */
  PspInit *init = pspInitCreate();

  /* Set values */
  pspInitSetInt(init, "Video", "Display Mode", Config.DisplayMode);
  pspInitSetInt(init, "Video", "Update Frequency", Config.UpdateFreq);
  pspInitSetInt(init, "Video", "Frameskip", Config.Frameskip);
  pspInitSetInt(init, "Video", "VSync", Config.VSync);
  pspInitSetInt(init, "Video", "PSP Clock Frequency",Config.ClockFreq);
  pspInitSetInt(init, "Video", "Show FPS", Config.ShowFps);
  pspInitSetInt(init, "Menu", "Control Mode", Config.ControlMode);

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
  Config.UpdateFreq = 0;
  Config.Frameskip = 1;
  Config.VSync = 0;
  Config.ClockFreq = 222;
  Config.ShowFps = 0;
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

int  OnMenuItemChanged(const struct PspUiMenu *uimenu, 
  PspMenuItem* item, const PspMenuOption* option)
{
  if (uimenu == &ControlUiMenu)
  {
    ActiveGameConfig.ButtonConfig[(int)item->Userdata] 
      = (unsigned int)option->Value;
  }
  else if (uimenu == &OptionUiMenu)
  {
    switch((int)item->Userdata)
    {
    case OPTION_DISPLAY_MODE:
      Config.DisplayMode = (int)option->Value;
      break;
    case OPTION_SYNC_FREQ:
      Config.UpdateFreq = (int)option->Value;
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
    if (SaveGameConfig())
      pspUiAlert("Changes saved");
    else
      pspUiAlert("ERROR: Changes not saved");
  }
  else if (uimenu == &SystemUiMenu)
  {
    switch ((int)((const PspMenuItem*)sel_item)->Userdata)
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
      else
        pspUiAlert("Screenshot saved successfully");
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
        pspMenuSelectOptionByValue(item, (void*)DefaultGameConfig.ButtonConfig[i]);

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
    (int)((const PspMenuItem*)item)->Userdata);

  if (pspFileIoCheckIfExists(path) && pspUiConfirm("Load state?"))
  {
    if (LoadState(path))
    {
      ResumeEmulation = 1;
      pspMenuFindItemByUserdata(((const PspUiGallery*)gallery)->Menu,
        ((const PspMenuItem*)item)->Userdata);
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
  if (button_mask & PSP_CTRL_SQUARE 
    || button_mask & PSP_CTRL_TRIANGLE)
  {
    char *path;
    char caption[32];
	  const char *config_name;
	  config_name = (LoadedGame) ? pspFileIoGetFilename(LoadedGame) : NoCartName;
    PspMenuItem *item = pspMenuFindItemByUserdata(gallery->Menu, sel->Userdata);

    path = (char*)malloc(strlen(SaveStatePath) + strlen(config_name) + 8);
    sprintf(path, "%s%s.s%02i", SaveStatePath, config_name, (int)item->Userdata);

    do /* not a real loop; flow control construct */
    {
      if (button_mask & PSP_CTRL_SQUARE)
      {
        if (pspFileIoCheckIfExists(path) && !pspUiConfirm("Overwrite existing state?"))
          break;

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

int OnQuickloadOk(const void *browser, const void *path)
{
  if (LoadedGame) free(LoadedGame);

  if (!Atari800_OpenFile(path, 1, 1, 0))
  { 
    pspUiAlert("Error loading file"); 
    return 0; 
  }

  LoadedGame = strdup(path);

  if (GamePath) free(GamePath);
  GamePath = pspFileIoGetParentDirectory(LoadedGame);

  ResumeEmulation = 1;
  return 1;
}

/* Initialize game configuration */
static void InitGameConfig()
{
  memcpy(&ActiveGameConfig, &DefaultGameConfig, sizeof(GameConfig));
}

/* Load game configuration */
static int LoadGameConfig()
{
  char *path;
  if (!(path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen("controlhack.bin") + 6))))
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
  if (!(path = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen("controlhack.bin") + 6))))
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

