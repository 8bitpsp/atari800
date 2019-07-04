Atari800 PSP
============

Atari 800, 800XL, 130XE and 5200 emulator

&copy; 2007-2009 Akop Karapetyan  
&copy; 1997-2007 Perry McFarlane, Rich Lawrence, Thomas Richter, Radek Sterba, Robert Golias, Petr Stehlik  
&copy; 1995-1997 David Firth

New Features
------------

#### Version 2.1.0.2 Jul 4 2019

[Download](https://github.com/8bitpsp/atari800/releases/tag/v2.1.0.2)
*   Added support for second fire button for Atari 5200 games

Installation
------------

Unzip `atari800.zip` into `/PSP/GAME/` folder on the memory stick.

Controls
--------

During emulation, Atari computers (800, 800XL, 130XE):

| PSP controls                    | Emulated controls            |
| ------------------------------- | ---------------------------- |
| D-pad up/down/left/right        | Keyboard up/down/left/right  |
| Analog stick up/down/left/right | Joystick up/down/left/right  |
| X (cross)                       | Joystick trigger             |
| O (circle)                      | Spacebar                     |
| Select                          | Select console button        |
| Start                           | Start console button         |
| [R]                             | Show virtual keyboard        |
| [L] + [R]                       | Return to the emulator menu  |

During emulation, Atari 5200:

| PSP controls                    | Emulated controls            |
| ------------------------------- | ---------------------------- |
| D-pad up/down/left/right        | Joystick up/down/left/right  |
| Analog stick up/down/left/right | Joystick up/down/left/right  |
| X (cross)                       | Joystick trigger             |
| Select                          | Pause console button         |
| Start                           | Start console button         |
| [R]                             | Show virtual keypad          |
| [L] + [R]                       | Return to the emulator menu  |

When the virtual keyboard/keypad is on:

| PSP controls                    | Function                 |
| ------------------------------- | ------------------------ |
| Directional pad                 | select key               |
| [ ] (square)                    | press key                |
| O (circle)                      | hold down/release key    |
| ^ (triangle)                    | release all held keys    |

Only certain keys can be held down.

By default, button configuration changes are not retained after button mapping is modified. To save changes, press X (cross) to save mapping to memory stick

Compiling
---------

To compile, ensure that [zlib](svn://svn.pspdev.org/psp/trunk/zlib) and [libpng](svn://svn.pspdev.org/psp/trunk/libpng) are installed, and run make:

`make -f Makefile.psp`

Version History
---------------

#### Version 2.1.0.1 (August 11 2009)

*   Emulator updated to version 2.1.0
*   Added ZIP file support
*   PSPLIB updated to latest version: images are now saved under PSP/PHOTOS/ATARI800 PSP; virtual keyboard updated; screenshot previews in file browser

#### 2.0.3.27 (October 09 2007)

*   This release adds the ability to switch or eject disks without resetting the system

#### 2.0.3.26 (September 26 2007)

*   Fixed a serious crash caused by having many long filenames in the same directory
*   Fixed controller configuration loading bug

#### 2.0.3.2 (September 08 2007)

*   Fixed mislabeled ‘Joystick Right’ control (thanks Robert)
*   Updated GUI – menu animations
*   Fixed various bugs dealing with controller configuration
*   Improved state saving/loading screen

#### 2.0.3.1 (August 18 2007)

*   Initial release
