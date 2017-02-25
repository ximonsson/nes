#include "nes.h"
#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/io.h"
#include "nes/apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define INES_HEADER_SIZE 16


static uint8_t* prg_rom = 0;
static uint8_t* chr_rom = 0;


/**
 *  Parse an iNES type ROM file.
 */
static int load_ines (FILE *fp)
{
	unsigned char header[INES_HEADER_SIZE];
	if (fread (header, 1, INES_HEADER_SIZE, fp) != INES_HEADER_SIZE)
	{
		fprintf (stderr, "did not get all bytes for ines header\n");
		return 1;
	}
	printf ("%.3s\n", header);

	int ret = 0;
	int trainer_size = ((header[6] & 0x04) >> 2) * 512;
	if (trainer_size)
		fseek (fp, trainer_size, SEEK_CUR);

	// Mapper ---------------------------------------------------------
	int mapper = (header[6] & 0xF0) >> 4 | (header[7] & 0xF0);
	// register mapper ...

	// PPU mirroring --------------------------------------------------
	int mirroring = MIRROR_HORIZONTAL;
	switch (header[6] & 0x9)
	{
	case 0: // horizontal mirroring
		mirroring = MIRROR_HORIZONTAL;
		break;
	case 1: // vertical mirroring
		mirroring = MIRROR_VERTICAL;
		break;
	case 8: // four screen mirroring
	case 9:
		mirroring = MIRROR_FOUR_SCREEN;
		break;
	}
	nes_ppu_set_mirroring (mirroring);

	// read PRG ROM --------------------------------------------------
	int prg_rom_size = header[4] * 16 << 10;
	prg_rom = calloc (prg_rom_size, 1);

	if ((ret = fread (prg_rom, 1, prg_rom_size, fp)) != prg_rom_size)
	{
		fprintf (stderr, "did not get all bytes for PRG ROM\n");
		fprintf (stderr, "expected %d, read %d\n", prg_rom_size, ret);
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

	// PRG RAM --------------------------------------------------
	int prg_ram_size = header[8] == 0 ? 1 : header[8];

	// CHR ROM --------------------------------------------------
	int chr_rom_size = header[5] * 8 << 10;

	if (chr_rom_size == 0)
		chr_rom = malloc (0x2000); // ingen aning
	else
	{
		chr_rom = malloc (chr_rom_size);
		if ((ret = fread (chr_rom, 1, chr_rom_size, fp)) != chr_rom_size)
		{
			fprintf (stderr, "did not get all bytes for CHR ROM\n");
			fprintf (stderr, "expected %d, read %d\n", chr_rom_size, ret);
			return 1;
		}
		// load it to VRAM in PPU
		nes_ppu_load_vram (chr_rom);
	}

	// VERBOSE
	printf (" TV System: %s\n", header[9] & 1 ? "PAL" : "NTSC");
	printf (" PRG ROM size: %.2d x 16KB (= %.2dKB)\n", header[4], header[4] * 16);
	printf (" CHR ROM size: %.2d x  8KB (= %.2dKB)\n", header[5], header[5] * 8);
	printf (" PRG RAM size: %.2d x  8KB\n", prg_ram_size);
	printf (" Trainer: %.3d\n", trainer_size);
	printf (" Mapper: %.3d\n", mapper);

	if (chr_rom_size == 0)
		printf ("CHR RAM is used instead of CHR ROM\n");

	printf ("Mirroring: ");
	switch (mirroring)
	{
		case MIRROR_HORIZONTAL:
			printf ("HORIZONTAL\n");
			break;
		case MIRROR_VERTICAL:
			printf ("VERTICAL\n");
			break;
		case MIRROR_FOUR_SCREEN:
			printf ("FOUR_SCREEN\n");
			break;
	}

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
	nes_cpu_reset ();
	nes_ppu_reset ();
	nes_apu_reset ();
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
