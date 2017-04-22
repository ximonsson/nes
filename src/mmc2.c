#include <nes/cpu.h>
#include <nes/ppu.h>

//static uint8_t latch = 0;

#define CHR_BANK_SIZE 0x1000
#define N_CHR_BANKS 2
static int chr_banks[N_CHR_BANKS][2];
static int n_chr_banks;
static uint8_t* chr;

static uint8_t chr_read (uint16_t address)
{
	return 0;
}

static void chr_write (uint16_t address, uint8_t v)
{

}

#define N_PRG_BANKS 4
static int prg_banks[N_PRG_BANKS];
static int n_prg_banks;
static uint8_t* prg;

static void prg_write (uint16_t address, uint8_t v)
{
	if (address < 0xB000) // PRG ROM bank select ($A000-$AFFF)
	{
		prg_banks[0] = v;
	}
	else if (address < 0xC000) // $FD/0000 bank select ($B000-$BFFF)
	{

	}
	else if (address < 0xD000) // $FE/0000 bank select ($C000-$CFFF)
	{

	}
	else if (address < 0xE000) // $FD/1000 bank select ($D000-$DFFF)
	{

	}
	else if (address < 0xF000) // $FE/1000 bank select ($E000-$EFFF)
	{

	}
	else // Mirroring ($F000-$FFFF)
	{
		if ((v & 1) == 0)
			nes_ppu_set_mirroring (NES_PPU_MIRROR_VERTICAL);
		else
			nes_ppu_set_mirroring (NES_PPU_MIRROR_HORIZONTAL);
	}
}

#define PRG_ROM_BANK_SIZE 0x2000
//#define PRG(address) prg + prg_banks[address / PRG_ROM_BANK_SIZE] * PRG_ROM_BANK_SIZE + address % PRG_ROM_BANK_SIZE
static int prg_read (uint16_t address, uint8_t* v)
{
	if (address >= 0x8000)
	{
		address &= 0x7FFF;
		int bank = address / PRG_ROM_BANK_SIZE;
		int offset = address % PRG_ROM_BANK_SIZE;
		*v = prg[prg_banks[bank] * PRG_ROM_BANK_SIZE + offset];
		//*v = *(PRG (address));
		return 1;
	}
	return 0;
}

static int write (uint16_t address, uint8_t v)
{
	if (address >= 0x8000)
	{
		prg_write (address, v);
		return 1;
	}
	return 0;
}

void nes_mmc2_load (int _n_prg_banks, uint8_t* _prg, int _n_chr_banks, uint8_t* _chr)
{
	n_prg_banks = _n_prg_banks;
	prg         = _prg;
	n_chr_banks = _n_chr_banks;
	chr         = _chr;

	nes_cpu_add_store_handler (&write);
	nes_cpu_add_read_handler (&prg_read);
	nes_ppu_set_chr_writer (chr_write);
	nes_ppu_set_chr_read (chr_read);

	for (int i = 0; i < 3; i ++) // fix last 3 banks
		prg_banks[N_PRG_BANKS - (1 + i)] = n_prg_banks - (1 + i);
}
