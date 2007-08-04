#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>

#include "menu.h"
#include "emulate.h"

#include "statesav.h"
#include "atari.h"

#include "video.h"
#include "image.h"
#include "ui.h"
#include "ctrl.h"
#include "psp.h"
#include "util.h"
#include "init.h"
#include "fileio.h"

#define TAB_QUICKLOAD 0
/*
#define TAB_CONTROL   2
*/
#define TAB_STATE     1
#define TAB_OPTION    2
#define TAB_SYSTEM    3
#define TAB_ABOUT     4
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

EmulatorOptions Options;

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
/*
  "Controls",
*/
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
  SystemMenuDef[] = {
    { "\tSystem", NULL, NULL, -1, NULL },
    { "Reset",            (void*)SYSTEM_RESET,
      NULL,             -1, "\026\001\020 Reset" },
    { "Save screenshot",  (void*)SYSTEM_SCRNSHOT,
      NULL,             -1, "\026\001\020 Save screenshot" },
    { NULL, NULL }
  };

/* Function declarations */
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

  /* Initialize paths */
  SaveStatePath 
    = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(SaveStateDir) + 2));
  sprintf(SaveStatePath, "%s%s/", pspGetAppDirectory(), SaveStateDir);
  ScreenshotPath 
    = (char*)malloc(sizeof(char) * (strlen(pspGetAppDirectory()) + strlen(ScreenshotDir) + 2));
  sprintf(ScreenshotPath, "%s%s/", pspGetAppDirectory(), ScreenshotDir);

  /* Initialize UI components */
  UiMetric.Background = Background;
  UiMetric.Font = &PspStockFont;
  UiMetric.Left = 8;
  UiMetric.Top = 24;
  UiMetric.Right = 472;
  UiMetric.Bottom = 250;
  UiMetric.OkButton = (!Options.ControlMode) ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
  UiMetric.CancelButton = (!Options.ControlMode) ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
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
  /*
  int i;
  */
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
    case TAB_OPTION:
      /* Init menu options */
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_DISPLAY_MODE);
      pspMenuSelectOptionByValue(item, (void*)Options.DisplayMode);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu,
        (void*)OPTION_SYNC_FREQ);
      pspMenuSelectOptionByValue(item, (void*)Options.UpdateFreq);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_FRAMESKIP);
      pspMenuSelectOptionByValue(item, (void*)(int)Options.Frameskip);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu,
        (void*)OPTION_VSYNC);
      pspMenuSelectOptionByValue(item, (void*)Options.VSync);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_CLOCK_FREQ);
      pspMenuSelectOptionByValue(item, (void*)Options.ClockFreq);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_SHOW_FPS);
      pspMenuSelectOptionByValue(item, (void*)Options.ShowFps);
      item = pspMenuFindItemByUserdata(OptionUiMenu.Menu, 
        (void*)OPTION_CONTROL_MODE);
      pspMenuSelectOptionByValue(item, (void*)Options.ControlMode);

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
      pspSetClockFrequency(Options.ClockFreq);
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
    Options.DisplayMode = pspInitGetInt(init, "Video", "Display Mode", DISPLAY_MODE_UNSCALED);
    Options.UpdateFreq = pspInitGetInt(init, "Video", "Update Frequency", 0);
    Options.Frameskip = pspInitGetInt(init, "Video", "Frameskip", 1);
    Options.VSync = pspInitGetInt(init, "Video", "VSync", 0);
    Options.ClockFreq = pspInitGetInt(init, "Video", "PSP Clock Frequency", 222);
    Options.ShowFps = pspInitGetInt(init, "Video", "Show FPS", 0);
    Options.ControlMode = pspInitGetInt(init, "Menu", "Control Mode", 0);

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
  pspInitSetInt(init, "Video", "Display Mode", Options.DisplayMode);
  pspInitSetInt(init, "Video", "Update Frequency", Options.UpdateFreq);
  pspInitSetInt(init, "Video", "Frameskip", Options.Frameskip);
  pspInitSetInt(init, "Video", "VSync", Options.VSync);
  pspInitSetInt(init, "Video", "PSP Clock Frequency",Options.ClockFreq);
  pspInitSetInt(init, "Video", "Show FPS", Options.ShowFps);
  pspInitSetInt(init, "Menu", "Control Mode", Options.ControlMode);

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
  Options.ControlMode = 0;
  Options.DisplayMode = DISPLAY_MODE_UNSCALED;
  Options.UpdateFreq = 0;
  Options.Frameskip = 1;
  Options.VSync = 0;
  Options.ClockFreq = 222;
  Options.ShowFps = 0;
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
  if (uimenu == &OptionUiMenu)
  {
    switch((int)item->Userdata)
    {
    case OPTION_DISPLAY_MODE:
      Options.DisplayMode = (int)option->Value;
      break;
    case OPTION_SYNC_FREQ:
      Options.UpdateFreq = (int)option->Value;
      break;
    case OPTION_FRAMESKIP:
      Options.Frameskip = (int)option->Value;
      break;
    case OPTION_VSYNC:
      Options.VSync = (int)option->Value;
      break;
    case OPTION_CLOCK_FREQ:
      Options.ClockFreq = (int)option->Value;
      break;
    case OPTION_SHOW_FPS:
      Options.ShowFps = (int)option->Value;
      break;
    case OPTION_CONTROL_MODE:
      Options.ControlMode = (int)option->Value;
      UiMetric.OkButton = (Options.ControlMode) 
        ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
      UiMetric.CancelButton = (Options.ControlMode) 
        ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;
      break;
    }
  }

  return 1;
}

int OnMenuOk(const void *uimenu, const void* sel_item)
{
  const char *game_name;
  
  if (uimenu == &SystemUiMenu)
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
}

