#include "nes/ppu.h"
#include "nes/cpu.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// # of cycles the PPU runs per frame
#define PPUCC_PER_FRAME PPUCC_PER_SCANLINE * SCANLINES_PER_FRAME

// Screen and sprite sizes
#define SCREEN_W      256
#define SCREEN_H      240
#define SPRITE_HEIGHT   8
#define SPRITE_WIDTH    8
#define TILE_SIZE      16

// PPUCTRL
#define GENERATE_NMI 0x80

// PPUSTATUS
#define VBLANK          0x80
#define SPRITE_ZERO_HIT 0x40
#define SPRITE_OVERFLOW 0x20

// OAM
#define PRIMARY_OAM_SIZE   64
#define SECONDARY_OAM_SIZE  8

// VRAM memory map
#define VRAM_SIZE   16 << 10
#define PATTERN_RAM 0x0000
#define NAMETABLE_0 0x2000
#define NAMETABLE_1 0x2400
#define NAMETABLE_2 0x2800
#define NAMETABLE_3 0x2C00
#define PALETTE_RAM 0x3F00

/* Status flags */
enum _flags
{
	w            = 0x1,
	nmi_occurred = 0x2,
	odd_frame    = 0x4,
};
static int flags;

/* Pointer to registers in RAM */
static uint8_t  *ppu_registers;

/* PPU VRAM */
static uint8_t   vram[VRAM_SIZE];
static uint8_t   vram_buffer;

/* pixel data to be displayed */
static uint8_t   screen[SCREEN_W * SCREEN_H * 3];
static uint8_t   screen_buffer[SCREEN_W * SCREEN_H * 3];

/* OAM data */
static uint8_t   primary_oam[PRIMARY_OAM_SIZE * 4];
static uint8_t   secondary_oam[SECONDARY_OAM_SIZE];

/* PPU scrolling registers. */
static uint16_t  t;
static uint16_t  v;
static uint8_t   x;

/* PPU clock cycles */
static int ppucc;


// DEBUGGERS --------------------------------------------------------------------------------------
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
	printf (" [v = x%.4X, t = x%.4X, x = %d, w = %d] coarse X = %2d, coarse Y = %2d, fine X = %d, fine Y = %d\n",
		v, t, x, flags & w, v & 0x1F, (v >> 5) & 0x1F, x, (v >> 12) & 7);
}
// DEBUGGERS ---------------------------------------------------------------------------------


void nes_ppu_reset ()
{
	// init ppu registers
	__memory__ ((void**) &ppu_registers, PPU_REGISTER_MEM_LOC);
	// ppu_registers[PPUSTATUS] = 0xA0;

	// reset flags
	flags = 0;
	ppucc = SCREEN_H * PPUCC_PER_SCANLINE - 1; // we start in vblank

	// reset scrolling and VRAM
	t = v = x = 0;
	vram_buffer = 0;

	// clear screen
	memset (screen, 0, SCREEN_W * SCREEN_H * 3);
	memset (screen_buffer, 0, SCREEN_W * SCREEN_H * 3);
}


void nes_ppu_load_vram (void* data)
{
	memcpy (vram, data, VRAM_SIZE);
}


/* chr_rom_banks contains indices for which CHR bank is currently loaded into
 * CHR ROM 0, respectively CHR ROM 1. */
static int chr_rom_banks[2] = {0, 1};

/* chr_rom points to all banks of CHR data */
static uint8_t* chr_rom;

void nes_ppu_load_chr_rom (void* data)
{
	chr_rom = data;
	chr_rom_banks[0] = 0;
	chr_rom_banks[1] = 1;
}

void nes_ppu_switch_chr_rom_bank (int bank, int chr_bank)
{
	chr_rom_banks[chr_bank] = bank;
}

/* chr_address returns the correct address within CHR data array
 * we assume that address is < $2000 */
static inline uint8_t* chr_p (uint16_t address)
{
	int bank = address / 0x1000;
	uint16_t offset = address % 0x1000;
	return chr_rom + chr_rom_banks[bank] * 0x1000 + offset;
}

/* macro to get a uint8_t* to the correct address within CHR ROM */
#define CHR(addr) chr_rom + chr_rom_banks[addr / 0x1000] * 0x1000 + addr % 0x1000

