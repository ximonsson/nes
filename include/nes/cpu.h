/** -----------------------------------------------------------------------------------
 *  File: cpu.h
 *  Author: ximon
 *  Description: public interface towards cpu to be used within the cpu module.
   ----------------------------------------------------------------------------------- */
#ifndef NES_CPU_H_
#define NES_CPU_H_

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
void nes_cpu_init () ;

/**
 *  Run the CPU and also the game.
 *  This function is blocking until game is done or kill signal has been
 *  sent.
 */
void nes_cpu_start () ;

/**
 *  nes_cpu_step reads one instruction and executes.
 *  Returns the number of CPU cycles run.
 */
int nes_cpu_step () ;

/**
 *  Stop the CPU from exekvation.
 *  This will also stop the entire emulator and game.
 */
void nes_cpu_stop () ;

/**
 *  Toggle pause state of the CPU.
 */
void nes_cpu_pause () ;

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
 *  Set the parameter p to a specific location in NES memory.
 *  Use with caution?
 */
void __memory__ (void **p, int location) ;

#endif
