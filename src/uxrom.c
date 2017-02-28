#include <stdint.h>
#include <stdio.h>
#include "nes/nes.h"
#include "nes/cpu.h"


static int write (uint16_t addr, uint8_t v)
{
	if (addr >= 0x8000)
	{
		nes_prg_load_bank (v & 0xF, 0);
		return 1;
	}
	return 0;
}

void nes_uxrom_load ()
{
	nes_cpu_add_store_handler (&write);
	nes_prg_load_bank (-1, 1);
}