/**
 *   read to CHR ROM making sure we read from the correct bank.
 */
static inline uint8_t chr_read (uint16_t address)
{
	return *(CHR (address));
}

/**
 *   write to CHR ROM making sure we write to the correct bank.
 */
static inline void write_chr (uint16_t address, uint8_t value)
{
	*(CHR (address)) = value;
}


void nes_ppu_load_oam_data (void* data)
{
	uint8_t oamaddr = ppu_registers[OAMADDR];
	// compute offset to make sure we wrap data
	uint16_t offset = (PRIMARY_OAM_SIZE << 2) - oamaddr;
	memcpy (primary_oam + oamaddr, data, offset);
	memcpy (primary_oam, data + offset, oamaddr);
}


/*
 *  Mirroring ----------------------------------------------------------------------------------
 */

/**
 *  mirror_vertically mirrors the input address vertically
 *    AB
 *    AB
 */
static uint16_t mirror_vertically (uint16_t address)
{
	return address &= 0x27FF; // TODO not sure this one works as intended
}

/**
 *  mirror_horizontally mirrors the input address horizontally
 *    AA
 *    BB
 */
static uint16_t mirror_horizontally (uint16_t address)
{
	if (address >= NAMETABLE_2)
		address = NAMETABLE_1 + (address & 0x3ff);
	else if (address >= NAMETABLE_1)
		address -= 0x400;
	return address;
}

/**
 *  mirror_four_screen mirrors the input address using four screen mirroring
 *    AB
 *    CD
 */
static uint16_t mirror_four_screen (uint16_t address)
{
	// TODO implement
	return address;
}

static uint16_t mirror_single0 (uint16_t address)
{
	return NAMETABLE_0 + (address % 0x0400);
}

static uint16_t mirror_single1 (uint16_t address)
{
	return NAMETABLE_1 + (address % 0x0400);
}

/* mirror function - takes an address and returns the respective one in VRAM respecting mirroring mode */
typedef uint16_t (*address_mirrorer) (uint16_t) ;

/* address_mirrorers lists mirroring functions, their index are their modes */
static address_mirrorer address_mirrorers[5] =
{
	mirror_horizontally,
	mirror_vertically,
	mirror_single0,
	mirror_single1,
	mirror_four_screen,
};

/* mirror_mode contains the current mirroring mode of the nametables */
static nes_ppu_mirroring_mode mirror_mode;

/* nametable_mirrorer is the current mirroring function set by the mapper */
static address_mirrorer nametable_mirrorer;

/**
 *  mirror_address returns the mirrored location of address within VRAM.
 *  The function assumes the caller is asking for an address within nametable space,
 *  as it is the only part that is mirrored.
 */
static uint16_t mirror_address (uint16_t address)
{
	// $3xxx -> $2xxxx
	return nametable_mirrorer (address & 0x2FFF);
}


void nes_ppu_set_mirroring (nes_ppu_mirroring_mode mode)
{
	mirror_mode = mode;
	nametable_mirrorer = *address_mirrorers[mirror_mode];
}


/*
 *  Register Writers ---------------------------------------------------------------------------
 */

/* Write > PPUCTRL $(2000) */
static void write_ppuctrl (uint8_t value)
{
	ppu_registers[PPUCTRL] = value;
	t = (t & 0xF3FF) | ((value & 3) << 10);
}

/* Write > PPUMASK $(2001) */
static void write_ppumask (uint8_t value)
{
	ppu_registers[PPUMASK] = value;
}

/* Write > OAMADDR $(2002) */
static void write_oamaddr (uint8_t value)
{
	ppu_registers[OAMADDR] = value;
}

/* Write > OAMDATA $(2004) */
static void write_oamdata (uint8_t value)
{
	uint8_t *oamaddr = ppu_registers + OAMADDR;
	primary_oam[*oamaddr] = value;
	*oamaddr += 1;
}

/* Write > PPUSCROLL $(2005) */
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

/* Write > PPUADDR $(2006) */
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

