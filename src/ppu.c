#include "nes/ppu.h"
#include "nes/cpu.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>


#define PPUCC_PER_FRAME     PPUCC_PER_SCANLINE * SCANLINES_PER_FRAME

#define SCREEN_W            256
#define SCREEN_H            240
#define VRAM_SIZE           16 << 10

#define GENERATE_NMI        0x80
#define SPRITE_OVERFLOW     0x20
#define SPRITE_ZERO_HIT     0x40
#define VBLANK              0x80

#define SPRITE_HEIGHT       8
#define SPRITE_WIDTH        8
#define SECONDARY_OAM_SIZE  8
#define PRIMARY_OAM_SIZE    64


enum _flags
{
	w = 0x1
};
static int flags;

// static uint8_t *ppuctrl;
// static uint8_t *ppumask;
// static uint8_t *ppustatus;
// static uint8_t *oamaddr;
// static uint8_t *oamdata;
// static uint8_t *ppuscroll;
// static uint8_t *ppuaddr;
// static uint8_t *ppudata;

static uint8_t  *ppu_registers;
static uint8_t   vram[VRAM_SIZE];
static uint8_t   vram_buffer;
static uint8_t   screen[SCREEN_W * SCREEN_H * 3];
static uint8_t   screen_buffer[SCREEN_W * SCREEN_H * 3];
static uint8_t   primary_oam[64 * 4];
static uint8_t   secondary_oam[8];

/**
 *  PPU scrolling registers.
 */
static uint16_t  t;
static uint16_t  v;
static uint8_t   x;

static int ppucc;
static int render_buf = 0;


void nes_ppu_init ()
{
	// init ppu registers
	__memory__ ((void **) &ppu_registers, PPU_REGISTER_MEM_LOC);
	for (uint8_t *p = ppu_registers; p - ppu_registers < 0x2000; p += 8)
		p[PPUSTATUS] = 0xA0;

	// reset flags
	flags = 0;
	ppucc = 0;

	// reset scrolling and VRAM
	memset (vram, 0, VRAM_SIZE);
	t = v = x = 0;
	vram_buffer = 0;

	// clear OAM
	memset (primary_oam,   0, 64 * 4);
	memset (secondary_oam, 0, 8 * sizeof (int));

	// clear screen
	memset (screen, 0, SCREEN_W * SCREEN_H * 3);
	memset (screen_buffer, 0, SCREEN_W * SCREEN_H * 3);
}


static void write_ppuctrl (uint8_t value)
{
	t = (t & 0xF3FF) | ((value & 3) << 10);
}

/*
static void write_ppumask (uint8_t value)
{
// nada
}

static void write_ppustatus (uint8_t value)
{
// nada
}

static void write_oamaddr (uint8_t value)
{
// nada
}
*/

static void write_oamdata (uint8_t value)
{
	uint8_t *oamaddr = ppu_registers + OAMADDR;
	primary_oam[*oamaddr] = value;
	*oamaddr += 1;
}

static void write_ppuscroll (uint8_t value)
{
	if (~flags & w)
	{
		t = (t & 0xFFE0) | ((value & 0xF8) >> 3);
		x = value & 7;
	}
	else
	{
		t = (t & 0xC1F) | ((value & 7) << 12) | ((value & 0xC0) << 8) | ((value & 0x38) << 5);
	}
	flags ^= w;
}

static void write_ppuaddr (uint8_t value)
{
	if (~flags & w)
	{
		t = (t & 0x00FF) | ((value & 0x3F) << 8);
	}
	else
	{
		v  = t = (t & 0xFF00) | value;
		// v %= 0x4000;
	}
	flags ^= w;
}

static void write_ppudata (uint8_t value)
{
	// mirror palettes
	if (v >= 0x3F00)
	{
		uint16_t delta = v % 4 == 0 ? 0x10 : 0x20;
		for (int i = 0x3F00 + v % delta; i < 0x4000; i += delta)
			vram[i] = value;
	}
	// mirror nametables
	else if (v >= 0x2000 && v % 0x1000 < 0xF00)
	{
		vram[0x2000 + v % 0x1000] =
		vram[0x3000 + v % 0x1000] = value;
	}
	// unmirrored write (patterns)
	else
		vram[v] = value;

	v += 1 + ((ppu_registers[PPUCTRL] & 0x04) >> 2) * 31;
	v %= 0x4000;
}


// ppu register write callbacks
static void (*register_write_callbacks[8])(uint8_t) =
{
	&write_ppuctrl,
	0, // &write_ppumask,
	0, // &write_ppustatus,
	0, // &write_oamaddr,
	&write_oamdata,
	&write_ppuscroll,
	&write_ppuaddr,
	&write_ppudata
};

