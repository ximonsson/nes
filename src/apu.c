#include "nes/apu.h"
#include "nes/cpu.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* apucc represents the APU clock cycles */
static int apucc;

/* frame contains which frame in the frame counter we are currently at */
static int frame;

/* APU registers */
static uint8_t* registers;

#define STATUS       registers[0x15]
#define FRAMECOUNTER registers[0x17]

/* length_counter_table contains lookup values for length counters */
static uint8_t length_counter_table[32] =
{
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

/* envelope linked to a channel */
struct envelope
{
	uint8_t* reg;     // register with flags
	uint8_t  decay;   // output volume when not constant flag is set
	int      divider; // period
	int      start;   // start flag
};

/* envelop_init initialize the envelope with reg as pointer to it's register */
static void envelope_init (struct envelope* env, uint8_t* reg)
{
	env->reg     = reg;
	env->divider = 0;
	env->start   = 0;
	env->decay   = 0;
}

/* envelope_clock clocks the supplied envelope linked to a channel */
static void envelope_clock (struct envelope* env)
{
	uint8_t r = *env->reg; // register value
	if (env->start) // start flag set
	{
		env->decay   = 15;
		env->divider = r & 0xF;
		env->start   = 0;
	}
	else
	{
		if (env->divider == 0)
		{
			// load V into divider
			env->divider = r & 0xF;
			// clock or loop decay level counter
			if (env->decay)
				env->decay --;
			else if (r & 0x20) // loop flag is set
				env->decay = 15;
		}
		else
			env->divider --;
	}
}

/**
 * envelope_volume returns the current volume of the envelope wether it is constant
 * or controlled by it's decay level counter.
 */
static uint8_t envelope_volume (struct envelope* env)
{
	if ((*env->reg) & 0x10) // constant volume
		return (*env->reg) & 0xF;
	else
		return env->decay;
}

/*
 * PULSE CHANNEL
 */

/* APU Pulse channel */
struct pulse
{
	uint8_t  length_counter; // length counter value
	uint8_t  number;         // pulse channel index, 1 or 2
	uint16_t timer;
	int      sweep;          // sweep divider
	int      reload_sweep;   // reload sweep flag
	int      sequencer;
	int      overflow;       // overflow from the sweep unit
	uint8_t* reg;            // base register address
	struct   envelope env;   // envelope unit
};

/* pulse_init initializes a new channel from a memory location to the first (of four) register */
static void pulse_init (struct pulse* ch, uint8_t* reg, uint8_t number)
{
	ch->reg            = reg;
	ch->timer          = 0;
	ch->sequencer      = 0;
	ch->length_counter = 0;
	ch->reload_sweep   = 0;
	ch->sweep          = 0;
	ch->overflow       = 0;
	ch->number         = number;
	envelope_init (&ch->env, reg);
}

/* pulse_period returns the channel's period length. */
static uint16_t inline pulse_period (struct pulse* ch)
{
	uint16_t period = ch->reg[3] & 7;
	         period = (period << 8) | ch->reg[2];
	return period;
}

/* clock pulse channel's length counter. */
static void pulse_clock_length_counter (struct pulse* ch)
{
	int halt = (ch->reg[0] & 0x20) == 0x20;
	if (!halt && ch->length_counter)
		ch->length_counter --;
}

/* pulse_envelope_write writes to a channel envelope register */
static void pulse_envelope_write (struct pulse* ch, uint8_t v)
{
	// restart the envelope
	ch->env.start = 1;
}

/* pulse_timer_low_write writes to the low bits of the channel's timer */
static void pulse_timer_low_write (struct pulse* ch, uint8_t v)
{
	// nada
}

/* pulse_reload_len_counter handles writes to a channels length counter / timer high register. */
static void pulse_reload_len_counter (struct pulse* ch, uint8_t v)
{
	if (~STATUS & ch->number)
		return;
	ch->length_counter = length_counter_table[v >> 3]; // reload length counter
	ch->env.start = 1; // restart the envelope
	ch->sequencer = 0; // restart sequencer
}

/* pulse_sweep_write handles writes to the pulse channel's sweep unit. */
static void pulse_sweep_write (struct pulse* ch, uint8_t v)
{
	ch->reload_sweep = 1; // make the sweep unit reload
}

/* pulse_adjust_period adjusts the channel's period after sweeping */
static void pulse_adjust_period (struct pulse* ch)
{
	uint8_t shift = ch->reg[1] & 7;
	if (shift == 0)
		return;

	uint16_t period = pulse_period (ch);
	// shift period to get adjustement and switch sign if needed
	uint16_t delta = period >> shift;
	if (ch->reg[1] & 8) // negate
	{
		delta = -delta;
		// if pulse channel 1 there is to be -1
		if (ch->number == 1)
			delta --;
	}
	period += delta;
	// set period overflow flag
	if ((ch->overflow = period > 0x7FF) != 0)
		return;

	// update the registers with the new period
	ch->reg[2] = period;
	ch->reg[3] = (ch->reg[3] & 0xF8) | ((period >> 8) & 7);
}

/* pulse_clock_sweep clock the channel's sweep unit */
static void pulse_clock_sweep (struct pulse* ch)
{
	int enabled = (ch->reg[1] & 0x80) != 0;
	uint8_t period = ((ch->reg[1] >> 4) & 7) + 1;
	if (ch->reload_sweep)
	{
		// reload sweep with period and clear the reload flag
		ch->sweep = period;
		ch->reload_sweep = 0;
		if (ch->sweep == 0 && enabled) // adjust period
			pulse_adjust_period (ch);
	}
	else
	{
		if (ch->sweep)
			ch->sweep --;
		else if (enabled) // sweep == 0
		{
			pulse_adjust_period (ch); // adjust period
			ch->sweep = period;
		}
	}
}

/* pulse_clock_timer clocks the channel's timer. */
static void pulse_clock_timer (struct pulse* ch)
{
	if (ch->timer == 0)
	{
		// reload timer and step duty waveform sequence
		ch->timer = pulse_period (ch);
		ch->sequencer = (ch->sequencer + 1) & 7;
	}
	else // decrement timer
		ch->timer --;
}

/* duty_sequence are the different waveform sequences for a pulse channel */
static uint8_t duty_sequence[4][8] =
{
	{ 0, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 1, 1, 0, 0, 0 },
	{ 1, 0, 0, 1, 1, 1, 1, 1 }
};

/* pulse_output gives the next output from the channel */
static uint8_t pulse_output (struct pulse* ch)
{
	uint8_t  duty   = ch->reg[0] >> 6;
	uint16_t period = pulse_period (ch);

	if (duty_sequence[duty][ch->sequencer] == 0)
		return 0;
	else if (ch->length_counter == 0)
		return 0;
	else if (period < 8 || ch->overflow)
		return 0;

	// return envelope volume
	return envelope_volume (&ch->env);
}

/*
 * TRIANGLE CHANNEL
 */

/* triangle channel */
struct triangle
{
	int      sequencer;
	uint16_t timer;
	uint8_t  length_counter;
	int      linear_counter;
	int      linear_counter_reload;
	uint8_t* reg;
};

/* triangle_init initialize the triangle channel using reg as the base of it's four registers */
static void triangle_init (struct triangle* tr, uint8_t* reg)
{
	tr->reg = reg;
	tr->timer = 0;
	tr->length_counter = 0;
	tr->linear_counter = 0;
	tr->linear_counter_reload = 0;
	tr->sequencer = 0;
}

/* triangle_reload_timer reloads the timer for the triangle channel. */
static void triangle_reload_timer (struct triangle* tr)
{
	tr->timer = tr->reg[3] & 7;
	tr->timer = (tr->timer << 8) | tr->reg[2];
}

/* triangle_clock_timer will step the provided triangle channel's timer */
static void triangle_clock_timer (struct triangle* tr)
{
	if (tr->timer == 0)
	{
		// clock sequencer and reload timer
		if (tr->linear_counter && tr->length_counter)
			tr->sequencer = (tr->sequencer + 1) & 0x1F;
		triangle_reload_timer (tr);
	}
	else
		tr->timer --;
}

/* triangle_clock_linear_counter clocks the triangle channel's linear counter */
static void triangle_clock_linear_counter (struct triangle* tr)
{
	if (tr->linear_counter_reload) // reload the linear counter
		tr->linear_counter = tr->reg[0] & 0x7F;
	else if (tr->linear_counter) // decrement the linear counter
		tr->linear_counter --;

	// if the control flag is cleared the reload flag is cleared
	if (~tr->reg[0] & 0x80)
		tr->linear_counter_reload = 0;
}

/* triangle_clock_length_counter clocks the triangle channel's length counter as long as it is not halted */
static void triangle_clock_length_counter (struct triangle* tr)
{
	int halt = tr->reg[0] & 0x80;
	if (!halt && tr->length_counter)
		tr->length_counter --;
}

/* triangle_reload_length_counter reload the triangle channel's length counter */
static void triangle_reload_length_counter (struct triangle* tr, uint8_t v)
{
	tr->length_counter = length_counter_table[v >> 3];
	// reload the linear counter
	tr->linear_counter_reload = 1;
}

/* triangle_sequence is the 32 step sequence played by the triangle channel */
static uint8_t triangle_sequence[32] =
{
	15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

/* triangle_output will output the next value in the triangle channel's sequence */
static uint8_t triangle_output (struct triangle* tr)
{
	if (tr->length_counter == 0)
		return 0;
	else if (tr->linear_counter == 0)
		return 0;
	return triangle_sequence[tr->sequencer];
}

/*
 * NOISE CHANNEL
 */

/* noise APU */
struct noise
{
	uint16_t shift_register;
	uint16_t length_counter;
	uint8_t* reg;
	int      timer;
	struct envelope env;
};

/* new_noise_channel returns a new noise channel */
static void noise_init (struct noise* noise, uint8_t* reg)
{
	noise->reg = reg;
	noise->shift_register = 1;
	noise->timer = 0;
	envelope_init (&noise->env, reg);
}

/* noise_clock_lfsr clocks the noise channel's left shift register (LFSR) */
static void noise_clock_lfsr (struct noise* noise)
{
	uint16_t sh = noise->shift_register;
	int shift = 1 + ((noise->reg[2] >> 7) * 5);

	uint8_t feedback = (sh ^ (sh >> shift)) & 1;
	noise->shift_register = ((sh >> 1) & 0x3FFF) | (feedback << 14);
}

/* noise_periods periods to load into the noise channel */
static uint16_t noise_periods[16] =
{
	4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

/* noise_clock_timer clocks the noise channel's timer */
static void noise_clock_timer (struct noise* noise)
{
	if (noise->timer == 0)
	{
		noise->timer = noise_periods[noise->reg[2] & 0xF];
		noise_clock_lfsr (noise);
	}
	else
		noise->timer --;
}

/* noise_output returns the noise channel's envelope volume */
static uint8_t noise_output (struct noise* noise)
{
	if ((noise->shift_register & 1) == 0)
		return 0;
	else if (noise->length_counter == 0)
		return 0;
	return envelope_volume (&noise->env);
}

/* noise_clock_length_counter clocks the noise channel's lenght counter */
static void noise_clock_length_counter (struct noise* noise)
{
	int halt = (*noise->reg) & 0x20;
	if (!halt && noise->length_counter)
	{
		noise->length_counter --;
	}
}

/* noise_reload_len_counter writes to a channels length counter / timer high register */
static void noise_reload_len_counter (struct noise* noise, uint8_t v)
{
	noise->length_counter = length_counter_table[v >> 3]; // reload length counter
	noise->env.start = 1; // restart the envelope
}

/*
 * DMC CHANNEL
 */

/* dmc_mem_reader represents the DMC channel's memory reader unit */
struct dmc_mem_reader
{
	uint16_t address;   // sample address
	uint16_t remaining; // bytes remaining
};

/* dmc_mem_reader_init initializes the DMC memory reader unit */
static void dmc_mem_reader_init (struct dmc_mem_reader* r)
{
	r->address = 0;
	r->remaining = 0;
}

/* dmc_output_unit represents the DMC channel's output unit */
struct dmc_output_unit
{
	uint8_t rsr;       // right shift register
	uint8_t remaining; // bits remaining
	uint8_t silent;    // silence flag
	uint8_t level;     // output level
};

/* dmc_output_unit_init initializes the DMC output unit. Will set the unit to silent. */
static void dmc_output_unit_init (struct dmc_output_unit* o)
{
	o->rsr       = 0;
	o->remaining = 0;
	o->silent    = 1;
	o->level     = 0;
}

/* dmc_rate_index contains rates for the DMC */
static uint16_t dmc_rate_index[16] =
{
	428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106,  84,  72,  54
};

/* APU Delta Modulation Channel (DMC) */
struct dmc
{
	uint8_t  buffer;               // sample buffer
	uint8_t  empty_buffer;         // set if the sample buffer is empty
	uint16_t timer;                // period
	uint8_t* reg;                  // register memory location
	struct dmc_mem_reader  reader; // DMC memory reader
	struct dmc_output_unit output; // DMC output unit
};

/* dmc_init initialize the DMC. */
static void dmc_init (struct dmc* dmc, uint8_t* reg)
{
	dmc->buffer = 0;
	dmc->empty_buffer = 1;
	dmc->timer = 0;
	dmc->reg = reg;
	dmc_mem_reader_init (&dmc->reader);
	dmc_output_unit_init (&dmc->output);
}

/* dmc_reader_reload reloads sample address and length */
static void dmc_reader_reload (struct dmc* dmc)
{
	dmc->reader.address = dmc->reg[2];
	dmc->reader.address = 0xC000 | (dmc->reader.address << 6);
	dmc->reader.remaining = dmc->reg[3];
	dmc->reader.remaining = (dmc->reader.remaining << 4) | 1;
}

/* dmc_clock_reader clocks the DMC's reader unit to load the next sample into its buffer */
static void dmc_clock_reader (struct dmc* dmc)
{
	nes_cpu_stall (4);
	// read from memory
	dmc->buffer = nes_cpu_read_ram (dmc->reader.address);

	// increment address
	dmc->reader.address ++;
	if (dmc->reader.address == 0) // overflow
		dmc->reader.address = 0x8000;

	// decrement bytes remaining and clear the empty buffer flag
	dmc->reader.remaining --;
	dmc->empty_buffer = 0;
}

/**
 * dmc_reload_output reloads the DMC's output unit's cycle by emptying the readers buffer,
 * if not empty, else it will silence the unit.
 * The reader is looped if needed else it outputs an IRQ signal if the flag is set.
 * Should only be called at the end of a cycle.
 */
static void dmc_reload_output (struct dmc* dmc)
{
	// reload output cycle
	dmc->output.remaining = 8;

	if (dmc->empty_buffer) // empty sample buffer - silence the output
		dmc->output.silent = 1;
	else
	{
		// empty buffer into output shift register
		dmc->output.silent = 0;
		dmc->output.rsr = dmc->buffer;
		dmc->empty_buffer = 1;

		// read a new sample
		if (dmc->reader.remaining == 0)
		{
			if (dmc->reg[0] & 0x40) // loop
				dmc_reader_reload (dmc);
			else if (dmc->reg[0] & 0x80) // signal IRQ
				nes_cpu_signal (IRQ);
		}

		if (dmc->reader.remaining)
			dmc_clock_reader (dmc);
	}
}

// dmc_clock_output clocks the DMC's output unit */
static void dmc_clock_output (struct dmc* dmc)
{
	if (!dmc->output.silent)
	{
		// if not silent update the output level
		if ((dmc->output.rsr & 1) && dmc->output.level <= 125)
			dmc->output.level += 2;
		else if ((~dmc->output.rsr & 1) && dmc->output.level >= 2)
			dmc->output.level -= 2;
	}

	// update output cycle
	dmc->output.rsr >>= 1;
	dmc->output.remaining --;

	if (dmc->output.remaining == 0) // reload cycle
		dmc_reload_output (dmc);
}

/* dmc_clock clock the DMC units timer */
static void dmc_clock (struct dmc* dmc)
{
	if (dmc->timer == 0)
	{
		// update output unit
		dmc_clock_output (dmc);
		// reload timer and change output level
		dmc->timer = dmc_rate_index[dmc->reg[0] & 0xF];
	}
	else
		dmc->timer --;
}

/* dmc_output outputs sample from the DMC */
static uint8_t dmc_output (struct dmc* dmc)
{
	if (~STATUS & 0x10)
		return 0;
	// return the output units level
	return dmc->output.level;
}

/**
 *  APU Channels
 */
static struct pulse pulse_1;
static struct pulse pulse_2;
static struct triangle triangle;
static struct noise noise;
static struct dmc dmc;

/* clock_envelopes clocks all the audio channels' envelope units as well
 * as the triangle channel's linear counter. */
static void clock_envelopes ()
{
	envelope_clock (&pulse_1.env);
	envelope_clock (&pulse_2.env);
	envelope_clock (&noise.env);
	triangle_clock_linear_counter (&triangle);
}

/* clock_sweeps clocks the pulse channels' sweep units */
static void clock_sweeps ()
{
	pulse_clock_sweep (&pulse_1);
	pulse_clock_sweep (&pulse_2);
}

/* clock_length_counters clocks all audio channel's length counters */
static void clock_length_counters ()
{
	pulse_clock_length_counter    (&pulse_1);
	pulse_clock_length_counter    (&pulse_2);
	noise_clock_length_counter    (&noise);
	triangle_clock_length_counter (&triangle);
}

static void clock_all ()
{
	clock_envelopes ();
	clock_sweeps ();
	clock_length_counters ();
}

// step_frame_counter_4 steps the frame counter in 4 step mode.
static void inline step_frame_counter_4 ()
{
	switch (frame)
	{
		case 0:
		case 2:
			clock_envelopes();
			break;
		case 1:
		case 3:
			clock_all();
			break;
	}
	if (frame == 3)
	{
		if (~FRAMECOUNTER & 0x40) // frame IRQ inhibit flag clear
			nes_cpu_signal (IRQ);
	}
}

// step_frame_counter_5 steps the frame counter in 5 step mode.
static void inline step_frame_counter_5 ()
{
	switch (frame)
	{
		case 0:
		case 2:
			clock_all();
			break;
		case 1:
		case 3:
			clock_envelopes();
			break;
	}
}

/**
 * step_frame_counter steps the frame counter/sequencer accordingly
 * to the mode that is set in register $4017.
 * mode 0:    mode 1:       function
 * ---------  -----------  -----------------------------
 *   - - - f    - - - - -    IRQ (if bit 6 is clear)
 *   - l - l    l - l - -    Length counter and sweep
 *   e e e e    e e e e -    Envelope and linear counter
 */
static void step_frame_counter ()
{
	frame ++;
	if (FRAMECOUNTER & 0x80)
	{
		frame %= 5;
		step_frame_counter_5 ();
	}
	else
	{
		frame %= 4;
		step_frame_counter_4 ();
	}
}

/* APU Register Writers --------------------------------------------------------------------------------------------- */

/* write to $4000 */
static void pulse1_envelope_write (uint8_t value)
{
	pulse_envelope_write (&pulse_1, value);
}

/* write to $4001 */
static void pulse1_sweep_write (uint8_t value)
{
	pulse_sweep_write (&pulse_1, value);
}

/* write to $4002 */
static void pulse1_timer_low_write (uint8_t value)
{
	pulse_timer_low_write (&pulse_1, value);
}

/* write to $4003 */
static void pulse1_len_cnt_write (uint8_t value)
{
	pulse_reload_len_counter (&pulse_1, value);
}

/* write to $4004 */
static void pulse2_envelope_write (uint8_t value)
{
	pulse_envelope_write (&pulse_2, value);
}

/* write to $4005 */
static void pulse2_sweep_write (uint8_t value)
{
	pulse_sweep_write (&pulse_2, value);
}

/* write to $4006 */
static void pulse2_timer_low_write (uint8_t value)
{
	pulse_timer_low_write (&pulse_2, value);
}

/* write to $4007 */
static void pulse2_len_cnt_write (uint8_t value)
{
	pulse_reload_len_counter (&pulse_2, value);
}

/* write to $4008 */
static void triangle_lin_cnt_write (uint8_t value)
{
	// nada
}

/* write to $400A */
static void triangle_timer_low_write (uint8_t value)
{
	// nada
}

/* write to $400B */
static void triangle_timer_high_write (uint8_t value)
{
	triangle_reload_length_counter (&triangle, value);
}

/* write $400C */
static void noise_env_write (uint8_t value)
{
	noise.env.start = 1;
}

/* write $400F */
static void noise_len_cnt_write (uint8_t value)
{
	noise_reload_len_counter (&noise, value);
}

/* dmc_direct_load loads the supplied value to the DMC's output unit level */
static void dmc_direct_load (uint8_t v)
{
	dmc.output.level = v & 0x7F;
}

/* write to status register */
static void status_write (uint8_t value)
{
	if (~value & 0x10) // silence DMC
		dmc.reader.remaining = 0;
	else if (dmc.reader.remaining == 0) // restart DMC
		dmc_reader_reload (&dmc);

	// silence noise
	if (~value & 0x08)
		noise.length_counter = 0;
	// silence triangle
	if (~value & 0x04)
		triangle.length_counter = 0;
	// silence pulse 2
	if (~value & 0x02)
		pulse_2.length_counter = 0;
	// silence pulse 1
	if (~value & 0x01)
		pulse_1.length_counter = 0;

	registers[0x10] &= 0x7F; // clear the DMC interrupt flag
}

/* write to frame counter register */
static void frame_counter_write (uint8_t value)
{
	if (value & 0x80)
		clock_all ();
}

/* End APU Register Writers ----------------------------------------------------------------------------------------- */

/* APU register writers */
typedef void (*writer) (uint8_t value) ;
static writer writers[0x18] = {
	// pulse 1 register write handlers
	&pulse1_envelope_write,     // $4000
	&pulse1_sweep_write,        // $4001
	&pulse1_timer_low_write,    // $4002
	&pulse1_len_cnt_write,      // $4003

	// pulse 2 register write handlers
	&pulse2_envelope_write,     // $4004
	&pulse2_sweep_write,        // $4005
	&pulse2_timer_low_write,    // $4006
	&pulse2_len_cnt_write,      // $4007

	// triangle register write handlers
	&triangle_lin_cnt_write,    // $4008
	0,
	&triangle_timer_low_write,  // $400A
	&triangle_timer_high_write, // $400B

	// noise register write handlers
	noise_env_write,            // $400C
	0, 0,
	&noise_len_cnt_write,       // $400F
	0,

	// dmc register write handlers
	&dmc_direct_load,           // $4011
	0, 0, 0,

	// status write handler
	&status_write,              // $4015
	0,

	// frame counter write handler
	&frame_counter_write        // $4017
};

void nes_apu_register_write (uint16_t address, uint8_t value)
{
	writer w;
	if ((w = *writers[address & 0x3FFF]) != NULL)
		w (value);
}


/* APU Register Readers --------------------------------------------------------------------------------------------- */

static uint8_t status_read ()
{
	uint8_t pulse_1_enabled  = pulse_1.length_counter  > 0;
	uint8_t pulse_2_enabled  = pulse_2.length_counter  > 0;
	uint8_t noise_enabled    = noise.length_counter    > 0;
	uint8_t triangle_enabled = triangle.length_counter > 0;
	uint8_t dmc_enabled      = dmc.reader.remaining    > 0;

	uint8_t ret = 0                        |
	              (registers[0x10] & 0x80) |
	              (registers[0x17] & 0x40) |
	              (dmc_enabled      << 4)  |
	              (noise_enabled    << 3)  |
	              (triangle_enabled << 2)  |
	              (pulse_2_enabled  << 1)  |
	              pulse_1_enabled;

	FRAMECOUNTER &= 0xD0; // clear frame interrupt flag
	return ret;
}

/* End APU Register Readers ----------------------------------------------------------------------------------------- */

/* APU register readers */
typedef uint8_t (*reader) ();
static reader readers[0x18] = { NULL };

uint8_t nes_apu_register_read (uint16_t address)
{
	reader r;
	if ((r = *readers[address & 0x3FFF]) != NULL)
		 return r ();
	return 0;
}

void nes_apu_reset ()
{
	// get memory location of APU registers
	__memory__ ((void**) &registers, 0x4000);
	readers[0x15] = &status_read; // TODO this is not needed to be called on each reset

	// initialize audio channels
	pulse_init    (&pulse_1,  registers,      1);
	pulse_init    (&pulse_2,  registers +  4, 2);
	triangle_init (&triangle, registers +  8);
	noise_init    (&noise,    registers + 12);
	dmc_init      (&dmc,      registers + 16);

	apucc = 0; // reset clock cycles
	frame = 0; // reset frame

 	status_write (0); // silence all channels
}

#define DEFAULT_SAMPLE_RATE 44100

/* audio_sample_rate is the playback sample rate of the application. */
static int audio_sample_rate = DEFAULT_SAMPLE_RATE;

/* sample_freq is the number of CPU cycles between sampling. */
static float sample_freq = NES_CPU_FREQ / DEFAULT_SAMPLE_RATE;

/* nsamples is the number samples in the render buffer. */
static size_t nsamples = 0;

/* samples points to the rendered samples */
static float* samples = 0;

/* struct high_pass_filter is a first order high pass filter. */
struct filter
{
	float prev_y;
	float prev_x;
	float alpha;
};

#define RC (audio_sample_rate / (2.0 * M_PI * cutoff))
#define DT 1.0

/* high_pass_filter_init initializes the high pass filter with the supplied cut off frequency */
static void high_pass_filter_init (struct filter* filter, int cutoff)
{
	filter->prev_y = 0.0;
	filter->prev_x = 0.0;
	filter->alpha = RC / (RC + DT);
}

/* high_pass_filter_pass outputs next value from x. */
float high_pass_filter_pass (struct filter* filter, float x)
{
	float y = filter->alpha * (filter->prev_y + x - filter->prev_x);
	filter->prev_y = y;
	filter->prev_x = x;
	return y;
}

/* low_pass_filter_init initializes the low pass filter the given cut off frequency. */
static void low_pass_filter_init (struct filter* filter, int cutoff)
{
	filter->prev_y = 0.0;
	filter->prev_x = 0.0;
	filter->alpha = DT / (RC + DT);
}

/* low_pass_filter_pass passes the next value based on input x. */
static float low_pass_filter_pass (struct filter* filter, float x)
{
	float y = filter->prev_y + filter->alpha * (x - filter->prev_y);
	filter->prev_y = y;
	filter->prev_x = x;
	return y;
}

/* the three filters that are used when rendering */
static struct filter filter_1;
static struct filter filter_2;
static struct filter filter_3;


void nes_audio_set_sample_rate (int rate)
{
	audio_sample_rate = rate;
	sample_freq = NES_CPU_FREQ / (float) audio_sample_rate;

	// allocate buffer for samples
	if (samples) free (samples);
	samples = malloc (rate * sizeof (float));

	// reinitialize filters
	high_pass_filter_init (&filter_1,    90);
	high_pass_filter_init (&filter_2,   440);
	low_pass_filter_init  (&filter_3, 14000);
}

/* mix will take output from all channels and return the resulting mix. */
static inline float mix ()
{
	uint8_t p1 = pulse_output    (&pulse_1);
	uint8_t p2 = pulse_output    (&pulse_2);
	uint8_t tr = triangle_output (&triangle);
	uint8_t n  = noise_output    (&noise);
	uint8_t d  = dmc_output      (&dmc);

	// we are using the less precise linear approximation
	float pulse_out = 0.00752 * (p1 + p2);
	float tnd_out = 0.00851 * tr + 0.00494 * n + 0.00335 * d;
	float output = pulse_out + tnd_out;

	return output;
}

/**
 * render takes the current output of all channels, mixes them and sends them
 * to a buffer of samples.
 */
static void render ()
{
	// get value from mixer
	float s = mix ();
	// apply filtering
 	s = high_pass_filter_pass (&filter_1, s);
	s = high_pass_filter_pass (&filter_2, s);
	s = low_pass_filter_pass (&filter_3, s);
	*(samples + nsamples) = s;
	nsamples ++;
}

void nes_audio_samples (float* smpls, size_t* size)
{
	*size = nsamples * sizeof (float);
	memcpy (smpls, samples, *size);
	nsamples = 0;
}

#define FRAME_COUNTER_RATE 240.0
static const float frame_rate = NES_CPU_FREQ / FRAME_COUNTER_RATE;

void nes_apu_step ()
{
	int f1 = apucc / frame_rate;
	int s1 = apucc / sample_freq;
	apucc ++;
	int f2 = apucc / frame_rate;
	int s2 = apucc / sample_freq;

	if ((apucc & 1) == 0) // even cycle
	{
		// clock pulse channels
		pulse_clock_timer (&pulse_1);
		pulse_clock_timer (&pulse_2);
		// clock noise channel
		noise_clock_timer (&noise);
		// clock dmc timer
		dmc_clock (&dmc);
	}
	// clock triangle
	triangle_clock_timer (&triangle);

	// step frame counter
	if (f1 != f2)
		step_frame_counter ();
	// render
	if (s1 != s2)
		render ();
}