/* Write > PPUDATA $(2007) */
static void write_ppudata (uint8_t value)
{
	if (v >= PALETTE_RAM) // palettes
	{
		// make sure to mirror
		uint16_t delta = v % 4 == 0 ? 0x10 : 0x20;
		for (int i = PALETTE_RAM + v % delta; i < VRAM_SIZE; i += delta)
			vram[i] = value;
	}
	else if (v >= NAMETABLE_0) // nametables
	{
		// read mirrored address
		vram[mirror_address (v)] = value;
	}
	else // patterns (no mirroring)
	{
		//vram[v] = value;
		write_chr (v, value);
	}

	v += 1 + ((ppu_registers[PPUCTRL] & 0x04) >> 2) * 31;
	v &= 0x3FFF;
}

/* Writer functions to PPU registers. Perform necessary modifications to PPU. */
static void (*register_writers[8])(uint8_t) =
{
	&write_ppuctrl,
	&write_ppumask,
	0, // &write_ppustatus,
	&write_oamaddr,
	&write_oamdata,
	&write_ppuscroll,
	&write_ppuaddr,
	&write_ppudata
};

/*
 *  End Register Writers --------------------------------------------------------------------------
 */


/* Write value to PPU register by calling the writer function associated to it. */
void nes_ppu_register_write (nes_ppu_register reg, uint8_t value)
{
	uint8_t *ppustatus = ppu_registers + PPUSTATUS;
	*ppustatus = (*ppustatus & 0xE0) | (value & 0x1F);
	if (*register_writers[reg])
		(*register_writers[reg])(value);
}


/*
 *  Register Readers ------------------------------------------------------------------------------
 */

/* Read < PPUSTATUS $(2002) */
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

/* Read < OAMDATA $(2004) */
static uint8_t read_oamdata ()
{
	return primary_oam[ppu_registers[OAMADDR]];
}

/* Read < PPUDATA $(2007) */
static uint8_t read_ppudata ()
{
	uint8_t ret = vram_buffer;
	if (v >= PALETTE_RAM) // palette read
	{
		ret = vram[v];
		vram_buffer = vram[mirror_address (v - 0x1000)];
	}
	else if (v >= NAMETABLE_0) // nametable read
	{
		vram_buffer = vram[mirror_address (v)];
	}
	else // v < $2000: pattern read
	{
		//vram_buffer = vram[v];
		vram_buffer = chr_read (v);
	}
	// increment and wrap
	v += 1 + ((ppu_registers[PPUCTRL] & 0x04) >> 2) * 31;
	v &= 0x3FFF;
	return ret;
}

// PPU register read callbacks
static uint8_t (*register_readers[8])() =
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

/*
 *  End Register Readers ------------------------------------------------------------------------
 */


uint8_t nes_ppu_register_read (nes_ppu_register reg)
{
	if (*register_readers[reg])
		return (*register_readers[reg])();
	return 0;
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
 *  tiles contains color information for the next 2 tiles (16 pixels).
 *  From low to high bits, each 4-bit value is the palette index of the pixel on the current
 *  scanline for dot 0 to 15.
 *  To fetch for example color(dot X) = (tiles >> (4 * X)) & 0xF.
 */
static uint64_t tiles = 0;

/**
 *  load_tile loads the next tile in relevance to the current scrolling (value of loopy_V).
 *  The old 32-bit data for the previous tile is shifted out, and the new tile data is loaded
 *  into the high bits of tiles.
 */
static void load_tile ()
{
	// shift out previous tile
	tiles >>= 32;

	// fine Y
	uint8_t y = (v >> 12) & 7;
	// get nametable byte
	uint8_t nametable = vram[mirror_address (0x2000 | (v & 0x0FFF))];
	// flagged background pattern tile table in ppuctrl
	uint8_t table = (ppu_registers[PPUCTRL] & 0x10) >> 4;
	// tile data
	uint16_t tile = table * 0x1000 + nametable * 0x10 + y;
	uint8_t low = chr_read (tile); // vram[tile];
	uint8_t high = chr_read (tile + 8); // vram[tile + 8];

	// get attribute byte and compute background palette for this tile
	uint8_t attribute =
		vram[mirror_address (0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07))];
	uint8_t palette = (attribute >> (((v >> 4) & 4) | (v & 2))) & 3;

	uint64_t colors = 0;
	for (int i = 0; i < 8; i ++)
	{
		colors <<= 4;
		colors |= (palette << 2) | ((high & 1) << 1) | (low & 1);
		low >>= 1;
		high >>= 1;
	}
	tiles |= colors << 32;
}

