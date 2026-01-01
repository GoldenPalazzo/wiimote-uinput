CC = gcc
CFLAGS = -Wall -Wextra

BINS = wii2xb.bin
SOURCES = src/main.c src/wiimote.c src/spoofer.c

.PHONY: all debug clean

all: CFLAGS += -O2
all: $(BINS)

wii2xb.bin: $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

debug: CFLAGS += -g -O0
debug: $(BINS)
clean:
	rm -f $(BINS)
