
Commodore C64 Emulator for the PicoPad
======================================

Based on code imported from MCUME by Jean-Marc Harvengt
- https://github.com/Jean-MarcHarvengt/MCUME

Which itself was based on Teensy64 by Frank Boesing
- https://github.com/FrankBoesing/Teensy64

Includes reSID 0.16 by Dag Lem
- https://github.com/daglem/reSID

Imported to the PicoLibSDK system and further developed by Vlastimil Babka.

Summary of changes so far:
- Adaptations to functionality provided by PicoLibSDK where possible.
- Removal of MCUME code intended for other hardware.
- Implementation of OSD menu including keyboard emulation and button assignment
  with multiple layouts, saving global and per-game configs to the SD card.
- Fixed some bugs in the VIC emulation that manifested in River Raid.

Usage:

- Being part of PicoLibSDK, the C64.uf2 image includes the SDK's bootloader.

- The emulator looks for games in the C64 folder on the root of the SD card.
  For now only .prg format (and file extension) is supported. To extract from
  a t64 or d64 file I have used the https://style64.org/dirmaster tool.

- Starting the emulator will show a .prg file selection screen. A confirms
  selection, Y reboots to PicoLibSDK's bootloader.

- The emulation starts and shows the C64 screen. If autorun is not enabled,
  pressing B will enter the commands to load and run the selected .prg.
  Otherwise one can interact with BASIC using the OSD keyboard but that's rather
  tedious.

- In the default layout, direction buttons and A emulate joystick 1, B (after
  the initial load sequence) pressing the SPACE key.

- Y opens the OSD menu, which can be navigated up/down and selected row
  toggled by A, and/or adjusted by left/right. Y exits the OSD (sub)menu, B
  on the main screen reboots to the bootloader.

- X on the main screen opens the OSD keyboard which can be navigated to select
  a key. Pressing A exits OSD and emulates a short keypress of the selected
  key. For Shift, C= and Ctrl keys, they are highlighted and sent as a key combo
  with the final key. X or Y cancels the key selection. Note: RESTORE key is not
  yet handled.

- The main menu allows to change volume, enable/disable autorun, showing FPS
  (for LCD refresh and C64 emulation) on the top of the screen, and button
  hints at the bottom. These settings can be saved to the global config
  (C64/_GLOBAL.CFG) to make them persistent.

Button layouts:

- The original C64 had two joysticks and the keyboard. PicoPad has 8 buttons.
  It's easy to emulate one joystick, which is sufficient for many single player
  games, but the games often require pressing some keyboard key (e.g. Space)
  first, or more with various trainer questions. To make this feasible without
  tediously selecting individual keys from the OSD keyboard, the OSD menu
  allows creating up to 10 button layouts and saving them in a per-game config.
  The config uses the same name as the respective .PRG file, but with a .CFG
  extension, and is located in the same subdirectory of C64/.

- In the OSD main menu, it's possible to switch between emulating joystick 1
  and 2, this setting is part of the per-game config.

- The button layouts can be switched in the same menu (using left/right keys).
  Pressing A will open the layout edit submenu.

- In the layout edit submenu, an action can be assigned for every button
  (except Y) by selecting the button and by pressing A. The left and right keys
  switch which layout is edited. Additionally, the currently edited layout can
  be marked as initial. Upon saving the per-game config, this is recorded in
  there.

- In the button assignment submenu, you can choose to assign a joystick action,
  a keyboard keypress (in this case the Shift, C= and Ctrl keys don't combine
  with a "main" key), no action, or an action that switches the current
  layout to a different one.

- The expected usage of this system is that layout 1 contains the main game
  controls, i.e. joystick and one or two keyboard keys. If different keyboard
  keys are needed to start the game (i.e. START/STOP, Y/N for trainer questions,
  F1 etc.) then those can be assigned in layout 2, with one of the buttons
  assigned for switching to layout 1 once the setup phase is over. Further
  layouts can be used for more complex scenarios. Ideally this would avoid the
  need for manual OSD keyboard selection.

TODO:
- handle RESTORE key
- kbd exit with Y, show operation info
- show run hint for B?
