/** -----------------------------------------------------------------------------------
*  File: nes.h
*  Author: ximon
*  Description: Public interface for the nes module. Any external resources
*               should only use functions declared here.
-------------------------------------------------------------------------------------- */
#ifndef NES_H_
#define NES_H_

#include <stdint.h>

/**
 *  Keys on the NES controller.
 */
typedef enum controller_keys
{
	nes_button_a       = 0x01,
	nes_button_b       = 0x02,
	nes_button_select  = 0x04,
	nes_button_start   = 0x08,
	nes_button_up      = 0x10,
	nes_button_down    = 0x20,
	nes_button_left    = 0x40,
	nes_button_right   = 0x80
}
nes_controller_keys;

/**
 *  Run a NES game.
 *  file parameter points to the file on the filesystem.
 *  This function is blocking.
 */
void nes_run (const char *file) ;

/**
 * nes_start resets the hardware components and loads the game @ filepath
 * but will not start execution.
 * Returns non-zero error code in case there was an error reading the file.
 */
int nes_start (const char* /* file */) ;

/**
 * nes_step_frame executes until an entire frame is rendered and then waits.
 */
void nes_step_frame () ;

/**
 *  Stop the current NES game running.
 */
void nes_stop () ;

/**
 *  Pause the current NES game.
 */
void nes_pause () ;

/**
 *  Register press event to player's controller.
 */
void nes_press_button (unsigned int player, enum controller_keys key) ;

/**
 *  Register release event to player's controller.
 */
void nes_release_button (unsigned int player, enum controller_keys key) ;

/**
 *  Save the current game as name.
 */
void nes_save_game (const char *name) ;

/**
 *  Load previous game state from the save file at location.
 */
void nes_load_save (const char *location) ;

/**
 *  Get a pointer to a finished rendered frame by the NES.
 */
const uint8_t* nes_screen_buffer () ;

#endif
