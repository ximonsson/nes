#include "nes/io.h"
#include <stdio.h>

#define N_CONTROLLERS 2

/* controller_states contains status of buttons pressed for each controller port */
static uint8_t controller_states[N_CONTROLLERS] = { 0 };

/* get_indices contains the current index for the button which's state will be returned at next read */
static uint8_t get_indices[N_CONTROLLERS] = { 0 };

/* reload_states contains the reload state for a controller flagged at bit with #port as index */
static uint8_t reload_states = 0;


void print_controller_state (enum nes_io_controller_port port)
{
	printf ("[%d] reload = %s, index = %d, state = ", port, (reload_states >> port) & 1 ? "true" : "false", get_indices[port]);
	for (int i = 0; i < 8; i ++)
		printf ("%d", controller_states[port] >> i & 1);
}


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
	uint8_t* index = get_indices + port;
	if (*index == 8)
	{
		return 1; // all buttons have been read - return 1
	}

	// check reload state
	if ((reload_states >> port) & 1)
	{
		// reload back to index 0
		*index = 0;
	}

	uint8_t ret = (controller_states[port] >> *index) & 1;
	*index += 1;
	return ret;
}


void nes_io_controller_port_write (enum nes_io_controller_port port, uint8_t value)
{
	// set reload state
	if (value & 1)
	{
		reload_states |= 1 << port;
		get_indices[port] = 0;
	}
	else
	{
		// unset reload state
		reload_states &= ~(1 << port);
	}
}
