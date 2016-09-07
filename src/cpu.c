#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/io.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#define OAM_DMA_REGISTER    0x4014

/* Controllers port memory locations */
#define CTRL_ONE_MEM_LOC    0x4016
#define CTRL_TWO_MEM_LOC    0x4017

/* DIFF_PAGE returns true or false depending on if x and y are on different pages */
#define DIFF_PAGE(x, y) ((x & 0xFF00) != (y & 0xFF00))

/**
*  Processor Status Flags
*/
enum processor_status_flags
{
	CARRY       = 0x01,
	ZERO        = 0x02,
	INTERRUPT   = 0x04,
	DECIMAL     = 0x08,
	// unused   = 0x10
	BREAK       = 0x20,
	OVERFLOW    = 0x40,
	NEGATIVE    = 0x80
};

/**
*  Addressing Modes used by the CPU.
*/
typedef enum addressing_modes
{
	ACCUMULATOR,
	IMMEDIATE,
	RELATIVE,
	ZERO_PAGE,
	ZERO_PAGE_X,
	ZERO_PAGE_Y,
	ABSOLUTE,
	ABSOLUTE_X,
	ABSOLUTE_Y,
	INDIRECT,
	INDEXED_INDIRECT,
	INDIRECT_INDEXED,
	IMPLICIT
}
addressing_mode;

enum status_flags
{
	PAGE_CROSS  = 0x01,
	STOP        = 0x02,
	PAUSE       = 0x04,
};
static int flags = STOP;

// CPU signals
static int signals;


#define MAX_EVENT_HANDLERS 16


/**
 *  CPU registers
 */
// Program Counter
static uint16_t pc;
// X register
static uint8_t  x;
// Y register
static uint8_t  y;
// A register
static uint8_t  a;
// Processor Status
static uint8_t  ps;
// Stack Pointer
static uint8_t  sp;

// CPU Clock Counter
static int cpucc;

// Memory
#define MEMORY_SIZE (65 << 10)
static uint8_t memory[MEMORY_SIZE];


void __memory__ (void **p, int location)
{
	*p = memory + location;
}


#define PRG_RAM_LOCATION 0x6000
#define PRG_ROM_LOCATION 0x8000
/* Load PRG ROM data to bank. */
void nes_cpu_load_prg_rom_bank (void *data, int bank)
{
	memcpy
	(
		memory + PRG_ROM_LOCATION + bank * NES_PRG_ROM_BANK_SIZE,
		data,
		NES_PRG_ROM_BANK_SIZE
	);
}

/* Load data to PRG ROM. */
void nes_cpu_load_prg_rom (void *data)
{
	memcpy
	(
		memory + PRG_ROM_LOCATION,
		data,
		NES_PRG_ROM_SIZE
	);
}


/** ----------------------------------------------------------------------------------------------
 *  ADDRESSING FUNCTIONS
 *  ---------------------------------------------------------------------------------------------- */

/* Zero Page - $00 */
static uint16_t zero_page ()
{
	return memory[pc];
}

/* Zero Page,X - $10,X */
static uint16_t zero_page_x ()
{
	uint8_t ret = memory[pc] + x;
	return ret;
}

/* Zero Page,Y - $10,Y */
static uint16_t zero_page_y ()
{
	uint8_t ret = memory[pc] + y;
	return ret;
}

/* Absolute - $1234 */
static uint16_t absolute ()
{
	uint16_t addr = memory[pc + 1];
	addr = (addr << 8) | memory[pc];
	return addr;
}

/* Absolute,X - $1234,X */
static uint16_t absolute_x ()
{
	uint16_t addr = absolute () + x;
	if (DIFF_PAGE (addr, pc))
		flags |= PAGE_CROSS;
	return addr;
}

/* Absolute,Y - $1234,Y */
static uint16_t absolute_y ()
{
	uint16_t addr = absolute () + y;
	if (DIFF_PAGE (addr, pc))
		flags |= PAGE_CROSS;
	return addr;
}

/* Indirect - ($FFFC) */
static uint16_t indirect ()
{
	uint8_t l = memory[pc];
	uint16_t h = memory[pc + 1];
	h <<= 8;

	uint16_t low = h | l;
	l ++;
	uint16_t high = h | l;

	uint16_t addr = memory[high];
	addr = (addr << 8) | memory[low];

	return addr;
}

/* Indexed Indirect - $(40,X) */
static uint16_t indexed_indirect ()
{
	uint8_t l = memory[pc] + x;
	uint8_t h = l + 1;
	uint16_t addr = memory[h];
	addr = (addr << 8) | memory[l];

	return addr;
}

/* Indirect Indexed - ($40),Y */
static uint16_t indirect_indexed ()
{
	uint8_t l = memory[pc];
	uint8_t h = l + 1;
	uint16_t addr = memory[h];
	addr = ((addr << 8) | memory[l]) + y;

	if (DIFF_PAGE (addr, pc))
		flags |= PAGE_CROSS;

	return addr;
}

/* Accumulator - A */
static uint16_t accumulator ()
{
	return 0;
}

/* Immediate - #10 */
static uint16_t immediate ()
{
	return pc;
}

/* Relative - *+4 */
static uint16_t relative ()
{
	return pc;
}

