#include <stdint.h>
#include <stdio.h>
#include "nes/cpu.h"

static uint8_t* prg;
static int n_prg_banks;
static int banks[2];

static int read (uint16_t addr, uint8_t* v)
{
	if (addr >= 0x8000)
	{
		addr &= 0x7FFF;
		*v = prg[banks[addr / 0x4000] * 0x4000 + addr % 0x4000];
		return 1;
	}
	return 0;
}

static int write (uint16_t addr, uint8_t v)
{
	if (addr >= 0x8000)
	{
		banks[0] = v & 0xF;
		return 1;
	}
	return 0;
}

void nes_uxrom_load (int n, uint8_t* _prg, int m, uint8_t* chr)
{
	n_prg_banks = n;
	prg = _prg;
	nes_cpu_add_store_handler (&write);
	nes_cpu_add_read_handler (&read);
	banks[0] = 0; banks[1] = n_prg_banks - 1;
}
