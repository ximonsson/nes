/** -----------------------------------------------------------------------------------
*  File: nes.h
*  Author: ximon
*  Description: Public interface for the nes module. Any external resources
*               should only use functions declared here.
-------------------------------------------------------------------------------------- */
#ifndef NES_H_
#define NES_H_

#include <stdint.h>
#include <stdlib.h>

/**
 *  Keys on the NES controller.
 */
typedef enum nes_controller_keys
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
nes_controller_key;

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
 *  Register press event to player's controller.
 */
void nes_press_button (unsigned int player, nes_controller_key key) ;

/**
 *  Register release event to player's controller.
 */
void nes_release_button (unsigned int player, nes_controller_key key) ;

/**
 *  Save the current game as name.
 */
void nes_save_game (const char* name) ;

/**
 *  Load previous game state from the save file at location.
 */
void nes_load_save (const char* location) ;

/**
 *  Get a pointer to a finished rendered frame by the NES.
 */
const uint8_t* nes_screen_buffer () ;

/**
 * nes_audio_set_sample_rate sets the desired sample rate for audio playback */
void nes_audio_set_sample_rate (int /* rate */) ;

/**
 * nes_audio_samples fills buf with samples and sets size to the size in bytes
 * of the samples.
 */
void nes_audio_samples (float* /* buf */, size_t* /* size */) ;

#endif