// Array to address calculating function indexed by their mode.
static uint16_t (*address_calculators[12])() =
{
	&accumulator,
	&immediate,
	&relative,
	&zero_page,
	&zero_page_x,
	&zero_page_y,
	&absolute,
	&absolute_x,
	&absolute_y,
	&indirect,
	&indexed_indirect,
	&indirect_indexed
};

#ifdef VERBOSE
static void accumulator_string (char *s)
{
	*s = 'A';
}

static void zero_page_string (char *s)
{
	sprintf (s, "$%.2X = %.2X", memory[pc], memory[zero_page()]);
}

static void zero_page_x_string (char *s)
{
	sprintf (s, "$%.2X,X = %.2X", memory[pc], memory[zero_page_x()]);
}

static void zero_page_y_string (char *s)
{
	sprintf (s, "$%.2X,Y = %.2X", memory[pc], memory[zero_page_y()]);
}

static void absolute_string (char *s)
{
	uint16_t m = absolute();
	sprintf (s, "$%.4X = %.2X", m, memory[m]);
}

static void absolute_x_string (char *s)
{
	uint16_t m = absolute_x();
	sprintf (s, "$%.4X,X = %.2X", m, memory[m]);
}

static void absolute_y_string (char *s)
{
	uint16_t m = absolute_y();
	sprintf (s, "$%.4X,Y = %.2X", m, memory[m]);
}

static void indirect_string (char *s)
{
	uint16_t m = memory[pc], n = memory[pc + 1];
	m = (n << 8) | m;
	sprintf (s, "($%.4X) = %.4X", m, indirect());
}

static void indexed_indirect_string (char *s)
{
	uint8_t  m = memory[pc];
	uint16_t a = indexed_indirect();
	sprintf (s, "($%.2X,X) @ %.2X = %.4X = %.2X", m, m + x, a, memory[a]);
}

static void indirect_indexed_string (char *s)
{
	sprintf (s, "($%.2X),Y", memory[pc]);
}

static void immediate_string (char *s)
{
	sprintf (s, "#%.2X", memory[pc]);
}

static void implicit_string (char *s)
{
	// nada
}

// Array to address calculating function indexed by their mode.
static void (*address_calculators_string[13])(char *) =
{
	&accumulator_string,
	&immediate_string,
	&immediate_string,
	&zero_page_string,
	&zero_page_x_string,
	&zero_page_y_string,
	&absolute_string,
	&absolute_x_string,
	&absolute_y_string,
	&indirect_string,
	&indexed_indirect_string,
	&indirect_indexed_string,
	&implicit_string
};

#endif // VERBOSE


/**
 *  Calculate new address, number of bytes to progress and if a page cross occurred given
 *  an addressing mode.
 */
static uint16_t calculate_address (addressing_mode mode)
{
	uint16_t addr = (*address_calculators[mode])();
	return addr;
}

/* end ADDRESSING FUNCTIONS ----------------------------------------------------------- */


/* Stack location in memory */
#define STACK_LOCATION 0x0100

/* Push a value on to the stack. */
static void push (uint8_t value)
{
	memory[STACK_LOCATION | sp] = value;
	sp --;
}

/* Pop a value from the stack. */
static uint8_t pop ()
{
	sp ++;
	return memory[STACK_LOCATION | sp];
}

/**
 *  Branch an offset number of bytes.
 *  Returns 1 if to a new page, 0 if on the same.
 */
static void branch (int8_t offset)
{
	uint16_t _pc = pc + offset;
	if (DIFF_PAGE (pc, _pc))
		cpucc ++;
	pc = _pc;
}


/* Store Handlers --------------------------------------------------------------------------------------------------- */

/**
 *  typedef function for store event handler.
 *  takes an address and value we are trying to store at it.
 *  returns 1 or 0 incase we should stop propagation.
 */
typedef int (*store_handler) (uint16_t address, uint8_t value) ;
static store_handler store_handlers[MAX_EVENT_HANDLERS];

/**
 *  Store event handler for when we are modifying PPU registers.
 *  Stops propagation as PPU registers are special.
 */
static int on_ppu_register_write (uint16_t address, uint8_t value)
{
	if (address >= PPU_REGISTER_MEM_LOC && address < PPU_REGISTER_MEM_LOC + 0x2000)
	{
		nes_ppu_register_write (address & 7, value);
		return 1;
	}
	return 0;
}

/**
 *  Store event handler for when writing to OAM_DMA register.
 *  Stops propagation as it is not needed to be stored in RAM.
 */
static int on_dma_write (uint16_t address, uint8_t value)
{
	if (address == OAM_DMA_REGISTER)
	{
		nes_ppu_load_oam_data (memory + value * 0x100);
		cpucc += 513 + (cpucc & 1);
		return 1;
	}
	return 0;
}

/**
 *  Event handler for writes to one of the controller ports.
 *  Will stop propagation if the address is one of the controller ports.
 */
static int on_controller_port_write (uint16_t address, uint8_t value)
{
	if (address == CTRL_ONE_MEM_LOC)
	{
		nes_io_controller_port_write (0, value);
		nes_io_controller_port_write (1, value);
		return 1;
	}
	return 0;
}

/* End Store Handlers ----------------------------------------------------------------------------------------------- */


/**
 *  Store value to memory.
 *  Loop through and call all registered store event handlers.
 */
