#ifndef _NES_H_
#define _NES_H_

/**
 * nes_step_callback registers a callback to be called each time we step the CPU.
 * This can be used to step the mapper if needed.
 */
void nes_step_callback (void (*cb) ()) ;

#endif /* _NES_H_ */
