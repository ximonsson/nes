# nes

NES Emulator

Even though still in development, a lot of games can be played. There are still some bugs to solve.
Currently NROM, SxROM and UxROM games are supported.

## Depedencies

None for the library itself.
To install the test application `libsdl2`, `libpulse` and `libgl` are needed.

## Installation

`make` to create lib and test application.

`make lib` to just create the library.

## TODO

### Bugs

* **Audio**
	* Audio in all games get hacky after a while (~5min) Could be sampling rate?
	* DMC does not seem to work correctly (few games seem to use it so it is unnoticeable, might be the reason for the high pitch noise in *Mega Man II*)

* **Graphics**
	* *Mega Man II* vertical scrolling between levels vs Quickman is horrible.
	* *Teenage Mutant Ninja Turtles* sprite visible during welcome screen.
	* *Zelda* transition from welcome screen is not smooth as in other emulators.

### Future Development

* reading PRG should be handled by mappers
* reading CHR should be handled by mappers
* more mapper support
* battery support (SRAM)
* saving/loading games (outside battery support)
* make sure you can load a new game without exiting binary
* code always needs to be cleaned up somewhere
