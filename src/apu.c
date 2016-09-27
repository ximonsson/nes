#include "nes/apu.h"
#include "nes/cpu.h"
#include <string.h>
#include <stdio.h>


/* length_counter_table contains lookup values for length counters */
static uint8_t length_counter_table[32] = {
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

/* envelope linked to a channel */
struct envelope
{
	uint8_t* reg;
	int      divider;
	int      start_flag;
	int      decay_level_counter;
};

/* clock_envelope clocks the supplied envelope linked to a channel */
void clock_envelope (struct envelope* env)
{
	if (env->start_flag == 0)
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
		env->start_flag = 0;
		env->decay_level_counter = 15;
		env->divider = (*env->reg) & 0xF;
	}
}

/* APU channel */
struct channel
{
	uint8_t  length_counter;
	uint8_t* reg;
	int      sweep;
	int      reload_sweep;
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
	ch->env.start_flag = 1;
}

/* channel_timer_low_write writes to the low bits of the channel's timer */
static void channel_timer_low_write (struct channel* ch, uint8_t v)
{
	// restart the envelope
	ch->env.start_flag = 1;
}

/* channel_reload_len_counter writes to a channels length counter / timer low register */
static void channel_reload_len_counter (struct channel* ch, uint8_t v)
{
	ch->length_counter = length_counter_table[v >> 3]; // reload length counter
	ch->env.start_flag = 1; // restart the envelope
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

/* channel_output gives the next output from the channel */
static uint8_t channel_output ()
{
	return 0;
}

/* new_channel creates a new channel from a memory location to the first (of four) register */
struct channel new_channel (uint8_t* reg)
{
	struct channel ch;
	ch.reg = ch.env.reg = reg;
	return ch;
}

/**
 *  APU Channels
 */
struct channel pulse_1;
struct channel pulse_2;
struct channel triangle;
struct channel noise;

// f = CPU / (16 * (t + 1))
// t = (CPU / (16 * f)) - 1

/* APU registers */
static uint8_t* registers;
static uint8_t* frame_counter;


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

/* write to status register */
static void status_write (uint8_t value)
{
	if (~value & 0x10)
	{
		// silence DMC
	}
	if (~value & 0x08)
		noise.length_counter = 0; // silence noise

	if (~value & 0x04)
		triangle.length_counter = 0; // silence triangle

	if (~value & 0x02)
		pulse_2.length_counter = 0; // silence pulse 2

	if (~value & 0x01)
		pulse_1.length_counter = 0; // silence pulse 1
}

/* write to frame counter register */
static void frame_counter_write (uint8_t value)
{

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
	uint8_t ret = (noise_enabled << 3) | (triangle_enabled << 2) | (pulse_2_enabled << 1) || pulse_1_enabled;
	// TODO add DMC enabled flag and interrupt status
	return ret;
}

/* End APU Register Readers ----------------------------------------------------------------------------------------- */

/* APU register readers */
typedef uint8_t (*reader) () ;
static reader readers[0x18] = { NULL };

uint8_t nes_apu_register_read (uint16_t address)
{
//	printf ("reading APU register @ $%.4X\n", address);
	reader r;
	if ((r = *readers[address % 0x4000]) != NULL)
		 return r ();
	return 0;
}

/* apucc represents the APU clock cycles */
static int apucc;

void nes_apu_reset ()
{
	// get memory location of APU registers
	__memory__ ((void**) &registers, 0x4000);

	frame_counter = registers + 0x17; // frame counter register

	// set registers for audio channels
	pulse_1  = new_channel (registers);
	pulse_2  = new_channel (registers + 4);
	triangle = new_channel (registers + 8);
	noise    = new_channel (registers + 12);

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

	writers[0x15] = &status_write;
	writers[0x17] = &frame_counter_write;

	apucc = 0; // reset clock cycles

// 	nes_apu_register_write (0x4015, 0); // silence all channels
}

/* step_frame_counter steps the frame counter/sequencer */
static void step_frame_counter ()
{
	if ((*frame_counter) & 0x80)
	{
		// 5-step mode
		if (apucc == 3728)
		{
			// step envelopes
			clock_envelope (&pulse_1.env);
			clock_envelope (&pulse_2.env);
			clock_envelope (&triangle.env);
			clock_envelope (&noise.env);

			channel_clock_sweep (&pulse_1);
			channel_clock_sweep (&pulse_2);
		}
		else if (apucc == 7456)
		{
			clock_channel_length_counter (&pulse_1);
			clock_channel_length_counter (&pulse_2);
			clock_channel_length_counter (&triangle);
			clock_channel_length_counter (&noise);
		}
		// and more ...
	}
	else
	{
		// 4-step mode
	}
}


void nes_apu_step ()
{
	apucc ++;
	step_frame_counter ();
}


void nes_apu_render ()
{

}
