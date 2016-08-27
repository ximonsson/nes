CC = gcc
AR = ar

BIN     = bin
BUILD   = build
SRC_DIR = src
LIBS    = libs
EXEC    = $(BIN)/nes

SRC  = cpu.c io.c nes.c ppu.c
OBJS = $(addprefix $(BUILD)/, $(SRC:.c=.o))
LIB  = $(LIBS)/libnes.a

CFLAGS += -Wall

ifdef DEBUG
CFLAGS += -g3
else
CFLAGS += -O3
endif

INCLUDES = -I./include

LDFLAGS += -L./libs -lSDL2 -lGLESv2 -lpthread -lnes

ARCMD = rcs


all: lib app

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