static void mem_store (uint8_t value, uint16_t address)
{
	// loop through store event handlers
	// any non-zero return value means we stop propagation and return
	for (store_handler* handle = store_handlers; *handle != NULL; handle ++)
		if ((*handle)(address, value) != 0)
			return;

	// if we arrive here it is alright to store to memory
	memory[address] = value;
	// Apply memory mirroring
	if (address < 0x2000)
	{
		// internal RAM
		for (int i = address % 0x800; i < 0x2000; i += 0x800)
			memory[i] = value;
	}
}


/* Read Handlers ---------------------------------------------------------------------------------------------------- */

/**
 *  typedef for read event handler.
 *  takes an address and pointer to a value to set.
 *  returns 1 or 0 depending on if we should prevent propagation
 */
typedef int (*read_handler) (uint16_t address, uint8_t *value) ;
static read_handler read_handlers[MAX_EVENT_HANDLERS];

/* Read from PPU register */
static int on_ppu_register_read (uint16_t address, uint8_t *value)
{
	if (address >= PPU_REGISTER_MEM_LOC && address < PPU_REGISTER_MEM_LOC + 0x2000)
	{
		*value = nes_ppu_register_read (address & 7);
		return 1;
	}
	return 0;
}

/**
 *   Event handler for reading one of the controller ports.
 *   Will stop propagation if the address is one of the address ports.
 */
static int on_controller_port_read (uint16_t address, uint8_t *value)
{
	if (address == CTRL_ONE_MEM_LOC || address == CTRL_TWO_MEM_LOC)
	{
		*value = nes_io_controller_port_read (address & 1);
		return 1;
	}
	return 0;
}

/* End Read Handlers ------------------------------------------------------------------------------------------------ */


/**
 *  Read a value from the memory.
 *  Loop through all read event handlers before returning the value.
 */
static uint8_t mem_read (uint16_t address)
{
	uint8_t b = memory[address];
	// loop through read event handlers
	for (read_handler* handle = read_handlers; *handle != NULL; handle ++)
		if ((*handle)(address, &b) != 0)
			break;
	return b;
}

/**
 *  Preferred abstract function for getting a value depending on addressing mode.
 *  Will make sure to call correct functions for read events and skip in case we are after the accumulator.
 */
static uint8_t get_value (addressing_mode mode)
{
	if (mode == ACCUMULATOR)
		return a;
	else
	{
		uint16_t address = calculate_address (mode);
		return mem_read (address);
	}
}


#define RST_VECTOR 0xFFFC
/* Init the CPU to its startup state. */
void nes_cpu_reset ()
{
	// default values of registers
	a  = 0;
	x  = 0;
	y  = 0;
	sp = 0xFD;
	ps = 0x24;

	flags = 0;
	cpucc = 0;

	// load program counter
	pc = memory[RST_VECTOR + 1];
	pc = pc << 8 | memory[RST_VECTOR];

	memset (store_handlers, 0, MAX_EVENT_HANDLERS * sizeof (store_handler));
	memset (read_handlers,  0, MAX_EVENT_HANDLERS * sizeof (read_handler));

	// register store event handlers
	store_handlers[0] = &on_ppu_register_write;
	store_handlers[1] = &on_dma_write;
	store_handlers[2] = &on_controller_port_write;

	// register read event handlers
	read_handlers[0]  = &on_ppu_register_read;
	read_handlers[1]  = &on_controller_port_read;
}

/**
 *  Set signal in CPU.
 *  Used to signal interrupt is required.
 */
void nes_cpu_signal (enum nes_cpu_signal sig)
{
	signals |= sig;
}

/**
 *  Handle interrupt.
 *  Does the necessary pushing to stack and jump to the new program counter.
 *  An interrupt takes 7 cycles to perform.
 */
static inline void interrupt (uint16_t _pc)
{
	// push PC
	push (pc >> 8); // high
	push (pc);      // low
	// push PS
	push (ps | BREAK | 0x10); // push processor status with B and unused flag set
	ps |= INTERRUPT;          // disable interrupts
	// set new PC
	pc = _pc;
	cpucc += 7;
}


#define NMI_VECTOR 0xFFFA
/* nmi generates an interrupt and loads the NMI vector. */
static void nmi ()
{
	uint16_t nmi_vector = memory[NMI_VECTOR + 1];
	nmi_vector = (nmi_vector << 8) | memory[NMI_VECTOR];
	interrupt (nmi_vector);
}


#define IRQ_VECTOR 0xFFFE
/* irq generates an interrupt, if the interrupts are not disabled, and loads the IRQ vector. */
static void irq ()
{
	if (ps & ~INTERRUPT)
	{
		uint16_t irq_vector = memory[IRQ_VECTOR + 1];
		irq_vector = (irq_vector << 8) | memory[IRQ_VECTOR];
		interrupt (irq_vector);
	}
}

/**
 *  Convenience function for setting flags depending of the value of value parameter.
 */
static inline void set_flags (uint8_t value, uint8_t flags)
{
	ps &= ~flags;
	if ((flags & ZERO) == ZERO && value == 0)
		ps |= ZERO;
	if ((flags & NEGATIVE) == NEGATIVE && (value & 0x80) == 0x80)
		ps |= NEGATIVE;
}


