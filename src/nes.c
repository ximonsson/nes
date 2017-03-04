#include "nes.h"
#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/io.h"
#include "nes/apu.h"
#include "nes/mapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


static int load_mapper (int mapper)
{
	switch (mapper)
	{
	case 0: // NROM
		break;
	case 1: // MMC1
		nes_mmc1_load();
		break;
	case 2: // UxROM
		nes_uxrom_load();
		break;
	case 4: // MMC3
		nes_mmc3_load();
	default:
		fprintf (stderr, "mapper (%.3d) not supported\n", mapper);
		return 1;
	}
	return 0;
}

/* PRG ROM */
// TODO i would like the CPU to handle this like the PPU and CHR
static uint8_t* prg_rom = 0;
static uint8_t prg_rom_n_banks = 0;
static uint8_t prg_rom_bank_loaded = 0;

void nes_prg_load_bank (int bank, int upper)
{
	if (bank == -1)
		bank += prg_rom_n_banks;
	prg_rom_bank_loaded = bank;
	nes_cpu_load_prg_rom_bank (prg_rom + bank * NES_PRG_ROM_BANK_SIZE, upper);
}

/* CHR ROM */
static uint8_t* chr_rom = 0;

/* battery_backed flags if the cartridge contains battery packed SRAM */
static int battery_backed = 0;

/* size of iNES file header */
#define INES_HEADER_SIZE 16

/* Print data about the iNES header */
static void print_ines_info (uint8_t header[INES_HEADER_SIZE])
{
	int mapper = (header[6] & 0xF0) >> 4 | (header[7] & 0xF0);

	printf ("%.3s\n", header);
	printf (" TV System:    %s\n", header[9] & 1 ? "PAL" : "NTSC");
	printf (" Mapper:       %.3d\n", mapper);
	printf (" PRG ROM size:  %2d x 16KB (= %3dKB)\n", header[4], header[4] * 16);
	printf (" PRG RAM size:  %2d x  8KB\n", header[8] == 0 ? 1 : header[8]);
	if (header[5] != 0)
		printf (" CHR ROM size:  %2d x  8KB (= %3dKB)\n", header[5], header[5] * 8);
	else
		printf (" CHR RAM is used instead of CHR ROM\n");

	// int trainer_size = ((header[6] & 0x04) >> 2) * 512;
	// printf (" Trainer:       %d\n", trainer_size);

	if ((header[6] & 2) == 2)
		printf ("Battery backed SRAM\n");

	printf (" Mirroring: ");
	switch (header[6] & 0x9)
	{
	case 0:
		printf ("HORIZONTAL\n");
		break;
	case 1:
		printf ("VERTICAL\n");
		break;
	case 8:
	case 9:
		printf ("FOUR SCREEN\n");
		break;
	}
}

/**
 *  Parse an iNES type ROM file.
 */
static int load_ines (FILE *fp)
{
	int ret = 0;
	unsigned char header[INES_HEADER_SIZE];
	if (fread (header, 1, INES_HEADER_SIZE, fp) != INES_HEADER_SIZE)
	{
		fprintf (stderr, "did not get all bytes for ines header\n");
		return 1;
	}

	// check trainer - jump over it if it exists
	int trainer_size = ((header[6] & 0x04) >> 2) * 512;
	if (trainer_size)
		fseek (fp, trainer_size, SEEK_CUR);

	// PPU mirroring --------------------------------------------------
	int mirroring = NES_PPU_MIRROR_HORIZONTAL;
	switch (header[6] & 0x9)
	{
	case 0: // horizontal mirroring
		mirroring = NES_PPU_MIRROR_HORIZONTAL;
		break;
	case 1: // vertical mirroring
		mirroring = NES_PPU_MIRROR_VERTICAL;
		break;
	case 8: // four screen mirroring
	case 9:
		mirroring = NES_PPU_MIRROR_FOUR_SCREEN;
		break;
	}
	nes_ppu_set_mirroring (mirroring);

	// read PRG ROM --------------------------------------------------
	prg_rom_n_banks = header[4];
	int prg_rom_size = prg_rom_n_banks * 16 << 10;
	prg_rom = calloc (prg_rom_size, 1);

	if ((ret = fread (prg_rom, 1, prg_rom_size, fp)) != prg_rom_size)
	{
		fprintf (stderr, "did not get all bytes for PRG ROM\n");
		return 1;
	}
	// load PRG ROM data to memory
	if (prg_rom_size <= NES_PRG_ROM_BANK_SIZE)
	{
		// if data is smaller than one bank we mirror the memory down.
		nes_cpu_load_prg_rom_bank (prg_rom, 0);
		nes_cpu_load_prg_rom_bank (prg_rom, 1);
	}
	else
		nes_cpu_load_prg_rom (prg_rom);

	// cartridge contains battery backed PRG RAM
	battery_backed = (header[6] & 2) == 2;

	// CHR ROM --------------------------------------------------
	int chr_rom_n_banks = header[5];
	int chr_rom_size = chr_rom_n_banks * 8 << 10;

	if (chr_rom_size == 0) // CHR RAM
		chr_rom = calloc (0x2000, 1);
	else
	{
		chr_rom = malloc (chr_rom_size);
		if ((ret = fread (chr_rom, 1, chr_rom_size, fp)) != chr_rom_size)
		{
			fprintf (stderr, "did not get all bytes for CHR ROM\n");
			return 1;
		}
	}
	// load it to VRAM in PPU
	nes_ppu_load_chr_rom (chr_rom);

	// Mapper ---------------------------------------------------------
	int mapper = (header[6] & 0xF0) >> 4 | (header[7] & 0xF0);
	uint8_t zeroes[4] = { 0 };
	if (memcmp (header + 11, zeroes, 4))
	{
		// not all zeroes in end of header
		// we mask the upper 4 bits of the map number in this case.
		mapper &= 0xF;
	}
	// load the mapper - return in case we do not support it
	if (load_mapper (mapper) != 0)
		return 1;

	print_ines_info (header);
	return 0;
}

/**
 *  Open a NES ROM file.
 */
static int load_game (const char* file)
{
	FILE* fp = fopen (file, "rb");
	int ret = 0;
	if (!fp)
	{
		ret = 1;
		goto end;
	}
	ret = load_ines (fp);
	fclose (fp);
end:
	return ret;
}


void nes_stop ()
{
	// cleanup
	free (prg_rom);
	free (chr_rom);
	prg_rom = 0;
}

// keep track of PPU cycles to know when a frame is done
static int ppucc;

int nes_start (const char* file)
{
	// load game
	if (load_game (file) != 0)
		return 1;

	// init hardware
	nes_cpu_reset();
	nes_ppu_reset();
	nes_apu_reset();
	ppucc = 0;

	return 0;
}


void nes_step_frame ()
{
	// number of CPU cycles run during one step
	int cc;
	// run until a frame has been fully rendered
	while (ppucc < PPUCC_PER_SCANLINE * SCANLINES_PER_FRAME)
	{
		cc = nes_cpu_step ();

		// render on PPU
		for (int i = 0; i < cc * 3; i ++)
			nes_ppu_step ();

		// render audio
		for (int i = 0; i < cc; i ++)
			nes_apu_step ();

		ppucc += cc * 3;

		// TODO emulate Hz
	}
	ppucc %= PPUCC_PER_SCANLINE * SCANLINES_PER_FRAME;
}


void nes_press_button (unsigned int player, enum controller_keys key)
{
	nes_io_press_key (player, key);
}


void nes_release_button (unsigned int player, enum controller_keys key)
{
	nes_io_release_key (player, key);
}
