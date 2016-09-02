/** -----------------------------------------------------------------------------------------------------------------
 *  file: apu.h
 *  description: NES APU module
 *  ----------------------------------------------------------------------------------------------------------------- */

#ifndef NES_APU_H_
#define NES_APU_H_

#include <stdint.h>

#define NES_APU_PULSE_1       0x4000
#define NES_APU_PULSE_2       0x4004
#define NES_APU_TRIANGLE      0x4008
#define NES_APU_NOISE         0x400C
#define NES_APU_DMC           0x4010
#define NES_APU_STATUS        0x4015
#define NES_APU_FRAME_COUNTER 0x4017

/**
 * nes_apu_register enumerates the different registers on the APU
 */
typedef enum nes_apu_register
{
	// ...
	PULSE_1,
	PULSE_2,
	TRIANGLE,
	NOISE,
	DMC,
	STATUS,
	FRAME_COUNTER,
}
nes_apu_register;


/**
 *  nes_apu_init initializes the APU.
 *  Returns non zero on failure.
 */
void nes_apu_reset () ;

/**
 *  nes_apu_register_write writes value to the APU's register associated with the given address.
 */
void nes_apu_register_write (uint16_t /* address */, uint8_t /* value */) ;

/**
 *  nes_apu_register_read reads from the APU register associated with the supplied address.
 */
uint8_t nes_apu_register_read (uint16_t /* address */) ;

/**
 *  nes_apu_step performs a tick in the APU
 */
void nes_apu_step () ;

/**
 *  nes_apu_render renders current sound data to buffer.
 */
void nes_apu_render () ;


#endif // NES_APU_H_
