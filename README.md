# nes

NES Emulator

Even though still in development, a lot of games can be played. There are still some bugs to solve.

Currently supported mappers are:
* NROM
* SxROM
* UxROM

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
	* DMC does not seem to work correctly (few games seem to use it so it is unnoticeable, might be the reason for the high pitch noise in *Mega Man II* and maybe some of the graphic glitches as the IRQ might not be fired correctly)

* **Graphics**
	* *Mega Man II* vertical scrolling between levels vs Quickman is horrible.
	* *Teenage Mutant Ninja Turtles* sprite visible during welcome screen.
	* *Zelda* transition from welcome screen is not smooth as in other emulators.
	* There is a problem with BG/sprite priorities in *SMB3*

* **Mappers**
	* MMC3 - IRQ timeout is not right. Need to find a more correct way of detecting A12 low

### Future Development

* more mapper support
* battery support (SRAM)
* saving/loading game states
* make sure you can load a new game without exiting binary (needs better reset support)
* code always needs to be cleaned up somewhere (for example, I should really use better variable names for the PPU implementation)