/**
 * Write value to PPU register and call any callbacks associated
 *  to it.
 */
void nes_ppu_register_write (nes_ppu_register reg, uint8_t value)
{
	uint8_t *ppustatus = ppu_registers + PPUSTATUS;
	*ppustatus = (*ppustatus & 0xE0) | (value & 0x1F);
	if (*register_write_callbacks[reg])
		(*register_write_callbacks[reg])(value);
}


/*
 *  READ PPU REGISTERS CALLBACKS
 */
static uint8_t read_ppustatus ()
{
	uint8_t ret = ppu_registers[PPUSTATUS];
	ppu_registers[PPUSTATUS] &= ~VBLANK;
	flags &= ~w;
	return ret;
}

static uint8_t read_oamdata ()
{
	return primary_oam[ppu_registers[OAMADDR]];
}

static uint8_t read_ppudata ()
{
	uint8_t ret = 0;
	if (v < 0x3F00) // normal read
	{
		ret = vram_buffer;
		vram_buffer = vram[v];
	}
	else // palette read
	{
		ret = vram[v];
		vram_buffer = vram[v - 0x1000];
	}
	// increment and wrap
	v += 1 + ((ppu_registers[PPUCTRL] & 0x04) >> 2) * 31;
	v %= 0x4000;
	return ret;
}

// PPU register read callbacks
static uint8_t (*register_read_callbacks[8])() =
{
	0,
	0,
	&read_ppustatus,
	0,
	&read_oamdata,
	0,
	0,
	&read_ppudata
};

/**
 *  Read value from register.
 *  Calls the necessary callbacks associated to reads.
 */
uint8_t nes_ppu_register_read (nes_ppu_register reg)
{
	if (*register_read_callbacks[reg])
		return (*register_read_callbacks[reg])();
	else
		return ppu_registers[reg];
}


/*
 *  MIRRORING
 */

/* mirroring contains the current mirroring mode. Defaults to HORIZONTAL */
static nes_ppu_mirroring_mode mirroring = HORIZONTAL;

/* mirror_func will point to the current mirroring function used. */
static void (*mirror_func) (int *, int *, int *);

/* mirror_horizontally applies horizontal mirroring */
static void mirror_horizontally (int* x, int* y, int* nametable)
{
	*y %= SCREEN_H * 2;
	*nametable += (*y) / SCREEN_H * 0x400;
}

/* mirror_vertically applies vertical mirroring */
static void mirror_vertically (int *x, int *y, int *nametable)
{
	*x %= SCREEN_W * 2;
	*nametable += (*x) / SCREEN_W * 0x400;
}

/* mirror_four_screen applies four screen mirroring */
static void mirror_four_screen (int *x, int *y, int *nametable)
{
	// TODO implement
}

void nes_ppu_set_mirroring_mode (nes_ppu_mirroring_mode _mirroring)
{
	mirroring = _mirroring;
	switch (mirroring)
	{
		case HORIZONTAL:
			mirror_func = &mirror_horizontally;
		break;

		case VERTICAL:
			mirror_func = &mirror_vertically;
		break;

		case FOUR_SCREEN:
			mirror_func = &mirror_four_screen;
		break;
	}
}

/**
 *  Get background color and pixel value at x and y on the screen.
 */
static void background_color (int _x, int _y, uint8_t *pixel, uint8_t *color)
{
	uint8_t ppuctrl      = ppu_registers[PPUCTRL];
	int     nametable_   = 0x2000 | (v & 0x0FFF);

	// get palette from attribute table
	uint16_t attribute_  = 0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07);
	uint8_t  b           = vram[attribute_];
	uint8_t  palette     = ((b >> ((((_y & 0x1F) >> 4) << 2) + (((_x & 0x1F) >> 4) << 1))) & 3) << 2;

	// get the byte representing the 8x8 tile we are in
	b = vram[nametable_];
	_x &= 7;
	_y &= 7;

	// get color index
	int pattern = ((ppuctrl & 0x10) >> 4) * 0x1000 + b * 0x10;
	*pixel = ((vram[pattern + _y] >> (7 - _x)) & 1) | ((vram[pattern + _y + 8] >> (7 - _x) << 1) & 2);
	*color = vram[0x3F00 + palette + *pixel];
}

/**
 *  Get sprite color and pixel value at x and y coordinates within the sprite.
 */
