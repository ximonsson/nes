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
	w = 0x1,
	nmi_occurred = 0x2,
	odd_frame = 0x4,
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


// DEBUGGERS ----------------------------------------------------------------------------------------------------
void print_pattern_table (uint16_t addr)
{
	printf ("Pattern @ x%4X [v = %4X, t = %4X, x = %d]\n ", addr, v, t, x);
	uint8_t* pattern = vram + addr;

	for (int y = 0; y < 8; y ++)
	{
		uint8_t low = *pattern;
		uint8_t high = *(pattern + 8);
		for (int x = 0; x < 8; x ++)
		{
			uint8_t b = (low >> (7 - x) & 1) | ((high >> (7 - x) & 1) << 1);
			printf ("%d", b);
		}
		printf ("\n ");
	}
	printf ("\n");
}


void print_scroll ()
{
	printf ("coarse X = %2d, coarse Y = %2d, fine X = %d, fine Y = %d\n", v & 0x1F, (v >> 5) & 0x1F, x, (v >> 12) & 7);
}
// DEBUGGERS ----------------------------------------------------------------------------------------------------


void nes_ppu_init ()
{
	// init ppu registers
	__memory__ ((void **) &ppu_registers, PPU_REGISTER_MEM_LOC);
	for (uint8_t *p = ppu_registers; p - ppu_registers < 0x2000; p += 8)
		p[PPUSTATUS] = 0xA0;

	// reset flags
	flags = 0;
	ppucc = SCREEN_H * PPUCC_PER_SCANLINE - 1; // we start in vblank

	// reset scrolling and VRAM
	// memset (vram, 0, VRAM_SIZE);
	t = v = x = 0;
	vram_buffer = 0;

	// clear OAM
	// memset (primary_oam,   0, 64 * 4);
	// memset (secondary_oam, 0, 8 * sizeof (int));

	// clear screen
	memset (screen, 0, SCREEN_W * SCREEN_H * 3);
	memset (screen_buffer, 0, SCREEN_W * SCREEN_H * 3);
}


static void write_ppuctrl (uint8_t value)
{
	ppu_registers[PPUCTRL] = value;
	t = (t & 0xF3FF) | ((value & 3) << 10);
}


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
		t = (t & 0xFFE0) | (value >> 3);
		x = value & 7;
	}
	else
	{
		t = (t & 0xC1F) | ((value & 7) << 12) | ((value & 0xF8) << 2);
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
	// v %= 0x4000;
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
	uint8_t ret = ppu_registers[PPUSTATUS] & 0x7F;
	// set bit 7 to old status of NMI occurred
	if (flags & nmi_occurred)
		ret |= 1 << 7;
	// reset address latch for PPUSCROLL and NMI occured.
	flags &= ~(w | nmi_occurred);
	// clear VBLANK flag
	ppu_registers[PPUSTATUS] &= ~VBLANK;

	return ret;
}

static uint8_t read_oamdata ()
{
	return primary_oam[ppu_registers[OAMADDR]];
}

static uint8_t read_ppudata ()
{
	uint8_t ret = 0;
	if ((v % 0x4000) < 0x3F00) // normal read
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
	// v %= 0x4000;
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


/**
 *  Increase current vertical scroll.
 */
static inline void increment_vertical_scroll ()
{
	if ((v & 0x7000) != 0x7000) // fine Y < 7
		v += 0x1000; // increment fine Y
	else
	{
		v &= ~0x7000; // set fine Y = 0
		uint16_t y = (v & 0x03E0) >> 5; // coarse Y
		if (y == 29)
		{
			y  = 0;      // coarse Y = 0
			v ^= 0x0800; // switch vertical nametable
		}
		else if (y == 31)
			y = 0; // coarse Y = 0
		else
			y ++; // increment coarse Y
		v = (v & ~0x03E0) | (y << 5); // insert coarse Y back into V
	}
}


/**
 *  Increase current horizontal scroll.
 */
static inline void increment_horizontal_scroll ()
{
	if ((v & 0x1F) == 0x1F) // coarse X = 31
	{
		v &= ~0x001F; // set coarse X = 0
		v ^=  0x0400; // switch horizontal nametable
	}
	else
		v ++; // increment coarse X
}


/**
 *  Get background color and pixel value at x and y on the screen.
 */
static uint8_t background_color (int dot, uint8_t *pixel)
{
	// print_scroll();
	dot = (dot & 7) + x;
	// fine Y
	uint8_t y = (v >> 12) & 7;
	// get nametable byte
	uint8_t tile = vram[0x2000 | (v & 0x0FFF)];
	// flagged background pattern tile table in ppuctrl
	uint8_t table = (ppu_registers[PPUCTRL] & 0x10) >> 4;
	// pattern location
	uint16_t pattern = table * 0x1000 + tile * 0x10 + y;
	// get attribute byte
	uint8_t attribute = vram[0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07)];
	// palette index
	uint8_t palette = (attribute >> (((1 - (y & 1)) << 2) + ((dot & 1) << 1))) & 3;
	// compute color index within palette
	*pixel = ((vram[pattern] >> (7 - dot)) & 1) | ((vram[pattern + 8] >> (6 - dot)) & 2);

	return vram[0x3F00 + (palette << 2) + (*pixel)];
}


