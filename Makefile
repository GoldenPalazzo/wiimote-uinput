CC = gcc
CFLAGS = -Wall -Wextra

BINS = wii2xb.bin wiimote.bin
SOURCES = src/main.c


.PHONY: all debug clean

all: CFLAGS += -O2
all: $(BINS)

wii2xb.bin: $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $<

wiimote.bin: src/wiimote.c
	$(CC) $(CFLAGS) -o $@ $<



debug: CFLAGS += -g -O0
debug: $(BINS)
clean:
	rm -f $(BINS)
