#include <nes/cpu.h>
#include <nes/ppu.h>

#define CHR_BANK_SIZE 0x1000
#define N_CHR_BANKS 2
static int chr_banks[2][2];
static int n_chr_banks;
static uint8_t* chr;

uint8_t chr_read (uint16_t address)
{
	return 0;
}

void chr_write (uint16_t address, uint8_t v)
{

}

#define PRG_BANK_SIZE 0x2000
#define N_PRG_BANKS 4
static int prg_banks[N_PRG_BANKS];
static int n_prg_banks;
static uint8_t* prg;

void prg_write (uint16_t address, uint8_t v)
{

}

uint8_t prg_read (uint8_t address)
{
	return 0;
}

int write (uint16_t address, uint8_t v)
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
	prg = _prg;
	n_chr_banks = _n_chr_banks;
	chr = _chr;

	nes_cpu_add_store_handler (write);
	nes_ppu_set_chr_writer (chr_write);
	nes_ppu_set_chr_read (chr_read);

	for (int i = 0; i < 3; i ++) // fix last 3 banks
	{
		prg_banks[N_PRG_BANKS - (1 + i)] = n_prg_banks - (1 + i);
	}
}