/**
 *  Get background color (index in palette) and pixel value at x and y on the screen.
 *  The pixel value can be used to see if this pixel is transparent by making the check pixel == 0.
 */
static uint8_t background_color (int dot, uint8_t* pixel)
{
	uint8_t color = (tiles >> (((dot & 7) + x) << 2)) & 0xF;
	*pixel = color & 3;
	return vram[0x3F00 | color];
}

/**
 *  Get sprite color and pixel value at x and y coordinates within the sprite @ index within
 *  primary OAM.
 *  The value set to pixel can be used to determin if the pixel is transparent or not, by
 *  making the check pixel == 0.
 */
static uint8_t sprite_color (int index, int x, int y, uint8_t *pixel)
{
	uint8_t *sprite = primary_oam + (index << 2);
	int h = SPRITE_HEIGHT + ((ppu_registers[PPUCTRL] & 0x20) >> 2);

	int pattern;
	if (h == 8) // 8x8 mode
		pattern = ((ppu_registers[PPUCTRL] & 0x08) << 9) + (sprite[1] << 4);
	else // 8x16 mode
		pattern = ((sprite[1] & 1) << 12) + ((sprite[1] & 0xFE) << 4) + ((y << 1) & 0x10);

	// the formulas below solve flipping of sprites, but still keeps it correct if not flipped
	y &= 7;
	x += ((sprite[2] >> 6) & 1) * (7 - 2 * x);
	y += ((sprite[2] >> 7) & 1) * (7 - 2 * y);

	// get pixel (0, 1 or 2?) (within palette?)
	uint8_t low  = chr_read (pattern + y); //vram[pattern + y];
	uint8_t high = chr_read (pattern + y + 8); //vram[pattern + y + 8];
	*pixel = ((low >> (7 - x)) & 1) | (((high >> (7 - x)) << 1) & 2);
	return vram[0x3F10 | (sprite[2] & 0x3) << 2 | (*pixel)];
}

/**
 * set_pixel_color renders to virtual screen @ (x, y) the color pointed out by pindex from
 * the palette.
 */
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


#define SHOW_SPRITES    (ppu_registers[PPUMASK] & 0x10)
#define SHOW_BACKGROUND (ppu_registers[PPUMASK] & 0x08)
/**
 *  render_pixel wil compute what color the pixel @ (x, y) will have and render it to the
 *  virtual screen.
 *  Computation of pixel is done by taking into account background/sprite priorities and
 *  looking at nametables/sprites.
 */
