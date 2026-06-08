/***************************************************************************
--                      tiny_chess - uart
--
--           Copyright (C) 2026 By Ulrik Hørlyk Hjort
--
-- Permission is hereby granted, free of charge, to any person obtaining
-- a copy of this software and associated documentation files (the
-- "Software"), to deal in the Software without restriction, including
-- without limitation the rights to use, copy, modify, merge, publish,
-- distribute, sublicense, and/or sell copies of the Software, and to
-- permit persons to whom the Software is furnished to do so, subject to
-- the following conditions:
--
-- The above copyright notice and this permission notice shall be
-- included in all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
-- EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
-- MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
-- NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
-- LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
-- OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
-- WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
-- ***************************************************************************/

#include "uart.h"

#include <avr/io.h>

void uart_init(uint16_t ubrr) {
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)ubrr;
    UCSR0B = (1u << RXEN0) | (1u << TXEN0);
    UCSR0C = (1u << UCSZ01) | (1u << UCSZ00);
}

void uart_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
    }

    while ((UCSR0A & (1u << UDRE0)) == 0u) {
    }

    UDR0 = (uint8_t)c;
}

void uart_puts(const char *text) {
    while (*text != '\0') {
        uart_putc(*text++);
    }
}

void uart_puts_P(PGM_P text) {
    char c;

    while ((c = (char)pgm_read_byte(text++)) != '\0') {
        uart_putc(c);
    }
}

char uart_getc(void) {
    while ((UCSR0A & (1u << RXC0)) == 0u) {
    }

    return (char)UDR0;
}

uint8_t uart_readline(char *buffer, uint8_t size) {
    uint8_t len = 0;

    if (size == 0u) {
        return 0;
    }

    for (;;) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_puts("\n");
            break;
        }

        if ((c == '\b' || c == 127) && len > 0u) {
            len--;
            uart_puts("\b \b");
            continue;
        }

        if (len + 1u >= size) {
            continue;
        }

        buffer[len++] = c;
        uart_putc(c);
    }

    buffer[len] = '\0';
    return len;
}
