#include "nes/io.h"


#define N_CONTROLLERS 2


static uint8_t controller_states[N_CONTROLLERS] = {0, 0};
static uint8_t get_indices      [N_CONTROLLERS] = {0, 0};
static uint8_t reload_states = 0;


void nes_io_press_key (enum nes_io_controller_port port, enum controller_keys key)
{
	controller_states[port] |= key;
}


void nes_io_release_key (enum nes_io_controller_port port, enum controller_keys key)
{
	controller_states[port] &= ~key;
}


uint8_t nes_io_controller_port_read (enum nes_io_controller_port port)
{
	// check reload state
	if (~reload_states & (1 << port) && get_indices[port] == 8)
		return 1;

	uint8_t ret = (controller_states[port] >> get_indices[port]) & 1;
	get_indices[port] ++;
	return ret;
}


void nes_io_controller_port_write (enum nes_io_controller_port port, uint8_t value)
{
	// set reload state
	if (~reload_states & (1 << port) && value >= 1)
		reload_states |= 1 << port;
	// unset reload state
	else if (reload_states & (1 << port) && (value & 1) == 0)
	{
		reload_states &= ~(1 << port);
		get_indices[port] = 0;
	}
	// else disable controller?
	else
		get_indices[port] = 8;
}
