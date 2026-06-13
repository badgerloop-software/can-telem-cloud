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
    src/encoder.c \
    src/db_watcher.c \
    src/writer.c \
    src/serial_radio.c \
    src/gnss_reader.c \
    third_party/cJSON.c

TARGET = can_telem
LDFLAGS ?= -lcurl -lsqlite3 -lpthread -lm

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
