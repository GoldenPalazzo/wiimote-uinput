CC = gcc
CFLAGS = -Wall -Wextra -Wfloat-equal -Wundef -Wshadow -Wpointer-arith \
	 -Wcast-align -Wstrict-prototypes -Wstrict-overflow \
	 -Wwrite-strings -Waggregate-return -Wcast-qual \
	 -Wswitch-default -Wswitch-enum -Wconversion \
	 -Wunreachable-code
LDFLAGS = -ludev

SRC_FOLDER = src
BUILD_FOLDER = build
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst $(SRC_FOLDER)/%.c,$(BUILD_FOLDER)/%.o,$(SOURCES))
BIN = $(BUILD_FOLDER)/wiimote-uinput

.PHONY: all debug clean

all: CFLAGS += -O2
all: $(BIN)

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_FOLDER)/%.o: $(SRC_FOLDER)/%.c | $(BUILD_FOLDER)
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS += -g -O0
debug: $(BIN)
clean:
	rm -f $(OBJECTS) $(BIN)

$(BUILD_FOLDER):
	mkdir -p $(BUILD_FOLDER)