/** -----------------------------------------------------------------------------------
 * CPU INSTRUCTIONS  *
 * ------------------------------------------------------------------------------------ */

/**
 *  CPU Instruction.
 *  Has a name (for debugging purposes) and a pointer to a function which to run.
 */
typedef struct instruction
{
	const char *name;
	void (*exec) (addressing_mode);
}
instruction;

// Add With Carry
static void adc (addressing_mode mode)
{
	uint16_t b = get_value (mode);
	uint16_t v = b + a + (ps & CARRY);
	uint8_t  c = v;

	set_flags (c, ZERO | NEGATIVE | CARRY | OVERFLOW);

	if (v > 0xFF)
		ps |= CARRY;

	if (~(a ^ b) & (a ^ c) & 0x80)
		ps |= OVERFLOW;

	a = c;
}
static const instruction ADC = { "ADC", &adc };


// Logical AND
static void and (addressing_mode mode) {
	uint8_t v = get_value (mode);
	a &= v;
	set_flags (a, NEGATIVE | ZERO);
}
static const instruction AND = { "AND", &and };


// Arithmetic shift left
static void asl (addressing_mode mode) {
	uint8_t  v;
	uint16_t adr = calculate_address (mode);

	if (mode == ACCUMULATOR)
		v = a;
	else
		v = mem_read (adr);

	ps &= ~CARRY;
	// set carry flag to bit 7 of value (indicates overflow)
	ps |= v >> 7 & 1;
	// shift one bit left and set last bit to zero
	v <<= 1;
	v  &= 0xFE;

	set_flags (v, ZERO | NEGATIVE);

	if (mode == ACCUMULATOR)
		a = v;
	else
		mem_store (v, adr);
}
static const instruction ASL = { "ASL", &asl };


