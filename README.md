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

* **Audio** - Sound works great at start of every game but after a while it will get borky.
	* Audio in all games get hacky after a while (~5min) Could be sampling rate?

* **Graphics** - some glitches between nametables during scrolling, vertical scrolling mostly/only.
	* *Mega Man II*

* **Graphics** - sprites are sometimes faulty - either their Y coordinate can be wrong or they are flipped incorrectly over X axis.
	* *Teenage Mutant Ninja Turtles* - Leonardo's sword when facing south
	* *Zelda II* - the explosion when defeating enemies

### Future Development

* reading PRG should be handled by mappers
* reading CHR should be handled by mappers
* more mapper support
* battery support (SRAM)
* saving/loading games (outside battery support)
* make sure you can load a new game without exiting binary
* code always needs to be cleaned up somewhere
