#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "emulate.h"

#include "video.h"
#include "image.h"

extern PspImage *Screen;

void InitMenu(int *argc, char **argv)
{
  if (!InitEmulation(argc, argv))
    return;

  RunEmulation();
  TrashEmulation();
}

