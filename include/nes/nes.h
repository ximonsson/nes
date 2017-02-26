#ifndef NES_NES_H_
#define NES_NES_H_

void nes_prg_load_16k_bank (int /* bank */, int /* lower bank */) ;
void nes_prg_load_32k_bank (int /* bank */) ;
void nes_chr_load_4kb_bank (int /* bank */, int /* lower */) ;
void nes_chr_load_8kb_bank (int /* bank */) ;

#endif
