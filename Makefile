CC = gcc
AR = ar

BIN     = bin
BUILD   = build
SRC_DIR = src
LIBS    = lib
EXEC    = $(BIN)/nes

SRC  = cpu.c io.c nes.c ppu.c apu.c mmc1.c uxrom.c mmc3.c mmc2.c cnrom.c
OBJS = $(addprefix $(BUILD)/, $(SRC:.c=.o))
LIB  = $(LIBS)/libnes.a

CFLAGS += -Wall

ifdef DEBUG
CFLAGS += -g3
else
CFLAGS += -O3
endif

ifdef VERBOSE
CFLAGS += -DVERBOSE
endif

INCLUDES = -I./include

LDFLAGS += -L./$(LIBS) -lSDL2 -lnes -lpulse -lpulse-simple

ifdef GLES
LDFLAGS += -lGLESv2
CFLAGS += -DGLES
else
LDFLAGS += -lGL
CFLAGS += -DGL_GLEXT_PROTOTYPES
endif

ARCMD = rcs


all: lib exec

lib: $(LIB)

exec: $(EXEC)

clean:
	rm -rf $(OBJS) $(LIB) $(EXEC)

.PHONY: $(EXEC)

$(LIB): $(OBJS)
	@mkdir -p $(@D)
	$(AR) $(ARCMD) $@ $^

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(EXEC):
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ app/main.c $(LDFLAGS)
