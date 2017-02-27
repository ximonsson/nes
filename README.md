# nes

NES Emulator

## Depedencies

To install the test application `libsdl2`, `libpulse` and `libgl` are needed.

## Installation

`make` to create lib and test application.

`make lib` to just create the library.

## TODO

* audio bug (Super Mario Bros. if fireworks go off at end of level, sound effects are gone, probably loading of DMC samples)
* some glitches between nametables during scrolling
* more mapper support
* make sure you can load a new game without exiting binary
* battery support (SRAM)
* saving/loading games (outside battery support)
* code always needs to be cleaned up somewhere
