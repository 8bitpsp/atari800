/*
 * atari_psp.c - Sony PlayStation Portable port code
 *
 * Copyright (c) 2007 Akop Karapetyan
 * Copyright (c) 2005 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>

#include "pl_snd.h"
#include "video.h"
#include "pl_psp.h"
#include "ctrl.h"

#include "psp/menu.h"

PSP_MODULE_INFO(PSP_APP_NAME, 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

static void ExitCallback(void* arg)
{
  ExitPSP = 1;
}

int main(int argc, char **argv)
{
  /* Initialize PSP */
  pl_psp_init(argv[0]);
  pspCtrlInit();
  pspVideoInit();
  pl_snd_init(1024, 0);

  /* Initialize callbacks */
  pl_psp_register_callback(PSP_EXIT_CALLBACK, ExitCallback, NULL);
  pl_psp_start_callback_thread();

  /* Show the menu */
  InitMenu();
  DisplayMenu();
  TrashMenu();

  /* Release PSP resources */
  pl_snd_shutdown();
  pspVideoShutdown();
  pl_psp_shutdown();

  return(0);
}
