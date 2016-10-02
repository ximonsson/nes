/** -----------------------------------------------------------------------------------
 *  File: cpu.h
 *  Author: ximon
 *  Description: public interface towards cpu to be used within the cpu module.
   ----------------------------------------------------------------------------------- */
#ifndef NES_CPU_H_
#define NES_CPU_H_

#include <stdint.h>

#define NES_PRG_ROM_SIZE        0x8000
#define NES_PRG_ROM_BANK_SIZE   0x4000
#define NES_CPU_FREQ            1789773

/**
 *  CPU signals
 */
enum nes_cpu_signal
{
	NMI = 0x01,
	IRQ = 0x02,
	RST = 0x04,
};

/**
 *  Init CPU to startup state.
 *  This must be performed before run is called to make sure
 *  the game will run properly.
 */
void nes_cpu_reset () ;

/**
 *  nes_cpu_step reads one instruction and executes.
 *  Returns the number of CPU cycles run.
 */
int nes_cpu_step () ;

/**
*  Load entire PRG ROM from data source.
*  It expects that the data points to a memory location
*  sufficiently large to fill PRG ROM.
*/
void nes_cpu_load_prg_rom (void *data) ;

/**
 *  Load a bank of memory into PRG ROM.
 *  Bank # is either 0 or 1;
 */
void nes_cpu_load_prg_rom_bank (void *data, int bank) ;

/**
 *  Send CPU signal.
 */
void nes_cpu_signal (enum nes_cpu_signal sig) ;

/**
 * nes_cpu_stall stalls the CPU for the supplied number of cycles.
 */
void nes_cpu_stall (int /* cycles */);

/**
 * nes_cpu_read_ram returns the byte @ address in RAM.
 */
uint8_t nes_cpu_read_ram (uint16_t /* address */) ;

/**
 *  Set the parameter p to a specific location in NES memory.
 *  Use with caution?
 */
void __memory__ (void** p, int location) ;

#endif
