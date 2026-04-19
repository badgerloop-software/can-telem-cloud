CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CFLAGS  += -Isrc -Ithird_party

SRC = \
    src/main.c \
    src/config.c \
    src/influx.c \
    src/format_loader.c \
    src/can_reader.c \
    src/decoder.c \
    src/writer.c \
    third_party/cJSON.c

TARGET = can_telem
LDFLAGS ?= -lcurl

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
