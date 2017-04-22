#ifndef NES_MAPPER_H_
#define NES_MAPPER_H_
#include <stdint.h>

void nes_mmc1_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;


void nes_uxrom_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

void nes_mmc3_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

void nes_mmc2_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

#endif // NES_MAPPER_H_

