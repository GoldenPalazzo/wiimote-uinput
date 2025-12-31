CC = gcc
CFLAGS = -Wall -Wextra -Werror

BINNAME = wii2xb.bin
SOURCES = src/main.c

$(BINNAME): $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $<


.PHONY: all debug clean
all: CFLAGS += -O2
all: $(BINNAME)
debug: CFLAGS += -g -O0
debug: $(BINNAME)
clean:
	rm -f $(BINNAME)
