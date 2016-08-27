/** -------------------------------------------------------------------------------------
*  File: io.h
*  Author: ximon
*  Description: Interface to the NES controllers.
---------------------------------------------------------------------------------------- */
#ifndef NES_IO_H_
#define NES_IO_H_

#include "nes.h"
#include <stdint.h>

/**
*  Enumerate ports, one for each player.
*/
enum nes_io_controller_port
{
	port_one = 0,
	port_two = 1
};

/**
 * Register key press event for a player.
 */
void nes_io_press_key (enum nes_io_controller_port port, enum controller_keys key) ;

/**
 *  Register key release event for a player.
 */
void nes_io_release_key (enum nes_io_controller_port port, enum controller_keys key) ;

/**
 *  Get the controller state of the selected port.
 */
uint8_t nes_io_controller_port_read (enum nes_io_controller_port port) ;

/**
 *  Write byte to one of the controller ports.
 */
void nes_io_controller_port_write (enum nes_io_controller_port port, uint8_t value) ;

#endif
