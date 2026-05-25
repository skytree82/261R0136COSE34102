CC = riscv64-unknown-linux-gnu-gcc

CFLAGS ?= -O2 -g -Wall -Wextra
LDFLAGS ?= -pthread

TARGET := multimodel_infinite.riscv
SRC := multimodel_infinite.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
