#ifndef NES_MAPPER_H_
#define NES_MAPPER_H_
#include <stdint.h>

// Mapper 01
void nes_mmc1_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

// Mapper 02
void nes_uxrom_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

// Mapper 03
void nes_cnrom_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

// Mapper 04
void nes_mmc3_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

// Mapper 09
void nes_mmc2_load (
	int /* # prg bank */, uint8_t* /* prg */,
	int /* # chr bank */, uint8_t* /* chr */
) ;

#endif // NES_MAPPER_H_