// Branch if carry clear
static void bcc (addressing_mode mode)
{
	if ((ps & CARRY) == 0)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BCC = { "BCC", &bcc };


// Branch if carry set
static void bcs (addressing_mode mode)
{
	if ((ps & CARRY) == CARRY)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BCS = { "BCS", &bcs };


// Branch if equal
static void beq (addressing_mode mode)
{
	if ((ps & ZERO) == ZERO)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BEQ = { "BEQ", &beq };


// Bit test
static void bit (addressing_mode mode)
{
	uint8_t v = get_value (mode);
	// reset overflow and negative bits and then set them to bit 6-7
	// of memory value
	ps &= ~(OVERFLOW | NEGATIVE | ZERO);
	ps |= v & 0xC0;
	if ((a & v) == 0)
		ps |= ZERO;
}
static const instruction BIT = { "BIT", &bit };


// Branch if minus
static void bmi (addressing_mode mode)
{
	if ((ps & NEGATIVE) == NEGATIVE)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BMI = { "BMI", &bmi };


// Branch if not equal
static void bne (addressing_mode mode)
{
	if ((ps & ZERO) == 0)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BNE = { "BNE", &bne };


// Branch if positive
static void bpl (addressing_mode mode)
{
	if ((ps & NEGATIVE) == 0)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BPL = { "BPL", &bpl };


// Force interrupt
static void brk (addressing_mode mode)
{
	ps |= BREAK;
	uint16_t irq_vector = memory[IRQ_VECTOR + 1];
	irq_vector = irq_vector << 8 | memory[IRQ_VECTOR];
	interrupt (irq_vector);
}
static const instruction BRK = { "BRK", &brk };


// Branch if overflow clear
static void bvc (addressing_mode mode)
{
	if ((ps & OVERFLOW) == 0)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BVC = { "BVC", &bvc };


// Branch if overflow is set
static void bvs (addressing_mode mode)
{
	if ((ps & OVERFLOW) == OVERFLOW)
	{
		uint8_t v = get_value (mode);
		cpucc ++;
		branch (v);
	}
}
static const instruction BVS = { "BVS", &bvs };


// Clear carry flag
static void clc (addressing_mode mode)
{
	ps &= ~CARRY;
}
static const instruction CLC = { "CLC", &clc };


// Clear decimal flag
static void cld (addressing_mode mode)
{
	ps &= ~DECIMAL;
}
static const instruction CLD = { "CLD", &cld };


// Clear interrupt disable
static void cli (addressing_mode mode)
{
	ps &= ~INTERRUPT;
}
static const instruction CLI = { "CLI", &cli };


// Clear overflow flag
static void clv (addressing_mode mode)
{
	ps &= ~OVERFLOW;
}
static const instruction CLV = { "CLV", &clv };


// Compare
static void cmp (addressing_mode mode)
{
	uint8_t v = get_value (mode);
	set_flags (a - v, ZERO | NEGATIVE | CARRY);
	if (a >= v)
		ps |= CARRY;
}
static const instruction CMP = { "CMP", &cmp };


// Compare X register
static void cpx (addressing_mode mode)
{
	uint8_t m = get_value (mode);
	set_flags (x - m, ZERO | NEGATIVE | CARRY);
	if (x >= m)
		ps |= CARRY;
}
static const instruction CPX = { "CPX", &cpx };


// Compare Y register
static void cpy (addressing_mode mode)
{
	uint8_t m = get_value (mode);
	set_flags (y - m, ZERO | NEGATIVE | CARRY);
	if (y >= m)
		ps |= CARRY;
}
static const instruction CPY = { "CPY", &cpy };


// Decrement memory
static void dec (addressing_mode mode)
{
	uint16_t adr  = calculate_address (mode);
	uint8_t value = mem_read (adr) - 1;
	set_flags (value, ZERO | NEGATIVE);
	mem_store (value, adr);
}
static const instruction DEC = { "DEC", &dec };


// Decrement X register
static void dex (addressing_mode mode)
{
	x --;
	set_flags (x, ZERO | NEGATIVE);
}
static const instruction DEX = { "DEX", &dex };


// Decrement Y register
static void dey (addressing_mode mode)
{
	y --;
	set_flags (y, ZERO | NEGATIVE);
}
static const instruction DEY = { "DEY", &dey };


// Exclusive OR
static void eor (addressing_mode mode)
{
	uint8_t v = get_value (mode);
	a ^= v;
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction EOR = { "EOR", &eor };


// Increment memory
static void inc (addressing_mode mode)
{
	uint16_t adr = calculate_address (mode);
	uint8_t value = mem_read (adr) + 1;
	set_flags (value, ZERO | NEGATIVE);
	mem_store (value, adr);
}
static const instruction INC = { "INC", &inc };


// Increment X register
static void inx (addressing_mode mode)
{
	x ++;
	set_flags (x, ZERO | NEGATIVE);
}
static const instruction INX = { "INX", &inx };


// Increment Y register
static void iny (addressing_mode mode)
{
	y ++;
	set_flags (y, ZERO | NEGATIVE);
}
static const instruction INY = { "INY", &iny };


// Jump
static void jmp (addressing_mode mode)
{
	pc = calculate_address (mode);
}
static const instruction JMP = { "JMP", &jmp };


// Jump to subroutine
static void jsr (addressing_mode mode)
{
	uint16_t adr = calculate_address (mode);
	pc ++;
	push (pc >> 8);
	push (pc);
	pc = adr;
}
static const instruction JSR = { "JSR", &jsr };


// Load accumulator
static void lda (addressing_mode mode)
{
	a = get_value (mode);
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction LDA = { "LDA", &lda };


// Load X register
static void ldx (addressing_mode mode)
{
	x = get_value (mode);
	set_flags (x, ZERO | NEGATIVE);
}
static const instruction LDX = { "LDX", &ldx };


// Load Y register
static void ldy (addressing_mode mode)
{
	y = get_value (mode);
	set_flags (y, ZERO | NEGATIVE);
}
static const instruction LDY = { "LDY", &ldy };


// Logical shift right
static void lsr (addressing_mode mode)
{
	uint8_t b;
	uint16_t adr = calculate_address (mode);
	if (mode == ACCUMULATOR)
		b = a;
	else
		b = mem_read (adr);

	ps  &= ~CARRY;
	ps  |= b & 1;
	b  >>= 1;
	set_flags (b, ZERO | NEGATIVE);

	if (mode == ACCUMULATOR)
		a = b;
	else
		mem_store (b, adr);
}
static const instruction LSR = { "LSR", &lsr };


// No operation
static void nop (addressing_mode mode) { }
static const instruction NOP = { "NOP", &nop };


// Logical inclusive or
static void ora (addressing_mode mode)
{
	a |= get_value (mode);
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction ORA = { "ORA", &ora };


// Push accumulator
static void pha (addressing_mode mode)
{
	push (a);
}
static const instruction PHA = { "PHA", &pha };


// Push processor status
static void php (addressing_mode mode)
{
	push (ps | 0x30);
}
static const instruction PHP = { "PHP", &php };


// Pull accumulator
static void pla (addressing_mode mode)
{
	a = pop ();
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction PLA = { "PLA", &pla };


// Pull processor status
static void plp (addressing_mode mode)
{
	ps = pop ();
}
static const instruction PLP = { "PLP", &plp };


// Rotate left
static void rol (addressing_mode mode)
{
	uint8_t b;
	uint16_t adr = calculate_address (mode);

	if (mode == ACCUMULATOR)
		b = a;
	else
		b = mem_read (adr);

	uint8_t c = b >> 7 & 1;
	b  <<= 1;
	b   |= ps & CARRY;
	ps  &= ~CARRY;
	ps  |= c;

	set_flags (b, ZERO | NEGATIVE);

	if (mode == ACCUMULATOR)
		a = b;
	else
		mem_store (b, adr);
}
static const instruction ROL = { "ROL", &rol };


// Rotate right
static void ror (addressing_mode mode)
{
	uint8_t b;
	uint16_t adr = calculate_address (mode);

	if (mode == ACCUMULATOR)
		b = a;
	else
		b = mem_read (adr);

	uint8_t c = b & 1;
	b  >>= 1;
	b   |= (ps & CARRY) << 7;
	ps  &= ~CARRY;
	ps  |= c;

	set_flags (b, ZERO | NEGATIVE);

	if (mode == ACCUMULATOR)
		a = b;
	else
		mem_store (b, adr);
}
static const instruction ROR = { "ROR", &ror };


// Return from interrupt
static void rti (addressing_mode mode)
{
	ps = pop ();
	pc = pop ();
	uint16_t b = pop ();
	pc |= b << 8;
}
static const instruction RTI = { "RTI", &rti };


// Return from subroutine
static void rts (addressing_mode mode)
{
	pc = pop ();
	uint16_t b = pop ();
	pc |= b << 8;
	pc ++;
}
static const instruction RTS = { "RTS", &rts };


// Subtract with carry
static void sbc (addressing_mode mode)
{
	int16_t b = get_value (mode);
	int16_t c = a - b - (1 - (ps & CARRY));

	ps &= ~(CARRY | OVERFLOW); // reset CARRY and OVERFLOW

	if (~(a ^ ~b) & (a ^ c) & 0x80) // if signs do not match there is overflow
		ps |= OVERFLOW;

	if (c >= 0) // 0 -> 255 set CARRY
		ps |= CARRY;

	a = c; // store to A and set ZERO and NEGATIVE flag
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction SBC = { "SBC", &sbc };


// Set carry flag
static void sec (addressing_mode mode)
{
	ps |= CARRY;
}
static const instruction SEC = { "SEC", &sec };


// Set decimal flag
static void sed (addressing_mode mode)
{
	ps |= DECIMAL;
}
static const instruction SED = { "SED", &sed };


// Set interrupt disabled
static void sei (addressing_mode mode)
{
	ps |= INTERRUPT;
}
static const instruction SEI = { "SEI", &sei };


// Store accumulator
static void sta (addressing_mode mode)
{
	uint16_t adr = calculate_address (mode);
	mem_store (a, adr);
}
static const instruction STA = { "STA", &sta };


// Store X register
static void stx (addressing_mode mode)
{
	uint16_t adr = calculate_address (mode);
	mem_store (x, adr);
}
static const instruction STX = { "STX", &stx };


// Store Y register
static void sty (addressing_mode mode)
{
	uint16_t adr = calculate_address (mode);
	mem_store (y, adr);
}
static const instruction STY = { "STY", &sty };


// Transfer accumulator to X
static void tax (addressing_mode mode)
{
	x = a;
	set_flags (x, ZERO | NEGATIVE);
}
static const instruction TAX = { "TAX", &tax };


// Transfer accumulator to Y
static void tay (addressing_mode mode)
{
	y = a;
	set_flags (y, ZERO | NEGATIVE);
}
static const instruction TAY = { "TAY", &tay };


// Transfer stack pointer to X
static void tsx (addressing_mode mode)
{
	x = sp;
	set_flags (x, ZERO | NEGATIVE);
}
static const instruction TSX = { "TSX", &tsx };


// Transfer X to accumulator
static void txa (addressing_mode mode)
{
	a = x;
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction TXA = { "TXA", &txa };


// Transfer X to stack pointer
static void txs (addressing_mode mode)
{
	sp = x;
}
static const instruction TXS = { "TXS", &txs };


// Transfer Y to accumulator
static void tya (addressing_mode mode)
{
	a = y;
	set_flags (a, ZERO | NEGATIVE);
}
static const instruction TYA = { "TYA", &tya };


// Illegal operation
static const instruction unknown_instruction = { "[*]", &nop };


/**
*  CPU Operation.
*  Points to a CPU instruction with preset addressing mode.
*  Already knows the number of bytes and cycles (-ich) that the instruction will consume.
*/
typedef struct operation
{
	const instruction*  instr;
	addressing_mode     mode;
	uint16_t            bytes;
	int                 cycles;
	int                 cc_page_cross;
}
operation;

/**
*  Execute an operation setting the number of bytes and the number of cycles the operation consumed.
*/
static void operation_exec (operation *op)
{
	// execute instruction
	op->instr->exec (op->mode);
	// increment PC and CPUCC
	pc += op->bytes;
	cpucc += op->cycles;
	// add extra cycles in case of page cross
	if (flags & PAGE_CROSS)
		cpucc += op->cc_page_cross;

	flags &= ~PAGE_CROSS; // reset page cross flag
}

#ifdef VERBOSE
	static void operation_to_string (operation *op, char *dest)
	{
		pc ++;
		sprintf (dest, "%s ", op->instr->name);
		address_calculators_string[op->mode](dest + 4);
		pc --;
	}
#endif

#define illegal_operation \
{\
	&unknown_instruction,\
	IMPLICIT,\
	0,\
	0,\
	0\
}
// opcode to operation map
static operation operations[16][16] =
{
	{{&BRK, IMPLICIT, 0, 7, 0},    {&ORA, INDEXED_INDIRECT, 1, 6, 0},  illegal_operation,           illegal_operation,
	 illegal_operation,            {&ORA, ZERO_PAGE,        1, 3, 0}, {&ASL, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&PHP, IMPLICIT, 0, 3, 0},    {&ORA, IMMEDIATE,        1, 2, 0}, {&ASL, ACCUMULATOR, 0, 2, 0}, illegal_operation,
	 illegal_operation,            {&ORA, ABSOLUTE,         2, 4, 0}, {&ASL, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0x1
	{{&BPL, RELATIVE, 1, 2, 0},    {&ORA, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&ORA, ZERO_PAGE_X,      1, 4, 0}, {&ASL, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&CLC, IMPLICIT, 0, 2, 0},    {&ORA, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&ORA, ABSOLUTE_X,       2, 4, 1}, {&ASL, ABSOLUTE_X,  2, 7, 0}, illegal_operation},
	// 0x2
	{{&JSR, ABSOLUTE,  0, 6, 0},   {&AND, INDEXED_INDIRECT, 1, 6, 0}, illegal_operation,            illegal_operation,
	 {&BIT, ZERO_PAGE, 1, 3, 0},   {&AND, ZERO_PAGE,        1, 3, 0}, {&ROL, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&PLP, IMPLICIT,  0, 4, 0},   {&AND, IMMEDIATE,        1, 2, 0}, {&ROL, ACCUMULATOR, 0, 2, 0}, illegal_operation,
	 {&BIT, ABSOLUTE,  2, 4, 0},   {&AND, ABSOLUTE,         2, 4, 0}, {&ROL, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0x3
	{{&BMI, RELATIVE, 1, 2, 0},    {&AND, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&AND, ZERO_PAGE_X,      1, 4, 0}, {&ROL, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&SEC, IMPLICIT, 0, 2, 0},    {&AND, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&AND, ABSOLUTE_X,       2, 4, 1}, {&ROL, ABSOLUTE_X,  2, 7, 0}, illegal_operation},
	// 0x4
	{{&RTI, IMPLICIT, 0, 6, 0},    {&EOR, INDEXED_INDIRECT, 1, 6, 0}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&EOR, ZERO_PAGE,        1, 3, 0}, {&LSR, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&PHA, IMPLICIT, 0, 3, 0},    {&EOR, IMMEDIATE,        1, 2, 0}, {&LSR, ACCUMULATOR, 0, 2, 0}, illegal_operation,
	 {&JMP, ABSOLUTE, 0, 3, 0},    {&EOR, ABSOLUTE,         2, 4, 0}, {&LSR, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0x5
	{{&BVC, RELATIVE, 1, 2, 0},    {&EOR, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&EOR, ZERO_PAGE_X,      1, 4, 0}, {&LSR, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&CLI, IMPLICIT, 0, 2, 0},    {&EOR, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&EOR, ABSOLUTE_X,       2, 4, 1}, {&LSR, ABSOLUTE_X,  2, 7, 0}, illegal_operation},
	// 0x6
	{{&RTS, IMPLICIT, 0, 6, 0},    {&ADC, INDEXED_INDIRECT, 1, 5, 0}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&ADC, ZERO_PAGE,        1, 3, 0}, {&ROR, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&PLA, IMPLICIT, 0, 4, 0},    {&ADC, IMMEDIATE,        1, 2, 0}, {&ROR, ACCUMULATOR, 0, 2, 0}, illegal_operation,
	 {&JMP, INDIRECT, 0, 5, 0},    {&ADC, ABSOLUTE,         2, 4, 0}, {&ROR, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0x7
	{{&BVS, RELATIVE, 1, 2, 0},    {&ADC, INDIRECT_INDEXED, 1, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&ADC, ZERO_PAGE_X,      1, 4, 0}, {&ROR, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&SEI, IMPLICIT, 0, 2, 0},    {&ADC, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&ADC, ABSOLUTE_X,       2, 4, 1}, {&ROR, ABSOLUTE_X,  2, 7, 0}, illegal_operation},
	// 0x8
	{illegal_operation,            {&STA, INDEXED_INDIRECT, 1, 6, 0}, illegal_operation,            illegal_operation,
	 {&STY, ZERO_PAGE, 1, 3, 0},   {&STA, ZERO_PAGE,        1, 3, 0}, {&STX, ZERO_PAGE,   1, 3, 0}, illegal_operation,
	 {&DEY, IMPLICIT,  0, 2, 0},    illegal_operation,                {&TXA, IMPLICIT,    0, 2, 0}, illegal_operation,
	 {&STY, ABSOLUTE,  2, 4, 0},   {&STA, ABSOLUTE,         2, 4, 0}, {&STX, ABSOLUTE,    2, 4, 0}, illegal_operation},
	// 0x9
	{{&BCC, RELATIVE,    1, 2, 0}, {&STA, INDIRECT_INDEXED, 1, 6, 0}, illegal_operation,            illegal_operation,
	 {&STY, ZERO_PAGE_X, 1, 4, 0}, {&STA, ZERO_PAGE_X,      1, 4, 0}, {&STX, ZERO_PAGE_Y, 1, 4, 0}, illegal_operation,
	 {&TYA, IMPLICIT,    0, 2, 0}, {&STA, ABSOLUTE_Y,       2, 5, 0}, {&TXS, IMPLICIT,    0, 2, 0}, illegal_operation,
	 illegal_operation,            {&STA, ABSOLUTE_X,       2, 5, 0}, illegal_operation,            illegal_operation},
	// 0xa
	{{&LDY, IMMEDIATE, 1, 2, 0},   {&LDA, INDEXED_INDIRECT, 1, 6, 0}, {&LDX, IMMEDIATE,   1, 2, 0}, illegal_operation,
	 {&LDY, ZERO_PAGE, 1, 3, 0},   {&LDA, ZERO_PAGE,        1, 3, 0}, {&LDX, ZERO_PAGE,   1, 3, 0}, illegal_operation,
	 {&TAY, IMPLICIT,  0, 2, 0},   {&LDA, IMMEDIATE,        1, 2, 0}, {&TAX, IMPLICIT,    0, 2, 0}, illegal_operation,
	 {&LDY, ABSOLUTE,  2, 4, 0},   {&LDA, ABSOLUTE,         2, 4, 0}, {&LDX, ABSOLUTE,    2, 4, 0}, illegal_operation},
	// 0xb
	{{&BCS, RELATIVE,    1, 2, 0}, {&LDA, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 {&LDY, ZERO_PAGE_X, 1, 4, 0}, {&LDA, ZERO_PAGE_X,      1, 4, 0}, {&LDX, ZERO_PAGE_Y, 1, 4, 0}, illegal_operation,
	 {&CLV, IMPLICIT,    0, 2, 0}, {&LDA, ABSOLUTE_Y,       2, 4, 1}, {&TSX, IMPLICIT,    0, 2, 0}, illegal_operation,
	 {&LDY, ABSOLUTE_X,  2, 4, 1}, {&LDA, ABSOLUTE_X,       2, 4, 1}, {&LDX, ABSOLUTE_Y,  2, 4, 1}, illegal_operation},
	// 0xc
	{{&CPY, IMMEDIATE, 1, 2, 0},   {&CMP, INDEXED_INDIRECT, 1, 6, 0}, illegal_operation,            illegal_operation,
	 {&CPY, ZERO_PAGE, 1, 3, 0},   {&CMP, ZERO_PAGE,        1, 3, 0}, {&DEC, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&INY, IMPLICIT,  0, 2, 0},   {&CMP, IMMEDIATE,        1, 2, 0}, {&DEX, IMPLICIT,    0, 2, 0}, illegal_operation,
	 {&CPY, ABSOLUTE,  2, 4, 0},   {&CMP, ABSOLUTE,         2, 4, 0}, {&DEC, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0xd
	{{&BNE, RELATIVE,  1, 2, 0},   {&CMP, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&CMP, ZERO_PAGE_X,      1, 4, 0}, {&DEC, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&CLD, IMPLICIT,  0, 2, 0},   {&CMP, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&CMP, ABSOLUTE_X,       2, 4, 1}, {&DEC, ABSOLUTE_X,  2, 7, 0}, illegal_operation},
	// 0xe
	{{&CPX, IMMEDIATE, 1, 2, 0},   {&SBC, INDEXED_INDIRECT, 1, 6, 0}, illegal_operation,            illegal_operation,
	 {&CPX, ZERO_PAGE, 1, 3, 0},   {&SBC, ZERO_PAGE,        1, 3, 0}, {&INC, ZERO_PAGE,   1, 5, 0}, illegal_operation,
	 {&INX, IMPLICIT,  0, 2, 0},   {&SBC, IMMEDIATE,        1, 2, 0}, {&NOP, IMPLICIT,    0, 2, 0}, illegal_operation,
	 {&CPX, ABSOLUTE,  2, 4, 0},   {&SBC, ABSOLUTE,         2, 4, 0}, {&INC, ABSOLUTE,    2, 6, 0}, illegal_operation},
	// 0xf
	{{&BEQ, RELATIVE,  1, 2, 0},   {&SBC, INDIRECT_INDEXED, 1, 5, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&SBC, ZERO_PAGE_X,      1, 4, 0}, {&INC, ZERO_PAGE_X, 1, 6, 0}, illegal_operation,
	 {&SED, IMPLICIT,  0, 2, 0},   {&SBC, ABSOLUTE_Y,       2, 4, 1}, illegal_operation,            illegal_operation,
	 illegal_operation,            {&SBC, ABSOLUTE_X,       2, 4, 1}, {&INC, ABSOLUTE_X,  2, 7, 0}, illegal_operation}
};

#undef illegal_operation

/* end CPU INSTRUCTIONS --------------------------------------------------------------- */


#ifdef VERBOSE
void print_operation (operation* op) {
	char reg_string[128] = {0};
	char op_string[128] = {0};

	sprintf (reg_string, "A:%.2X X:%.2X Y:%.2X PS:%.2X SP:%.2X ", a, x, y, ps, sp);
	operation_to_string (op, op_string);
	// printf ("%.4X %.2X %-32s %s\n", pc, opcode, op_string, reg_string);
	printf ("%.4X  ", pc);
	for (int i = 0; i < op->bytes + 1; i ++)
		printf ("%.2X ", memory[pc + i]);
	for (int i = 0; i < 3 - op->bytes - 1; i ++)
		printf ("   ");
	printf (" %-32s %s", op_string, reg_string);
}
#endif


#define PPU_CC_PER_CPU_CC 3

int nes_cpu_step ()
{
	int cc = cpucc;

	// check interrupts
	if (signals & NMI)
		nmi ();
	else if (signals & IRQ)
		irq ();
	signals = 0;

	// get operation
	uint8_t opcode = memory[pc];
	operation* op = &operations[opcode >> 4 & 0xF][opcode & 0xF];

	#ifdef VERBOSE
		print_operation (op);
	#endif

	// execute operation and step forward
	pc ++;
	operation_exec (op);

	#ifdef VERBOSE
		printf ("\n");
	#endif

	cc = cpucc - cc;
	int cpucc_per_frame = SCANLINES_PER_FRAME * PPUCC_PER_SCANLINE / PPU_CC_PER_CPU_CC + 1;
	if (cpucc > cpucc_per_frame)
		cpucc -= cpucc_per_frame;

	return cc;
}