static void inline render_pixel (int x, int y)
{
	uint8_t bg_color   = 0,
	        sprite_clr = 0,
	        bg_pixel   = 0;

	// default palette index to background clear color
	uint8_t color = vram[0x3F00];
	uint8_t mask = ppu_registers[PPUMASK];

	// background
	if (SHOW_BACKGROUND && (x >= 8 || (mask & 2)))
	{
		bg_color = background_color (x, &bg_pixel);
		if (bg_pixel)
			color = bg_color;
	}
	// sprite
	if (SHOW_SPRITES && (x >= 8 || (mask & 4)))
	{
		uint8_t *sprite;
		int sindex, x_off, y_off;
		uint8_t pixel;

		// loop through secondary OAM
		for (int i = 0; i < SECONDARY_OAM_SIZE; i ++)
		{
			// no more sprites in 2nd OAM
			if ((sindex = secondary_oam[i]) == 0xFF)
				break;

			sprite = primary_oam + (sindex << 2);

			if (sprite[3] > x || x - sprite[3] > 8)
				continue; // sprite too far away

			x_off = x - sprite[3];
			y_off = y - sprite[0] - 1; // TODO understand why -1
			sprite_clr = sprite_color (sindex, x_off, y_off, &pixel);
			if (pixel)
			{
				// sprite found
				if (bg_pixel)
				{
					// also found non-zero BG
					if (sindex == 0 && x != 255) // sprite zero hit
						ppu_registers[PPUSTATUS] |= SPRITE_ZERO_HIT;

					if ((sprite[2] & 0x20) == 0x20) // bg priority
						continue;
				}
				color = sprite_clr;
				break;
			}
		}
	}
	// render color
	set_pixel_color (x, y, color);
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
static void sprite_evaluation ()
{
	int scanln = ppucc / PPUCC_PER_SCANLINE;
 	uint8_t* y = primary_oam;
	int i = 0;
	int h = 8 + ((ppu_registers[PPUCTRL] & 0x20) >> 2);
	int n = 0;

	// loop through primary OAM and store indices to sprites in secondary OAM.
	for (i = 0; i < PRIMARY_OAM_SIZE && n < SECONDARY_OAM_SIZE; i ++, y += 4)
	{
		if ((*y) <= scanln && scanln < (*y) + h)
		{
			// sprite in range
			secondary_oam[n] = i;
			n ++;
		}
	}
	// in case we found 8, check for overflow
	if (n == SECONDARY_OAM_SIZE)
	{
		for (int m = 0; i < PRIMARY_OAM_SIZE; i ++, y += 4)
		{
			y += m;
			if ((*y) <= scanln && scanln < (*y) + h)
			{
				ppu_registers[PPUSTATUS] |= SPRITE_OVERFLOW;
				break; // jag tror man ska gå igenom allt, men fuck it.
			}
			m ++; // sprite overflow bug
			m &= 3;
		}
	}
}

// RENDERING_ENABLED returns wether either background or sprites are to be rendered
#define RENDERING_ENABLED (ppu_registers[PPUMASK] & 0x18)

/**
 * tick makes the PPU turn one cycle, and making any status updates,
 * such as odd/event frame flag, by doing so.
 */
static void inline tick ()
{
	// compute scanline and dot we are currently rendering
	int scanln = ppucc / PPUCC_PER_SCANLINE;
	int dot = ppucc % PPUCC_PER_SCANLINE;

	if (RENDERING_ENABLED &&
		scanln == SCANLINES_PER_FRAME - 1 &&
		dot == PPUCC_PER_SCANLINE - 2 &&
		(flags & odd_frame))
	{
		// If we are rendering, odd frames are one cycle shorter.
		// This is done by skipping the last cycle of the frame.
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

	int scanln = ppucc / PPUCC_PER_SCANLINE;              // line
	int pre_scanln = scanln == (SCANLINES_PER_FRAME - 1); // line 261
	int visible_scanln = scanln < SCREEN_H;               // lines 0 -> 239

	int dot = ppucc % PPUCC_PER_SCANLINE;     // dot
	int visible_dot = dot >= 1 && dot <= 256; // dot 1 -> 256, remember: first dot is idle

	if (RENDERING_ENABLED)
	{
		if (visible_dot && visible_scanln)
			render_pixel (dot - 1, scanln); // render pixel to screen

		// Scroll
		if (visible_scanln || pre_scanln)
		{
			if (pre_scanln && dot >= 280 && dot <= 304)
			{
				// copy vertical bits from t to v
				v = (v & ~0x7BE0) | (t & 0x7BE0);
			}
			else if ((dot >= 321 || visible_dot) && (dot & 7) == 0)
			{
				// load next tile before scrolling
				load_tile ();
				// scroll horizontally on each 8th dot between dots 328 and 256 of the next scanline
				increment_horizontal_scroll ();
				if (dot == 256) // scroll vertically
					increment_vertical_scroll ();
			}
			else if (dot == 257) // dot 257
			{
				// copy horizontal bits from t to v
				v = (v & ~0x041F) | (t & 0x041F);
				// clear sprite data from the last scanline
				memset (secondary_oam, 0xFF, SECONDARY_OAM_SIZE);
				if (visible_scanln)
					sprite_evaluation (); // evaluate sprites for next scanline
			}
		}
	}

	if (dot == 1)
	{
		if (scanln == 241)
		{
			// Set VBLANK and generate NMI
			ppu_registers[PPUSTATUS] |= VBLANK;
			flags |= nmi_occurred;
			if (ppu_registers[PPUCTRL] & GENERATE_NMI)
			{
				// TODO i have seen implementations where they delay this a number of cycles to fix timings.
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