static void sprite_color (int sprite_index, int x, int y, uint8_t *pixel, uint8_t *color)
{
	int pattern;
	uint8_t *sprite = primary_oam + sprite_index * 4;
	// int      h      = SPRITE_HEIGHT + ((ppu_registers[PPUCTRL] & 0x20) >> 5) * SPRITE_HEIGHT;
	int h = SPRITE_HEIGHT + (ppu_registers[PPUCTRL] & 0x20) ? SPRITE_HEIGHT : 0;

	if (h == 8) // 8x8 mode
		pattern = ((ppu_registers[PPUCTRL] & 0x08) >> 3) * 0x1000 + sprite[1] * 0x10;
	else // 8x16 mode
		pattern = (sprite[1] & 1) * 0x1000 + (sprite[1] & ~1) * 0x10;

	x += ((sprite[2] & 0x40) >> 6) * (7 - 2 * x);
	y += ((sprite[2] & 0x80) >> 7) * (h - 1 - 2 * y);
	// get pixel (0, 1 or 2?) (within palette?)
	// fan det är mycket magi som händer här, jag kommmer inte ihåg hur jag gjorde detta....
	*pixel = ((vram[pattern + y] >> (7 - x)) & 1) | (((vram[pattern + y + h] >> (7 - x)) << 1) & 2);
	*color = vram[0x3F10 + (sprite[2] & 0x3) * 4 + *pixel];
}

/**
 *  Perform sprite evaluation for the current scanline.
 *  Loads into secondary OAM up to 8 sprites that are to be rendered.
 *  Also sets the sprite overflow flag in case more than 8 are found.
 */
static void sprite_evaluation ()
{
	memset (secondary_oam, 0xFF, SECONDARY_OAM_SIZE);
	int scanline = ppucc / PPUCC_PER_SCANLINE - BLANK_SCANLINES;
	int sprites  = 0;
	int i        = 0;
	int y        = 0;
	int m        = 0;
	int h        = SPRITE_HEIGHT + (ppu_registers[PPUCTRL] & 0x20) ? SPRITE_HEIGHT : 0;
	// loop through primary OAM and store indices to sprites in secondary OAM.
	for (i = ppu_registers[OAMADDR]; i < PRIMARY_OAM_SIZE * 4 && sprites < SECONDARY_OAM_SIZE; i += 4)
	{
		y = primary_oam[i];
		if (y <= scanline && scanline < y + h)
		{
			secondary_oam[sprites] = i / 4;
			sprites ++;
		}
	}
	// in case we found 8, check for overflow
	if (sprites == 8)
	{
		for (m = 0; i < PRIMARY_OAM_SIZE * 4; i += 4)
		{
			y = primary_oam[i + m];
			if (y <= scanline && scanline < y + h)
			{
				ppu_registers[PPUSTATUS] |= SPRITE_OVERFLOW;
				break; // jag tror man ska gå igenom allt, men fuck it.
			}
			m ++; // sprite overflow bug
			m %= 4;
		}
	}
}

/**
 *  Increase current vertical scroll.
 */
static inline void increment_vertical_scroll ()
{
	if ((v & 0x7000) != 0x7000)
		v += 0x1000;
	else
	{
		v &= ~0x7000;
		uint16_t y = (v & 0x03E0) >> 5;
		if (y == 29)
		{
			y  = 0;
			v ^= 0x0800;
		}
		else if (y == 31)
			y = 0;
		else
			y ++;
		v = (v & ~0x03E0) | (y << 5);
	}
}

/**
 *  Increase current horizontal scroll.
 */
static inline void increment_horizontal_scroll ()
{
	if ((v & 0x1F) == 0x1F)
	{
		v &= ~0x001F;
		v ^=  0x0400;
	}
	else
		v ++;
}

static uint8_t palette[64][3] = {
	{3,3,3}, {0,1,4}, {0,0,6}, {3,2,6},
	{4,0,3}, {5,0,3}, {5,1,0}, {4,2,0},
	{3,2,0}, {1,2,0}, {0,3,1}, {0,4,0},
	{0,2,2}, {0,0,0}, {0,0,0}, {0,0,0},
	{5,5,5}, {0,3,6}, {0,2,7}, {4,0,7},
	{5,0,7}, {7,0,4}, {7,0,0}, {6,3,0},
	{4,3,0}, {1,4,0}, {0,4,0}, {0,5,3},
	{0,4,4}, {0,0,0}, {0,0,0}, {0,0,0},
	{7,7,7}, {3,5,7}, {4,4,7}, {6,3,7},
	{7,0,7}, {7,3,7}, {7,4,0}, {7,5,0},
	{6,6,0}, {3,6,0}, {0,7,0}, {2,7,6},
	{0,7,7}, {4,4,4}, {0,0,0}, {0,0,0},
	{7,7,7}, {5,6,7}, {6,5,7}, {7,5,7},
	{7,4,7}, {7,5,5}, {7,6,4}, {7,7,2},
	{7,7,3}, {5,7,2}, {4,7,3}, {2,7,6},
	{4,6,7}, {6,6,6}, {0,0,0}, {0,0,0}
};

