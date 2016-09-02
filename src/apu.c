#include "nes/apu.h"
#include "nes/cpu.h"


// f = CPU / (16 * (t + 1))
// t = (CPU / (16 * f)) - 1


/* APU Register Writers --------------------------------------------------------------------------------------------- */

void status_write (uint8_t value)
{

}

/* End APU Register Writers ----------------------------------------------------------------------------------------- */



void nes_apu_register_write (uint16_t address, uint8_t value)
{

}


/* APU Register Readers --------------------------------------------------------------------------------------------- */

uint8_t status_read ()
{
	return 0;
}

/* End APU Register Readers ----------------------------------------------------------------------------------------- */


uint8_t nes_apu_register_read (uint16_t address)
{
	return 0;
}



typedef uint8_t (*reader) () ;
reader readers[12];


typedef void (*writer) (uint8_t value) ;
writer writers[12];


void nes_apu_reset ()
{

}






void nes_apu_step ()
{

}


void nes_apu_render ()
{

}
