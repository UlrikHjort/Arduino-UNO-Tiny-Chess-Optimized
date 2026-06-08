/***************************************************************************
--                      tiny_chess - chess
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

#ifndef CHESS_H
#define CHESS_H

#include <stdint.h>

#define CHESS_WHITE 0u
#define CHESS_BLACK 8u

typedef uint16_t chess_move_t;

typedef struct {
    uint8_t squares[64];
    uint8_t side_to_move;
    uint8_t castling;
    int8_t ep_square;
    uint8_t halfmove_clock;
} chess_board_t;

void chess_reset(chess_board_t *board);
void chess_apply_move(chess_board_t *board, chess_move_t move);
uint8_t chess_best_move(const chess_board_t *board, uint8_t depth, chess_move_t *best_move);
uint8_t chess_parse_move(const chess_board_t *board, const char *text, chess_move_t *move);
void chess_format_move(chess_move_t move, char *out);
uint8_t chess_has_legal_move(const chess_board_t *board);
uint8_t chess_is_in_check(const chess_board_t *board, uint8_t side);
char chess_piece_char(uint8_t piece);

#endif
