#include "nes/apu.h"
#include "nes/cpu.h"
#include <string.h>


/* length_counter represents a channel's Length Counter unit */
struct length_counter
{
	uint8_t counter;
};

/* length_counter_clock clocks a provided length counter */
static void clock_length_counter (struct length_counter* lc)
{
	if (lc->counter)
		lc->counter --;
}

/* struct divider is linked to one of the channels */
struct divider
{
	int period;
	int counter;
};

/* clock_divider clocks the divider supplied */
static void clock_divider (struct divider* div)
{
	if (div->counter == 0)
	{
		div->counter = div->period;
		// generate output clock?
	}
	else
	{
		div->counter --;
	}
}

/* envelope linked to a channel */
struct envelope
{
	uint8_t*       reg;
	struct divider divider;
	int            start_flag;
	int            decay_level_counter;
};

/* clock_envelope clocks the supplied envelope linked to a channel */
void clock_envelope (struct envelope* env)
{
	if (env->start_flag == 0)
	{
		if (env->divider.counter == 0)
		{
			// load V into divider
			env->divider.counter = (*env->reg) & 0xF;
			// clock or loop decay level counter
			if (env->decay_level_counter)
				env->decay_level_counter --;
			else if ((*env->reg) & 0x20)
				env->decay_level_counter = 15;
		}
		clock_divider (&env->divider);
	}
	else
	{
		env->start_flag = 0;
		env->decay_level_counter = 15;
	}
}

/* APU channel */
struct channel
{
	struct envelope env;
	struct length_counter len_counter;
};


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
static uint8_t* dmc;
/* frame counter register */
static uint8_t* frame_counter = 0;


/* APU Register Writers --------------------------------------------------------------------------------------------- */

/* write to status register */
static void status_write (uint8_t value)
{

}

/* write to frame counter register */
static void frame_counter_write (uint8_t value)
{

}

/* End APU Register Writers ----------------------------------------------------------------------------------------- */

/* APU register writers */
typedef void (*writer) (uint8_t value) ;
static writer writers[0x17] = { NULL };
/*{
	&status_write,
	&frame_counter_write
};*/

void nes_apu_register_write (uint16_t address, uint8_t value)
{
	writer w;
	if ((w = *writers[address % 0x4000]) != NULL)
		w (value);
}


/* APU Register Readers --------------------------------------------------------------------------------------------- */

static uint8_t status_read ()
{
	return 0;
}


static uint8_t frame_counter_read ()
{
	return 0;
}

/* End APU Register Readers ----------------------------------------------------------------------------------------- */

/* APU register readers */
typedef uint8_t (*reader) () ;
static reader readers[24] = { 0 };
/*{
	&status_read,
	&frame_counter_read
};*/


uint8_t nes_apu_register_read (uint16_t address)
{
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
	pulse_1.env.reg  = registers;
	pulse_2.env.reg  = registers +  4;
	triangle.env.reg = registers +  8;
	noise.env.reg    = registers + 12;

	apucc = 0; // reset clock cycles
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
		}
		else if (apucc == 7456)
		{
			clock_length_counter (&pulse_1.len_counter);
			clock_length_counter (&pulse_2.len_counter);
			clock_length_counter (&triangle.len_counter);
			clock_length_counter (&noise.len_counter);
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
