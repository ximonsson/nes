#include <nes/cpu.h>
#include <nes/ppu.h>

uint8_t* prg;
int n_prg_banks;
uint8_t* chr;
int n_chr_banks;

void nes_cnrom_load (int _n_prg_banks, uint8_t* _prg, int _n_chr_banks, uint8_t* _chr)
{
	prg = _prg;
	n_prg_banks = _n_prg_banks;
	chr = _chr;
	n_chr_banks = _n_chr_banks;
}