/**
 *  Get sprite color and pixel value at x and y coordinates within the sprite.
 */
static void sprite_color (int sprite_index, int x, int y, uint8_t *pixel, uint8_t *color)
{
	int pattern;
	uint8_t *sprite = primary_oam + sprite_index * 4;
	int h = SPRITE_HEIGHT + ((ppu_registers[PPUCTRL] & 0x20) ? SPRITE_HEIGHT : 0);

	if (h == 8) // 8x8 mode
		pattern = ((ppu_registers[PPUCTRL] & 0x08) >> 3) * 0x1000 + sprite[1] * 0x10;
	else // 8x16 mode
		pattern = (sprite[1] & 1) * 0x1000 + (sprite[1] & ~1) * 0x10;

	x += ((sprite[2] & 0x40) >> 6) * (7 - 2 * x);
	y += ((sprite[2] & 0x80) >> 7) * (h - 1 - 2 * y);
	// get pixel (0, 1 or 2?) (within palette?)
	// fan det är mycket magi som händer här, jag kommmer inte ihåg hur jag gjorde detta....
	*pixel = ((vram[pattern + y] >> (7 - x)) & 1) | (((vram[pattern + y + h] >> (7 - x)) << 1) & 2);
	*color = vram[0x3F10 + (sprite[2] & 0x3) * 4 + (*pixel)];
}