/**
 * render renders the virtual screen to buffer.
 */
static void render ()
{
	memcpy (screen_buffer, screen, SCREEN_W * SCREEN_H * 3);
}

const uint8_t* nes_screen_buffer ()
{
	return screen_buffer;
}

static void set_pixel_color (int x, int y, uint8_t pindex)
{
	// screen[y * SCREEN_W + x] = pindex;
	uint8_t* p = screen + (y * SCREEN_W + x) * 3;
	memcpy (p, palette[pindex], 3);

	float mod = 0xFF / 7;
	for (int i = 0; i < 3; i ++)
		*(p + i) *= mod;
}


void nes_ppu_render (int pixels)
{
	uint8_t sprite_pixel, bg_pixel,
	        sprite_clr, bg_color;

	int x, y;
	int stop = 0;
	int rendering_enabled = (ppu_registers[PPUMASK] & 0x18) != 0;

	render_buf += pixels;
	for ( ; render_buf > 0 && stop == 0; render_buf --)
	{
		x = ppucc % PPUCC_PER_SCANLINE;
		y = ppucc / PPUCC_PER_SCANLINE - BLANK_SCANLINES;

		// HBLANK or VBLANK
		if (y < 0 || x >= SCREEN_W)
		{
			if (x == 0 && y == -BLANK_SCANLINES + 2)
			{
				ppu_registers[PPUSTATUS] |= VBLANK;
				if (ppu_registers[PPUCTRL] & GENERATE_NMI)
					nes_cpu_signal (NMI);
				stop = 1;
			}
			// increment v if rendering is enabled
			else if (x == SCREEN_W && rendering_enabled)
				increment_vertical_scroll ();

			goto loop;
		}

		if (x == 0)
		{
			// start of visual screen
			if (y == 0)
			{
				render ();
				if (rendering_enabled)
					v = t;
				ppu_registers[PPUSTATUS] &= ~(VBLANK | SPRITE_ZERO_HIT);
				stop = 1;
				// goto loop;
			}
			else if (rendering_enabled)
				v = (v & ~0x041F) | (t & 0x041F);

			sprite_evaluation ();
		}
		// increment v horizontally
		else if (x % 8 == 0 && rendering_enabled && y != 0)
			increment_horizontal_scroll ();

		// no point continuing the iteration from here if rendering is disabled
		if (!rendering_enabled)
			goto loop;

		bg_pixel = sprite_pixel = 0;

		// background
		if (!((ppu_registers[PPUMASK] & 0x08) == 0 || ((ppu_registers[PPUMASK] & 0x02) == 0 && y < 8 && x < 8)))
		{
			background_color (x, y, &bg_pixel, &bg_color);
			if (bg_pixel != 0)
				set_pixel_color (x, y, bg_color);
		}
		// sprites
		if (!((ppu_registers[PPUMASK] & 0x10) == 0 || ((ppu_registers[PPUMASK] & 0x4) == 0 && y < 8 && x < 8)))
		{
			uint8_t *sprite;
			int      j, x_off, y_off;
			uint8_t  tmp;
			for (int i = 0; i < SECONDARY_OAM_SIZE && sprite_pixel == 0; i ++)
			{
				if (secondary_oam[i] == 0xFF)
					break;

				j  = secondary_oam[i];
				sprite = primary_oam + j * 4;

				if (!(sprite[3] <= x && x - sprite[3] < 8))
					continue;

				x_off = x - sprite[3];
				y_off = y - sprite[0];

				sprite_color (j, x_off, y_off, &tmp, &sprite_clr);
				// sprite found
				if (tmp != 0)
				{
					if (bg_pixel != 0)
					{
						if (j == 0)
							ppu_registers[PPUSTATUS] |= SPRITE_ZERO_HIT;
						// bg priority
						if ((sprite[2] & 0x20) == 0x20)
							continue;
					}
					sprite_pixel = tmp;
					set_pixel_color (x, y, sprite_clr);
				}
			}
		}
		// copy backdrop color
		if (bg_pixel == 0 && sprite_pixel == 0)
			set_pixel_color (x, y, vram[0x3F00]);

		loop:
		ppucc ++;
		ppucc %= PPUCC_PER_FRAME;
	}
}


void nes_ppu_load_vram (void *data)
{
	memcpy (vram, data, VRAM_SIZE);
}


void nes_ppu_load_oam_data (void *data)
{
	uint8_t  oamaddr = ppu_registers[OAMADDR];
	uint16_t offset  = PRIMARY_OAM_SIZE * 4 - oamaddr;
	memcpy (primary_oam + oamaddr, data, offset);
	memcpy (primary_oam, data + offset, oamaddr);
}
