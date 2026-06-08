MCU := atmega328p
F_CPU := 16000000UL
TARGET := tiny_chess_asm
BAUD := 9600

CC := avr-gcc
OBJCOPY := avr-objcopy
SIZE := avr-size
AVRDUDE := avrdude

CFLAGS := -mmcu=$(MCU) -DF_CPU=$(F_CPU) -DBAUD=$(BAUD) -Os -std=c11 -Wall -Wextra -Werror
LDFLAGS := -mmcu=$(MCU)

SRC_C := src/main.c src/chess.c src/uart.c
SRC_S := src/fast_math.S src/fast_attack.S
OBJ := $(SRC_C:.c=.o) $(SRC_S:.S=.o)

PORT ?= /dev/ttyACM1
PROGRAMMER ?= arduino
UPLOAD_BAUD ?= 115200

.PHONY: all hex size flash clean

all: $(TARGET).elf $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	$(SIZE) -C --mcu=$(MCU) $<

flash: $(TARGET).hex
	$(AVRDUDE) -p $(MCU) -c $(PROGRAMMER) -P $(PORT) -b $(UPLOAD_BAUD) -D -U flash:w:$<:i

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).hex