static void inline set_pixel_color (int x, int y, uint8_t pindex)
{
	static const uint8_t palette[64][3] = {
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

	uint8_t* p = screen + (y * SCREEN_W + x) * 3;
	memcpy (p, palette[pindex], 3);

	static const float mod = 0xFF / 7;
	for (int i = 0; i < 3; i ++)
		*(p + i) *= mod;
}


static void inline render_pixel (int x, int y)
{
	uint8_t bg_pixel = 0,
	        sprite_pixel = 0,
	        bg_color = 0,
	        sprite_clr = 0;

	// default palette index to background clear color
	uint8_t palette_index = vram[0x3F00];

	// background
	if (!((ppu_registers[PPUMASK] & 0x08) == 0 || ((ppu_registers[PPUMASK] & 0x02) == 0 && y < 8 && x < 8)))
	{
		bg_color = background_color (x, &bg_pixel);
		if (bg_pixel != 0)
			palette_index = bg_color;
	}
	// sprites
	if (!((ppu_registers[PPUMASK] & 0x10) == 0 || ((ppu_registers[PPUMASK] & 0x4) == 0 && y < 8 && x < 8)))
	{
		uint8_t *sprite;
		int si, x_off, y_off;
		uint8_t tmp;

		// loop through secondary OAM
		for (int i = 0; i < SECONDARY_OAM_SIZE && sprite_pixel == 0; i ++)
		{
			// no more sprites in 2nd OAM
			if ((si = secondary_oam[i]) == 0xFF)
				break;

			sprite = primary_oam + si * 4;

			// sprite too far away
			if (!(sprite[3] <= x && x - sprite[3] < 8))
				continue;

			x_off = x - sprite[3];
			y_off = y - sprite[0];
			sprite_color (si, x_off, y_off, &tmp, &sprite_clr);
			if (tmp != 0)
			{
				// sprite found
				if (bg_pixel != 0)
				{
					if (si == 0) // sprite zero hit
						ppu_registers[PPUSTATUS] |= SPRITE_ZERO_HIT;

					if ((sprite[2] & 0x20) == 0x20) // bg priority
						continue;
				}
				sprite_pixel = tmp;
				palette_index = sprite_clr;
			}
		}
	}
	// render color
	set_pixel_color (x, y, palette_index);
}


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


/**
 *  Perform sprite evaluation for the current scanline.
 *  Loads into secondary OAM up to 8 sprites that are to be rendered.
 *  Also sets the sprite overflow flag in case more than 8 are found.
 */
static void sprite_evaluation (int scanline)
{
	int i = 0;
	int y = 0;
	int m = 0;
	int h = SPRITE_HEIGHT + ((ppu_registers[PPUCTRL] & 0x20) ? SPRITE_HEIGHT : 0);
	int sprites = 0;
	memset (secondary_oam, 0xFF, SECONDARY_OAM_SIZE);
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
	if (sprites == SECONDARY_OAM_SIZE)
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
 *  tick makes the PPU turn one cycle, and making any status updates by doing so.
 */
static void inline tick ()
{
	// compute if rendering is enabled
	int rendering_enabled = (ppu_registers[PPUMASK] & 0x18) != 0;

	// compute scanline and dot we are currently rendering
	int scanln = ppucc / PPUCC_PER_SCANLINE;
	int dot = ppucc % PPUCC_PER_SCANLINE;

	if ((flags & odd_frame) && rendering_enabled && scanln == SCANLINES_PER_FRAME - 1 && dot == PPUCC_PER_SCANLINE - 2)
	{
		// If we are rendering, odd frames are one cycle shorter. This is done by skipping the last cycle of the frame.
		ppucc ++;
	}
	// tick PPU and update dot and scanline
	ppucc ++;
	ppucc %= PPUCC_PER_FRAME;
	if (ppucc == 0)
	{
		// New frame
		flags ^= odd_frame; // toggle odd frame flag
		render ();
	}
}


void nes_ppu_step ()
{
	tick (); // go forward one cycle

	// compute if rendering is enabled
	int rendering_enabled = (ppu_registers[PPUMASK] & 0x18) != 0;

	int scanln = ppucc / PPUCC_PER_SCANLINE;
	int pre_scanln = scanln == (SCANLINES_PER_FRAME - 1); // line 261
	int visible_scanln = scanln < SCREEN_H;               // lines 0 -> 239

	int dot = ppucc % PPUCC_PER_SCANLINE;
	int visible_dot = dot >= 1 && dot <= SCREEN_W; // dot 1 -> 256, remember: first dot is idle

	if (rendering_enabled)
	{
		if (visible_dot && visible_scanln)
			render_pixel (dot - 1, scanln); // render pixel to screen

		// TODO revise if we need to implement correct fetching of nametable bytes
		//      so far render_pixel and background_color computes that for us.

		// Scroll !!
		if (visible_scanln || pre_scanln)
		{
			if (pre_scanln && dot >= 280 && dot <= 304)
			{
				// copy vertical bits from t to v
				v = (v & ~0x7BE0) | (t & 0x7BE0);
			}
			if ((dot >= 328 || visible_dot) && (dot & 7) == 0)
			{
				// scroll horizontally on each 8th dot between dots 328 and 256 of the next scanline
				// print_scroll ();
				increment_horizontal_scroll ();
				if (dot == 256) // scroll vertically
					increment_vertical_scroll ();
			}
			else if (dot == 257) // dot 257
			{
				// copy horizontal bits from t to v
				v = (v & ~0x041F) | (t & 0x041F);
				if (visible_scanln)
					sprite_evaluation (scanln + 1); // evaluate sprites for next scanline
			}
		}
	}

	if (dot == 1)
	{
		if (scanln == SCREEN_H + 1) // line 241
		{
			// Set VBLANK and generate NMI
			ppu_registers[PPUSTATUS] |= VBLANK;
			flags |= nmi_occurred;
			if (ppu_registers[PPUCTRL] & GENERATE_NMI)
			{
				// TODO i have seen implementations where they delay this a number of cycles
				nes_cpu_signal (NMI);
			}
		}
		else if (pre_scanln)
		{
			// clear vblank sprite overflow and sprite #0 hit
			flags &= ~nmi_occurred;
			ppu_registers[PPUSTATUS] &= ~(VBLANK | SPRITE_ZERO_HIT | SPRITE_OVERFLOW);
		}
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
