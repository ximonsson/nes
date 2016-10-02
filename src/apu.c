#include "nes/apu.h"
#include "nes/cpu.h"
#include <string.h>
#include <stdio.h>

/* apucc represents the APU clock cycles */
static int apucc;

/* APU registers */
static uint8_t* registers;
static uint8_t* frame_counter;

/* length_counter_table contains lookup values for length counters */
static uint8_t length_counter_table[32] =
{
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

/* duty_sequence are the different waveform sequences dependent on a pulse channels duty */
static uint8_t duty_sequence[4][8] =
{
	{ 0, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 1, 1, 0, 0, 0 },
	{ 1, 0, 0, 1, 1, 1, 1, 1 }
};

/* envelope linked to a channel */
struct envelope
{
	uint8_t* reg;
	int      divider;
	int      start;
	uint8_t  decay_level_counter;
};

/* clock_envelope clocks the supplied envelope linked to a channel */
void clock_envelope (struct envelope* env)
{
	if (env->start == 0)
	{
		if (env->divider == 0)
		{
			// load V into divider
			env->divider = (*env->reg) & 0xF;
			// clock or loop decay level counter
			if (env->decay_level_counter)
				env->decay_level_counter --;
			else if ((*env->reg) & 0x20) // loop flag is set
				env->decay_level_counter = 15;
		}
		else
			env->divider --;
	}
	else // start flag set
	{
		env->start = 0;
		env->decay_level_counter = 15;
		env->divider = (*env->reg) & 0xF;
	}
}

static uint8_t envelope_volume (struct envelope* env)
{
	if ((*env->reg) & 0x10) {
		// constant volume
		return (*env->reg) & 0xF;
	}
	else
		return env->decay_level_counter;
}

/* APU channel */
struct channel
{
	uint8_t  length_counter;
	uint8_t* reg;
	int      sweep;
	int      reload_sweep;
	int      sequencer;
	uint16_t timer;
	struct   envelope env;
};

/* clock APU channel's length counter */
static void clock_channel_length_counter (struct channel* ch)
{
	int halt = (*ch->reg) & 0x20;
	if (!halt && ch->length_counter)
	{
		ch->length_counter --;
	}
}

/* channel_envelope_write writes to a channel envelope register */
static void channel_envelope_write (struct channel* ch, uint8_t v)
{
	// restart the envelope
	ch->env.start = 1;
	ch->sequencer = 0;
}

/* channel_timer_low_write writes to the low bits of the channel's timer */
static void channel_timer_low_write (struct channel* ch, uint8_t v)
{
	// restart the envelope
	ch->env.start = 1;
	ch->sequencer = 0;
}

/* channel_reload_len_counter writes to a channels length counter / timer high register */
static void channel_reload_len_counter (struct channel* ch, uint8_t v)
{
	ch->length_counter = length_counter_table[v >> 3]; // reload length counter
	ch->env.start = 1; // restart the envelope
	ch->sequencer = 0;
}

/* channel_sweep_write writes to the channel's sweep unit */
static void channel_sweep_write (struct channel* ch, uint8_t v)
{
	ch->reload_sweep = 1; // make the sweep unit reload
}

/* channel_adjust_period adjusts the channel's period after sweeping */
static void channel_adjust_period (struct channel* ch)
{
	uint8_t shift = ch->reg[1] & 7;
	uint16_t timer = ch->reg[3] & 7;
	timer = (timer << 8) | ch->reg[2];
	uint16_t delta = timer;

	// shift the timer and change sign if needed
	delta >>= shift;
	if (ch->reg[1] & 8)
		delta = -delta;
		// TODO if pulse channel 1 there is to be -1
	timer += delta;

	// update the registers with the new timer
	ch->reg[2]  = timer;
	ch->reg[3] |= (timer >> 8) & 7;
}

/* channel_clock_sweep clock the channel's sweep unit. */
static void channel_clock_sweep (struct channel* ch)
{
	int sweep_enabled = (ch->reg[1] & 0x80) != 0;
	uint8_t period = (ch->reg[1] >> 4) & 7;
	if (ch->reload_sweep)
	{
		if (ch->sweep == 0 && sweep_enabled)
		{
			// adjust period
			channel_adjust_period (ch);
		}
		// reload sweep with period and clear the reload flag
		ch->sweep = period;
		ch->reload_sweep = 0;
	}
	else if (ch->sweep)
		ch->sweep --;
	else if (sweep_enabled)
	{
		ch->sweep = period;
		// adjust period
		channel_adjust_period (ch);
	}
}

/* channel_clock_timer clocks the channel's timer and if needed sequencer */
static void channel_clock_timer (struct channel* ch)
{
	if (ch->timer == 0)
	{
		// reload timer
		ch->timer = ch->reg[2] & 7;
		ch->timer = (ch->timer << 8) | ch->reg[3];
		// step duty waveform sequence
		ch->sequencer ++;
		ch->sequencer &= 7; // loop sequencer
	}
	else // decrement timer
		ch->timer --;
}

/* channel_output gives the next output from the channel */
static uint8_t channel_output (struct channel* ch)
{
	uint8_t duty = ch->reg[0] >> 6;
	uint16_t timer = ch->reg[3] & 7;
	timer |= (timer << 8) | ch->reg[2];

	if (duty_sequence[duty][ch->sequencer] == 0)
		return 0;
	else if (ch->length_counter == 0)
		return 0;
	else if (timer < 8 || timer > 0x7FF)
		return 0;

	// return envelope volume
	return envelope_volume (&ch->env);
}

/* new_channel creates a new channel from a memory location to the first (of four) register */
struct channel new_channel (uint8_t* reg)
{
	struct channel ch;
	ch.reg            =
	ch.env.reg        = reg;
	ch.timer          = 0;
	ch.sequencer      = 0;
	ch.length_counter = 0;
	ch.reload_sweep   = 0;
	ch.sweep          = 0;
	ch.env.divider = 0;
	ch.env.start = 0;
	ch.env.decay_level_counter = 0;

	return ch;
}

/* triangle_sequence is the 32 step sequence played by the triangle channel */
static uint8_t triangle_sequence[32] =
{
	15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
};

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

/* triangle_step_timer will step the provided triangle channel's timer */
static void triangle_step_timer (struct triangle* tr)
{
	tr->timer --;
	if (tr->timer == 0)
	{
		// clock sequencer and reload timer
		tr->timer = tr->reg[3] & 7;
		tr->timer = tr->timer << 8 | tr->reg[2];
		if (tr->linear_counter && tr->length_counter)
		{
			tr->sequencer ++;
			tr->sequencer &= 0x1F;
		}
	}
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

/* triangle_output will output the next value in the triangle channel's sequence */
static uint8_t triangle_output (struct triangle* tr)
{
	return triangle_sequence[tr->sequencer];
}

/* new_triangle_channel initializes a new triangle channel struct with registers @ base address reg */
static struct triangle new_triangle_channel (uint8_t* reg)
{
	struct triangle tr;
	tr.reg = reg;
	return tr;
}

/* noise_periods periods to load into the noise channel */
static uint16_t noise_periods[16] = {
	4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

/* noise APU */
struct noise {
	struct envelope env;
	uint16_t        shift_register;
	uint16_t        length_counter;
	uint8_t*        reg;
	int             timer;
};

/* noise_clock_lfsr clocks the noise channel's shift register */
static void noise_clock_lfsr (struct noise* noise)
{
	uint16_t sh = noise->shift_register;
	int shift = 1 + ((noise->reg[2] >> 7) * 5);
	uint8_t feedback = (sh ^ (sh >> shift)) & 1;
	noise->shift_register >>= 1;
	noise->shift_register |= feedback << 14;
}

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
	if (noise->shift_register & 1)
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

/* channel_reload_len_counter writes to a channels length counter / timer high register */
static void noise_reload_len_counter (struct noise* noise, uint8_t v)
{
	noise->length_counter = length_counter_table[v >> 3]; // reload length counter
	noise->env.start = 1; // restart the envelope
}

/* new_noise_channel returns a new noise channel */
struct noise new_noise_channel (uint8_t* reg)
{
	struct noise noise;
	noise.reg = noise.env.reg = reg;
	noise.shift_register = 1;
	noise.timer = 0;
	noise.env.divider = 0;
	noise.env.start = 0;
	noise.env.decay_level_counter = 0;
	return noise;
}

/* dmc_mem_reader represents the DMC channel's memory reader unit */
struct dmc_mem_reader
{
	uint16_t address;   // sample address
	uint16_t remaining; // bytes remaining
};

/* dmc_output_unit represents the DMC channel's output unit */
struct dmc_output_unit
{
	uint8_t rsr;       // right shift register
	uint8_t remaining; // bits remaining
	uint8_t silent;    // silence flag
	uint8_t level;     // output level
};

/* dmc_rate_index contains rates for the DMC */
static uint16_t dmc_rate_index[16] = {
	428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106,  84,  72,  54
};

/* APU Delta Modulation Channel (DMC) */
struct dmc
{
	uint8_t  buffer;       // sample buffer
	uint8_t  empty_buffer; // set if the sample buffer is empty
	int      interrupt;    // interrupt flag
	uint16_t timer;        // period
	uint8_t* reg;          // register memory location
	struct dmc_mem_reader  reader;
	struct dmc_output_unit output;
};

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
	if (dmc->reader.address == 0)
		dmc->reader.address = 0x8000;

	// decrement bytes remaining and clear the empty buffer flag
	dmc->reader.remaining --;
	dmc->empty_buffer = 0;
}

/**
 * dmc_reload_output reloads the DMC's output unit's cycle by emptying the readers buffer,
 * if not empty, else it will silence the unit.
 * The reader is looped if needed else it outputs an IRQ signal if the flag is set.
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
		// reload timer and change output level
		dmc->timer = dmc_rate_index[dmc->reg[0] & 0xF];
	}
	else
	{
		dmc->timer --;
		// update output unit
		dmc_clock_output (dmc);
	}
}

/* dmc_output outputs sample from the DMC */
static uint8_t dmc_output (struct dmc* dmc)
{
	// return the output units level
	return dmc->output.level;
}


/**
 *  APU Channels
 */
struct channel pulse_1;
struct channel pulse_2;
struct triangle triangle;
struct noise noise;
struct dmc dmc;

/* clock_envelopes clocks all the audio channel's envelope units */
static void clock_envelopes ()
{
	clock_envelope (&pulse_1.env);
	clock_envelope (&pulse_2.env);
	clock_envelope (&noise.env);
}

/* clock_sweeps clocks the pulse channel's sweep units */
static void clock_sweeps ()
{
	channel_clock_sweep (&pulse_1);
	channel_clock_sweep (&pulse_2);
}

/* clock_length_counters clocks all audio channel's length counters */
static void clock_length_counters ()
{
	clock_channel_length_counter  (&pulse_1);
	clock_channel_length_counter  (&pulse_2);
	noise_clock_length_counter    (&noise);
	triangle_clock_length_counter (&triangle);
}

/* step_frame_counter steps the frame counter/sequencer */
static void step_frame_counter ()
{
	if (apucc == 3728 || apucc == 7456 || apucc == 11185)
	{
		clock_envelopes ();
		triangle_clock_linear_counter (&triangle);
		if (apucc == 7456)
		{
			clock_length_counters ();
			clock_sweeps ();
		}
	}
	if ((*frame_counter) & 0x80)
	{
		// 5-step mode
		if (apucc == 18640)
		{
			clock_envelopes ();
			triangle_clock_linear_counter (&triangle);
			clock_length_counters ();
			clock_sweeps ();
			apucc = 0;
		}
	}
	else
	{
		// 4-step mode
		if (apucc == 14914)
		{
			clock_envelopes ();
			triangle_clock_linear_counter (&triangle);
			clock_length_counters ();
			clock_sweeps ();
			apucc = 0;
			// if (~(*frame_counter) & 0x40)
		    //		nes_cpu_signal (IRQ);
		}
	}
}


/* APU Register Writers --------------------------------------------------------------------------------------------- */

/* write to $4000 */
static void pulse1_envelope_write (uint8_t value)
{
	channel_envelope_write (&pulse_1, value);
}

/* write to $4001 */
static void pulse1_sweep_write (uint8_t value)
{
	channel_sweep_write (&pulse_1, value);
}

/* write to $4002 */
static void pulse1_timer_low_write (uint8_t value)
{
	channel_timer_low_write (&pulse_1, value);
}

/* write to $4003 */
static void pulse1_len_cnt_write (uint8_t value)
{
	channel_reload_len_counter (&pulse_1, value);
}

/* write to $4004 */
static void pulse2_envelope_write (uint8_t value)
{
	channel_envelope_write (&pulse_2, value);
}

/* write to $4005 */
static void pulse2_sweep_write (uint8_t value)
{
	channel_sweep_write (&pulse_2, value);
}

/* write to $4006 */
static void pulse2_timer_low_write (uint8_t value)
{
	channel_timer_low_write (&pulse_2, value);
}

/* write to $4007 */
static void pulse2_len_cnt_write (uint8_t value)
{
	channel_reload_len_counter (&pulse_2, value);
}

/* write to $4008 */
static void triangle_lin_cnt_write (uint8_t value)
{
	triangle.linear_counter_reload = 1;
}

/* write to $400A */
static void triangle_timer_low_write (uint8_t value)
{
	triangle.linear_counter_reload = 1;
}

/* write to $400B */
static void triangle_timer_high_write (uint8_t value)
{
	triangle_reload_length_counter (&triangle, value);
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
	if (~value & 0x10)
	{
		// silence DMC
		dmc.reader.remaining = 0;
	}
	else if (dmc.reader.remaining == 0)
	{
		// restart DMC
		dmc_reader_reload (&dmc);
	}

	if (~value & 0x08)
		noise.length_counter = 0; // silence noise

	if (~value & 0x04)
		triangle.length_counter = 0; // silence triangle

	if (~value & 0x02)
		pulse_2.length_counter = 0; // silence pulse 2

	if (~value & 0x01)
		pulse_1.length_counter = 0; // silence pulse 1

	registers[0x10] &= 0x7F; // clear the DMC interrupt flag
}

/* write to frame counter register */
static void frame_counter_write (uint8_t value)
{
	apucc = 0;
	if (value & 0x80)
	{
		clock_envelopes ();
		triangle_clock_linear_counter (&triangle);
		clock_length_counters ();
		clock_sweeps ();
	}
}

/* End APU Register Writers ----------------------------------------------------------------------------------------- */

/* APU register writers */
typedef void (*writer) (uint8_t value) ;
static writer writers[0x18] = { NULL };

void nes_apu_register_write (uint16_t address, uint8_t value)
{
	writer w;
	if ((w = *writers[address % 0x4000]) != NULL)
		w (value);
 	registers[address & 0x3FFF] = value;
}


/* APU Register Readers --------------------------------------------------------------------------------------------- */

static uint8_t status_read ()
{
	uint8_t pulse_1_enabled  = pulse_1.length_counter > 0;
	uint8_t pulse_2_enabled  = pulse_2.length_counter > 0;
	uint8_t noise_enabled    = noise.length_counter > 0;
	uint8_t triangle_enabled = triangle.length_counter > 0;
	uint8_t dmc_enabled      = dmc.reader.remaining > 0;

	uint8_t ret = (dmc_enabled << 4) | (noise_enabled << 3) | (triangle_enabled << 2) | (pulse_2_enabled << 1) || pulse_1_enabled;
	ret |= registers[0x10] & 0x80; // dmc interrupt flag
	ret |= registers[0x17] & 0x40; // frame interrupt flag

	registers[0x17] &= 0xD0; // clear frame interruot flag

	return ret;
}

/* End APU Register Readers ----------------------------------------------------------------------------------------- */

/* APU register readers */
typedef uint8_t (*reader) () ;
static reader readers[0x18] = { NULL };

uint8_t nes_apu_register_read (uint16_t address)
{
	reader r;
	if ((r = *readers[address % 0x4000]) != NULL)
		 return r ();
	return 0;
}

void nes_apu_reset ()
{
	// get memory location of APU registers
	__memory__ ((void**) &registers, 0x4000);

	frame_counter = registers + 0x17; // frame counter register

	// set registers for audio channels
	pulse_1  = new_channel (registers);
	pulse_2  = new_channel (registers + 4);
	triangle = new_triangle_channel (registers + 8);
	noise    = new_noise_channel (registers + 12);
	dmc.reg = registers + 0x10;

	readers[0x15] = &status_read;

	// pulse 1 registers
	writers[0x00] = &pulse1_envelope_write;
	writers[0x01] = &pulse1_sweep_write;
	writers[0x02] = &pulse1_timer_low_write;
	writers[0x03] = &pulse1_len_cnt_write;

	// pulse 2 registers
	writers[0x04] = &pulse2_envelope_write;
	writers[0x05] = &pulse2_sweep_write;
	writers[0x06] = &pulse2_timer_low_write;
	writers[0x07] = &pulse2_len_cnt_write;

	// triangle registers
	writers[0x08] = &triangle_lin_cnt_write;
	writers[0x0A] = &triangle_timer_low_write;
	writers[0x0B] = &triangle_timer_high_write;

	// noise registers
	writers[0x0F] = &noise_len_cnt_write;

	writers[0x11] = &dmc_direct_load;

	writers[0x15] = &status_write;
	writers[0x17] = &frame_counter_write;

	apucc = 0; // reset clock cycles

 	status_write (0); // silence all channels
}


void nes_apu_step ()
{
	apucc ++;

	// clock pulse channels
	channel_clock_timer (&pulse_1);
	channel_clock_timer (&pulse_2);

	// clock noise channel
	noise_clock_timer (&noise);

	// clock triangle twice as it runs @ CPU speed
	triangle_step_timer (&triangle);
	triangle_step_timer (&triangle);

	// clock dmc timer
	dmc_clock (&dmc);

	// step frame counter
	step_frame_counter ();
}

static float mix ()
{
	uint8_t p1 = channel_output (&pulse_1);
	uint8_t p2 = channel_output (&pulse_2);
	uint8_t tr = triangle_output (&triangle);
	uint8_t n  = noise_output (&noise);
	uint8_t d  = dmc_output (&dmc);

	// we are using the less precise linear approximation
	float pulse_out = 0.00752 * (p1 + p2);
	float tnd_out = 0.00851 * tr + 0.00494 * n + 0.00335 * d;
	return pulse_out + tnd_out;
}

void nes_apu_render ()
{
	mix ();
}
