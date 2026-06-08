/***************************************************************************
--                      tiny_chess - main
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

#include "chess.h"
#include "uart.h"

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>

#define UART_BAUD 9600UL
#define ENGINE_DEPTH 2u
#define INPUT_SIZE 16u

static void print_board(const chess_board_t *board) {
    int8_t rank;

    uart_puts_P(PSTR("\n  +-----------------+\n"));
    for (rank = 7; rank >= 0; rank--) {
        uint8_t file;

        uart_putc((char)('1' + rank));
        uart_puts_P(PSTR(" | "));
        for (file = 0; file < 8u; file++) {
            uint8_t square = (uint8_t)(rank * 8 + file);
            uart_putc(chess_piece_char(board->squares[square]));
            uart_putc(' ');
        }
        uart_puts_P(PSTR("|\n"));
    }
    uart_puts_P(PSTR("  +-----------------+\n    a b c d e f g h\n"));
}

static void print_turn(const chess_board_t *board) {
    uart_puts_P(PSTR("Turn: "));
    uart_puts_P(board->side_to_move == CHESS_WHITE ? PSTR("White") : PSTR("Black"));
    if (chess_is_in_check(board, board->side_to_move)) {
        uart_puts_P(PSTR(" (check)"));
    }
    uart_puts_P(PSTR("\n"));
}

static void print_help(void) {
    uart_puts_P(PSTR("Commands: e2e4, e7e8q, new, board, help\n"));
}

static void announce_result(const chess_board_t *board) {
    if (chess_has_legal_move(board)) {
        return;
    }

    if (chess_is_in_check(board, board->side_to_move)) {
        uart_puts_P(board->side_to_move == CHESS_WHITE ? PSTR("Checkmate. Black wins.\n")
                                                       : PSTR("Checkmate. White wins.\n"));
    } else {
        uart_puts_P(PSTR("Stalemate.\n"));
    }
}

int main(void) {
    chess_board_t board;
    char input[INPUT_SIZE];

    /* Hardware UART keeps the UI simple and avoids pulling in the Arduino runtime. */
    uart_init((uint16_t)((F_CPU / (16UL * UART_BAUD)) - 1UL));
    chess_reset(&board);

    uart_puts_P(PSTR("\nTiny Uno Chess\n"));
    uart_puts_P(PSTR("You play White over serial at 9600 baud.\n"));
    print_help();

    for (;;) {
        chess_move_t move;
        char move_text[6];

        print_board(&board);
        print_turn(&board);
        announce_result(&board);

        if (!chess_has_legal_move(&board)) {
            uart_puts_P(PSTR("> "));
            uart_readline(input, INPUT_SIZE);

            if (strcmp(input, "new") == 0) {
                chess_reset(&board);
            } else if (strcmp(input, "help") == 0) {
                print_help();
            }

            continue;
        }

        if (board.side_to_move == CHESS_WHITE) {
            uart_puts_P(PSTR("> "));
            uart_readline(input, INPUT_SIZE);

            if (strcmp(input, "help") == 0) {
                print_help();
                continue;
            }

            if (strcmp(input, "new") == 0) {
                chess_reset(&board);
                continue;
            }

            if (strcmp(input, "board") == 0) {
                continue;
            }

            if (!chess_parse_move(&board, input, &move)) {
                uart_puts_P(PSTR("Illegal move.\n"));
                continue;
            }

            chess_apply_move(&board, move);
            continue;
        }

        /* The engine always plays Black in this first cut. */
        uart_puts_P(PSTR("Engine thinking...\n"));
        if (!chess_best_move(&board, ENGINE_DEPTH, &move)) {
            continue;
        }

        chess_format_move(move, move_text);
        uart_puts_P(PSTR("Engine plays: "));
        uart_puts(move_text);
        uart_puts_P(PSTR("\n"));
        chess_apply_move(&board, move);
    }
}
