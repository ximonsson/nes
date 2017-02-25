#include "nes/mmc1.h"
#include "nes/cpu.h"
#include <stdio.h>


static uint8_t mmc1_sr;
#define RESET_SR mmc1_sr = 0x10
#define SHIFT_SR(v) mmc1_sr = (mmc1_sr >> 1) | ((v & 1) << 4)

static uint8_t mmc1_ctrl;


/**
 *  Write to control register the value mmc1_sr.
 */
static inline void write_control ()
{
	printf ("writing to MMC1 control x%.2X\n", mmc1_sr);
	mmc1_ctrl = mmc1_sr;
}

/**
 *  Write to CHR Bank 0 register the value mmc1_sr.
 */
static inline void write_chr_bank0 ()
{
	printf ("writing to MMC1 CHR 0 x%.2X\n", mmc1_sr);
}

/**
 *  Write to CHR Bank 1 register the value mmc1_sr.
 */
static inline void write_chr_bank1 ()
{
	printf ("writing to MMC1 CHR 1 x%.2X\n", mmc1_sr);
}

/**
 *  Write to PRG bank register the value mmc1_sr.
 */
static inline void write_prg_bank ()
{
	printf ("writing to MMC1 PRG BANK x%.2X\n", mmc1_sr);
}

/**
 *   Write to shift register.
 */
static int write (uint16_t addr, uint8_t v)
{
	if (addr >= 0x8000)
	{
		if ((v & 0x80) == 0x80) // reset shift register
		{
			RESET_SR;
		}
		else if ((mmc1_sr & 1) == 1) // 5th write - write to correct register
		{
			SHIFT_SR (v);
			switch ((addr >> 13) & 3) // write to correct register
			{
			case 0: // Control $8000-9FFF
				write_control();
				break;
			case 1: // CHR Bank 0 $A000-$BFFF
				write_chr_bank0();
				break;
			case 2: // CHR Bank 1 $C000-$DFFF
				write_chr_bank1();
				break;
			case 3: // PRG Bank $E000-$FFFF
				write_prg_bank();
				break;
			}
			RESET_SR;
		}
		else // shift LSB of v into shift register
		{
			SHIFT_SR (v);
		}
		return 1;
	}
	return 0;
}


static int read (uint16_t addr, uint8_t* v)
{
	if (addr >= 0x8000)
	{
		return 1;
	}
	return 0;
}


void nes_mmc1_load ()
{
	nes_cpu_add_store_handler (&write);
	nes_cpu_add_read_handler (&read);
	RESET_SR;
}
