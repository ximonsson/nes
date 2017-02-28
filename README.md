# nes

NES Emulator

Even though still in development, a lot of games can be played.
Currently NROM, SxROM and UxROM games are supported, but there are quite a few graphic bugs that need to be rectified.

## Depedencies

None for the library itself.
To install the test application `libsdl2`, `libpulse` and `libgl` are needed.

## Installation

`make` to create lib and test application.

`make lib` to just create the library.

## TODO

### Bugs

* audio bug, probably incorrect loading of DMC samples (Super Mario Bros. if fireworks go off at end of level, sound effects are gone)
* some glitches between nametables during scrolling, vertical scrolling mostly/only. (Mega Man II)
* sprites are sometimes faulty in games using MMC1 - either their Y coordinate can be wrong or they are flipped incorrectly over X axis.

### Future Development

* more mapper support
* battery support (SRAM)
* saving/loading games (outside battery support)
* make sure you can load a new game without exiting binary
* code always needs to be cleaned up somewhere
