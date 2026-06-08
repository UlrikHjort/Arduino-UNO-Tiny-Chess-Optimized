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

#include "chess.h"
#include "fast_attack.h"
#include "fast_math.h"

#include <string.h>

enum {
    PIECE_EMPTY = 0,
    PIECE_PAWN = 1,
    PIECE_KNIGHT = 2,
    PIECE_BISHOP = 3,
    PIECE_ROOK = 4,
    PIECE_QUEEN = 5,
    PIECE_KING = 6
};

enum {
    CASTLE_WHITE_K = 1u << 0,
    CASTLE_WHITE_Q = 1u << 1,
    CASTLE_BLACK_K = 1u << 2,
    CASTLE_BLACK_Q = 1u << 3
};

enum {
    MOVE_NORMAL = 0,
    MOVE_CASTLE_K = 1,
    MOVE_CASTLE_Q = 2,
    MOVE_EN_PASSANT = 3,
    MOVE_PROMOTE_Q = 4,
    MOVE_PROMOTE_R = 5,
    MOVE_PROMOTE_B = 6,
    MOVE_PROMOTE_N = 7
};

/* Pack from/to/flags into 16 bits to keep move lists and search stack tiny on AVR. */
#define MOVE_MAKE(from, to, flag) ((chess_move_t)((from) | ((to) << 6) | ((flag) << 12)))
#define MOVE_FROM(move) ((uint8_t)((move) & 0x3fu))
#define MOVE_TO(move) ((uint8_t)(((move) >> 6) & 0x3fu))
#define MOVE_FLAG(move) ((uint8_t)(((move) >> 12) & 0x0fu))

typedef uint8_t (*move_consumer_t)(chess_move_t move, const chess_board_t *next, void *context);

static const uint8_t initial_board[64] = {
    CHESS_WHITE | PIECE_ROOK,   CHESS_WHITE | PIECE_KNIGHT, CHESS_WHITE | PIECE_BISHOP, CHESS_WHITE | PIECE_QUEEN,
    CHESS_WHITE | PIECE_KING,   CHESS_WHITE | PIECE_BISHOP, CHESS_WHITE | PIECE_KNIGHT, CHESS_WHITE | PIECE_ROOK,
    CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,
    CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,   CHESS_WHITE | PIECE_PAWN,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,
    CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,   CHESS_BLACK | PIECE_PAWN,
    CHESS_BLACK | PIECE_ROOK,   CHESS_BLACK | PIECE_KNIGHT, CHESS_BLACK | PIECE_BISHOP, CHESS_BLACK | PIECE_QUEEN,
    CHESS_BLACK | PIECE_KING,   CHESS_BLACK | PIECE_BISHOP, CHESS_BLACK | PIECE_KNIGHT, CHESS_BLACK | PIECE_ROOK
};

static uint8_t piece_type(uint8_t piece) {
    return piece & 0x07u;
}

static uint8_t piece_color(uint8_t piece) {
    return piece & CHESS_BLACK;
}

static uint8_t is_same_color(uint8_t piece, uint8_t side) {
    return piece != PIECE_EMPTY && piece_color(piece) == side;
}

static uint8_t is_enemy(uint8_t piece, uint8_t side) {
    return piece != PIECE_EMPTY && piece_color(piece) != side;
}

static uint8_t file_of(uint8_t square) {
    return square & 7u;
}

static uint8_t rank_of(uint8_t square) {
    return square >> 3;
}

static uint8_t opposite_side(uint8_t side) {
    return side ^ CHESS_BLACK;
}

static int16_t piece_value(uint8_t type) {
    static const int16_t values[7] = {0, 100, 320, 330, 500, 900, 0};
    return values[type];
}

static int8_t abs8(int8_t value) {
    return asm_abs8(value);
}

static int8_t center_bonus(uint8_t piece, uint8_t square) {
    int8_t file = (int8_t)file_of(square);
    int8_t rank = (int8_t)rank_of(square);
    int8_t rel_rank = piece_color(piece) == CHESS_WHITE ? rank : (int8_t)(7 - rank);
    int8_t dist = abs8((int8_t)(3 - file)) + abs8((int8_t)(3 - rank));

    switch (piece_type(piece)) {
    case PIECE_PAWN:
        return (int8_t)(rel_rank * 4 - dist);
    case PIECE_KNIGHT:
        return (int8_t)(18 - dist * 4);
    case PIECE_BISHOP:
        return (int8_t)(12 - dist * 3);
    case PIECE_ROOK:
        return (int8_t)(rel_rank * 2);
    case PIECE_QUEEN:
        return (int8_t)(8 - dist * 2);
    case PIECE_KING:
        return (int8_t)(-dist * 3);
    default:
        return 0;
    }
}

static int16_t evaluate_board(const chess_board_t *board) {
    uint8_t square;
    int16_t score = 0;

    /* Keep evaluation cheap: material plus a tiny positional bonus for activity/space. */
    for (square = 0; square < 64u; square++) {
        uint8_t piece = board->squares[square];
        int16_t value;

        if (piece == PIECE_EMPTY) {
            continue;
        }

        value = piece_value(piece_type(piece)) + center_bonus(piece, square);
        if (piece_color(piece) == CHESS_WHITE) {
            score += value;
        } else {
            score -= value;
        }
    }

    /* Negamax expects the score from the side-to-move perspective. */
    return board->side_to_move == CHESS_WHITE ? score : (int16_t)-score;
}

static uint8_t find_king(const chess_board_t *board, uint8_t side) {
    uint8_t square;

    for (square = 0; square < 64u; square++) {
        uint8_t piece = board->squares[square];
        if (piece_type(piece) == PIECE_KING && piece_color(piece) == side) {
            return square;
        }
    }

    return 0u;
}

static uint8_t ray_steps(uint8_t file, uint8_t rank, int8_t delta_file, int8_t delta_rank) {
    uint8_t steps = 7u;

    if (delta_file > 0) {
        steps = (uint8_t)(7u - file);
    } else if (delta_file < 0) {
        steps = file;
    }

    if (delta_rank > 0) {
        uint8_t rank_steps = (uint8_t)(7u - rank);
        if (rank_steps < steps) {
            steps = rank_steps;
        }
    } else if (delta_rank < 0) {
        uint8_t rank_steps = rank;
        if (rank_steps < steps) {
            steps = rank_steps;
        }
    }

    return steps;
}

static uint8_t is_square_attacked(const chess_board_t *board, uint8_t square, uint8_t attacker_side) {
    static const int8_t knight_df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int8_t knight_dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    static const int8_t king_df[8] = {1, 1, 1, 0, 0, -1, -1, -1};
    static const int8_t king_dr[8] = {1, 0, -1, 1, -1, 1, 0, -1};
    static const int8_t bishop_df[4] = {1, 1, -1, -1};
    static const int8_t bishop_dr[4] = {1, -1, 1, -1};
    static const int8_t rook_df[4] = {1, -1, 0, 0};
    static const int8_t rook_dr[4] = {0, 0, 1, -1};
    uint8_t file = file_of(square);
    uint8_t rank = rank_of(square);
    uint8_t i;

    /* Check attacks by piece class instead of generating all opponent moves. */
    if (attacker_side == CHESS_WHITE) {
        if (rank > 0u) {
            if (file > 0u && board->squares[square - 9u] == (CHESS_WHITE | PIECE_PAWN)) {
                return 1u;
            }
            if (file < 7u && board->squares[square - 7u] == (CHESS_WHITE | PIECE_PAWN)) {
                return 1u;
            }
        }
    } else {
        if (rank < 7u) {
            if (file > 0u && board->squares[square + 7u] == (CHESS_BLACK | PIECE_PAWN)) {
                return 1u;
            }
            if (file < 7u && board->squares[square + 9u] == (CHESS_BLACK | PIECE_PAWN)) {
                return 1u;
            }
        }
    }

    for (i = 0; i < 8u; i++) {
        int8_t target_file = (int8_t)file + knight_df[i];
        int8_t target_rank = (int8_t)rank + knight_dr[i];

        if (target_file >= 0 && target_file < 8 && target_rank >= 0 && target_rank < 8) {
            uint8_t target = (uint8_t)(target_rank * 8 + target_file);
            uint8_t piece = board->squares[target];
            if (piece == (attacker_side | PIECE_KNIGHT)) {
                return 1u;
            }
        }
    }

    /* The ray walker is a good asm target: tight loop, simple state, called very often. */
    for (i = 0; i < 4u; i++) {
        uint8_t steps = ray_steps(file, rank, bishop_df[i], bishop_dr[i]);
        int8_t delta = (int8_t)(bishop_df[i] + bishop_dr[i] * 8);

        if (asm_scan_ray(board->squares, square, delta, steps, attacker_side, PIECE_BISHOP, PIECE_QUEEN) != 0u) {
            return 1u;
        }
    }

    for (i = 0; i < 4u; i++) {
        uint8_t steps = ray_steps(file, rank, rook_df[i], rook_dr[i]);
        int8_t delta = (int8_t)(rook_df[i] + rook_dr[i] * 8);

        if (asm_scan_ray(board->squares, square, delta, steps, attacker_side, PIECE_ROOK, PIECE_QUEEN) != 0u) {
            return 1u;
        }
    }

    for (i = 0; i < 8u; i++) {
        int8_t target_file = (int8_t)file + king_df[i];
        int8_t target_rank = (int8_t)rank + king_dr[i];

        if (target_file >= 0 && target_file < 8 && target_rank >= 0 && target_rank < 8) {
            uint8_t target = (uint8_t)(target_rank * 8 + target_file);
            if (board->squares[target] == (attacker_side | PIECE_KING)) {
                return 1u;
            }
        }
    }

    return 0u;
}

uint8_t chess_is_in_check(const chess_board_t *board, uint8_t side) {
    return is_square_attacked(board, find_king(board, side), opposite_side(side));
}

static uint8_t emit_legal_move(const chess_board_t *board, chess_move_t move, move_consumer_t consumer, void *context) {
    chess_board_t next = *board;

    chess_apply_move(&next, move);
    /* After apply_move(), side_to_move has flipped, so the mover is the opposite side. */
    /* This turns pseudo-legal generation into legal generation with one king-safety test. */
    if (chess_is_in_check(&next, opposite_side(next.side_to_move))) {
        return 1u;
    }

    return consumer(move, &next, context);
}

static uint8_t emit_promotion_set(const chess_board_t *board, uint8_t from, uint8_t to, move_consumer_t consumer, void *context) {
    static const uint8_t flags[4] = {MOVE_PROMOTE_Q, MOVE_PROMOTE_R, MOVE_PROMOTE_B, MOVE_PROMOTE_N};
    uint8_t i;

    for (i = 0; i < 4u; i++) {
        if (emit_legal_move(board, MOVE_MAKE(from, to, flags[i]), consumer, context) == 0u) {
            return 0u;
        }
    }

    return 1u;
}

static uint8_t generate_pawn_moves(const chess_board_t *board, uint8_t square, move_consumer_t consumer, void *context) {
    uint8_t side = board->side_to_move;
    uint8_t file = file_of(square);
    uint8_t rank = rank_of(square);
    int8_t forward = side == CHESS_WHITE ? 8 : -8;
    int8_t start_rank = side == CHESS_WHITE ? 1 : 6;
    int8_t promo_rank = side == CHESS_WHITE ? 6 : 1;
    int8_t next_square = (int8_t)square + forward;
    uint8_t target;

    /* Pawns are the only piece with asymmetric move rules, so they get a dedicated generator. */
    if (next_square >= 0 && next_square < 64 && board->squares[(uint8_t)next_square] == PIECE_EMPTY) {
        target = (uint8_t)next_square;
        if ((int8_t)rank == promo_rank) {
            if (emit_promotion_set(board, square, target, consumer, context) == 0u) {
                return 0u;
            }
        } else {
            if (emit_legal_move(board, MOVE_MAKE(square, target, MOVE_NORMAL), consumer, context) == 0u) {
                return 0u;
            }

            if ((int8_t)rank == start_rank) {
                int8_t double_square = next_square + forward;
                if (double_square >= 0 && double_square < 64 && board->squares[(uint8_t)double_square] == PIECE_EMPTY) {
                    if (emit_legal_move(board, MOVE_MAKE(square, (uint8_t)double_square, MOVE_NORMAL), consumer, context) == 0u) {
                        return 0u;
                    }
                }
            }
        }
    }

    if (file > 0u) {
        int8_t capture_square = (int8_t)square + forward - 1;
        if (capture_square >= 0 && capture_square < 64) {
            target = (uint8_t)capture_square;
            if (is_enemy(board->squares[target], side) || board->ep_square == (int8_t)target) {
                uint8_t flag = board->ep_square == (int8_t)target ? MOVE_EN_PASSANT : MOVE_NORMAL;
                if ((int8_t)rank == promo_rank) {
                    if (flag == MOVE_NORMAL) {
                        if (emit_promotion_set(board, square, target, consumer, context) == 0u) {
                            return 0u;
                        }
                    }
                } else if (emit_legal_move(board, MOVE_MAKE(square, target, flag), consumer, context) == 0u) {
                    return 0u;
                }
            }
        }
    }

    if (file < 7u) {
        int8_t capture_square = (int8_t)square + forward + 1;
        if (capture_square >= 0 && capture_square < 64) {
            target = (uint8_t)capture_square;
            if (is_enemy(board->squares[target], side) || board->ep_square == (int8_t)target) {
                uint8_t flag = board->ep_square == (int8_t)target ? MOVE_EN_PASSANT : MOVE_NORMAL;
                if ((int8_t)rank == promo_rank) {
                    if (flag == MOVE_NORMAL) {
                        if (emit_promotion_set(board, square, target, consumer, context) == 0u) {
                            return 0u;
                        }
                    }
                } else if (emit_legal_move(board, MOVE_MAKE(square, target, flag), consumer, context) == 0u) {
                    return 0u;
                }
            }
        }
    }

    return 1u;
}

static uint8_t generate_jump_moves(const chess_board_t *board,
                                   uint8_t square,
                                   const int8_t *delta_file,
                                   const int8_t *delta_rank,
                                   uint8_t count,
                                   move_consumer_t consumer,
                                   void *context) {
    uint8_t side = board->side_to_move;
    uint8_t file = file_of(square);
    uint8_t rank = rank_of(square);
    uint8_t i;

    for (i = 0; i < count; i++) {
        int8_t target_file = (int8_t)file + delta_file[i];
        int8_t target_rank = (int8_t)rank + delta_rank[i];

        if (target_file < 0 || target_file >= 8 || target_rank < 0 || target_rank >= 8) {
            continue;
        }

        {
            uint8_t target = (uint8_t)(target_rank * 8 + target_file);
            uint8_t piece = board->squares[target];

            if (!is_same_color(piece, side) &&
                emit_legal_move(board, MOVE_MAKE(square, target, MOVE_NORMAL), consumer, context) == 0u) {
                return 0u;
            }
        }
    }

    return 1u;
}

static uint8_t generate_slider_moves(const chess_board_t *board,
                                     uint8_t square,
                                     const int8_t *delta_file,
                                     const int8_t *delta_rank,
                                     uint8_t count,
                                     move_consumer_t consumer,
                                     void *context) {
    uint8_t side = board->side_to_move;
    uint8_t file = file_of(square);
    uint8_t rank = rank_of(square);
    uint8_t i;

    for (i = 0; i < count; i++) {
        int8_t target_file = (int8_t)file + delta_file[i];
        int8_t target_rank = (int8_t)rank + delta_rank[i];

        while (target_file >= 0 && target_file < 8 && target_rank >= 0 && target_rank < 8) {
            uint8_t target = (uint8_t)(target_rank * 8 + target_file);
            uint8_t piece = board->squares[target];

            if (is_same_color(piece, side)) {
                break;
            }

            if (emit_legal_move(board, MOVE_MAKE(square, target, MOVE_NORMAL), consumer, context) == 0u) {
                return 0u;
            }

            if (piece != PIECE_EMPTY) {
                break;
            }

            target_file += delta_file[i];
            target_rank += delta_rank[i];
        }
    }

    return 1u;
}

static uint8_t generate_castling(const chess_board_t *board, uint8_t square, move_consumer_t consumer, void *context) {
    uint8_t side = board->side_to_move;

    if (chess_is_in_check(board, side)) {
        return 1u;
    }

    if (side == CHESS_WHITE && square == 4u) {
        if ((board->castling & CASTLE_WHITE_K) != 0u &&
            board->squares[5] == PIECE_EMPTY &&
            board->squares[6] == PIECE_EMPTY &&
            !is_square_attacked(board, 5u, CHESS_BLACK) &&
            !is_square_attacked(board, 6u, CHESS_BLACK) &&
            emit_legal_move(board, MOVE_MAKE(square, 6u, MOVE_CASTLE_K), consumer, context) == 0u) {
            return 0u;
        }

        if ((board->castling & CASTLE_WHITE_Q) != 0u &&
            board->squares[1] == PIECE_EMPTY &&
            board->squares[2] == PIECE_EMPTY &&
            board->squares[3] == PIECE_EMPTY &&
            !is_square_attacked(board, 3u, CHESS_BLACK) &&
            !is_square_attacked(board, 2u, CHESS_BLACK) &&
            emit_legal_move(board, MOVE_MAKE(square, 2u, MOVE_CASTLE_Q), consumer, context) == 0u) {
            return 0u;
        }
    }

    if (side == CHESS_BLACK && square == 60u) {
        if ((board->castling & CASTLE_BLACK_K) != 0u &&
            board->squares[61] == PIECE_EMPTY &&
            board->squares[62] == PIECE_EMPTY &&
            !is_square_attacked(board, 61u, CHESS_WHITE) &&
            !is_square_attacked(board, 62u, CHESS_WHITE) &&
            emit_legal_move(board, MOVE_MAKE(square, 62u, MOVE_CASTLE_K), consumer, context) == 0u) {
            return 0u;
        }

        if ((board->castling & CASTLE_BLACK_Q) != 0u &&
            board->squares[57] == PIECE_EMPTY &&
            board->squares[58] == PIECE_EMPTY &&
            board->squares[59] == PIECE_EMPTY &&
            !is_square_attacked(board, 59u, CHESS_WHITE) &&
            !is_square_attacked(board, 58u, CHESS_WHITE) &&
            emit_legal_move(board, MOVE_MAKE(square, 58u, MOVE_CASTLE_Q), consumer, context) == 0u) {
            return 0u;
        }
    }

    return 1u;
}

static uint8_t for_each_legal_move(const chess_board_t *board, move_consumer_t consumer, void *context) {
    static const int8_t knight_df[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    static const int8_t knight_dr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    static const int8_t bishop_df[4] = {1, 1, -1, -1};
    static const int8_t bishop_dr[4] = {1, -1, 1, -1};
    static const int8_t rook_df[4] = {1, -1, 0, 0};
    static const int8_t rook_dr[4] = {0, 0, 1, -1};
    static const int8_t king_df[8] = {1, 1, 1, 0, 0, -1, -1, -1};
    static const int8_t king_dr[8] = {1, 0, -1, 1, -1, 1, 0, -1};
    uint8_t square;

    /* Generate pseudo-legal moves piece-by-piece and let emit_legal_move() filter checks. */
    for (square = 0; square < 64u; square++) {
        uint8_t piece = board->squares[square];

        if (!is_same_color(piece, board->side_to_move)) {
            continue;
        }

        switch (piece_type(piece)) {
        case PIECE_PAWN:
            if (generate_pawn_moves(board, square, consumer, context) == 0u) {
                return 0u;
            }
            break;
        case PIECE_KNIGHT:
            if (generate_jump_moves(board, square, knight_df, knight_dr, 8u, consumer, context) == 0u) {
                return 0u;
            }
            break;
        case PIECE_BISHOP:
            if (generate_slider_moves(board, square, bishop_df, bishop_dr, 4u, consumer, context) == 0u) {
                return 0u;
            }
            break;
        case PIECE_ROOK:
            if (generate_slider_moves(board, square, rook_df, rook_dr, 4u, consumer, context) == 0u) {
                return 0u;
            }
            break;
        case PIECE_QUEEN:
            if (generate_slider_moves(board, square, bishop_df, bishop_dr, 4u, consumer, context) == 0u ||
                generate_slider_moves(board, square, rook_df, rook_dr, 4u, consumer, context) == 0u) {
                return 0u;
            }
            break;
        case PIECE_KING:
            if (generate_jump_moves(board, square, king_df, king_dr, 8u, consumer, context) == 0u ||
                generate_castling(board, square, consumer, context) == 0u) {
                return 0u;
            }
            break;
        default:
            break;
        }
    }

    return 1u;
}

void chess_reset(chess_board_t *board) {
    memcpy(board->squares, initial_board, sizeof(initial_board));
    board->side_to_move = CHESS_WHITE;
    board->castling = CASTLE_WHITE_K | CASTLE_WHITE_Q | CASTLE_BLACK_K | CASTLE_BLACK_Q;
    board->ep_square = -1;
    board->halfmove_clock = 0;
}

char chess_piece_char(uint8_t piece) {
    static const char glyphs[7] = {'.', 'p', 'n', 'b', 'r', 'q', 'k'};
    char c = glyphs[piece_type(piece)];

    if (piece == PIECE_EMPTY) {
        return '.';
    }

    if (piece_color(piece) == CHESS_WHITE) {
        c = (char)(c - ('a' - 'A'));
    }

    return c;
}

void chess_apply_move(chess_board_t *board, chess_move_t move) {
    uint8_t from = MOVE_FROM(move);
    uint8_t to = MOVE_TO(move);
    uint8_t flag = MOVE_FLAG(move);
    uint8_t piece = board->squares[from];
    uint8_t captured = board->squares[to];
    uint8_t type = piece_type(piece);

    if (captured != PIECE_EMPTY || type == PIECE_PAWN) {
        board->halfmove_clock = 0u;
    } else {
        board->halfmove_clock++;
    }

    board->squares[from] = PIECE_EMPTY;
    board->ep_square = -1;

    if (flag == MOVE_EN_PASSANT) {
        uint8_t capture_square = piece_color(piece) == CHESS_WHITE ? (uint8_t)(to - 8u) : (uint8_t)(to + 8u);
        board->squares[capture_square] = PIECE_EMPTY;
    }

    if (type == PIECE_KING) {
        if (piece_color(piece) == CHESS_WHITE) {
            board->castling &= (uint8_t)~(CASTLE_WHITE_K | CASTLE_WHITE_Q);
        } else {
            board->castling &= (uint8_t)~(CASTLE_BLACK_K | CASTLE_BLACK_Q);
        }

        if (flag == MOVE_CASTLE_K) {
            if (piece_color(piece) == CHESS_WHITE) {
                board->squares[5] = board->squares[7];
                board->squares[7] = PIECE_EMPTY;
            } else {
                board->squares[61] = board->squares[63];
                board->squares[63] = PIECE_EMPTY;
            }
        } else if (flag == MOVE_CASTLE_Q) {
            if (piece_color(piece) == CHESS_WHITE) {
                board->squares[3] = board->squares[0];
                board->squares[0] = PIECE_EMPTY;
            } else {
                board->squares[59] = board->squares[56];
                board->squares[56] = PIECE_EMPTY;
            }
        }
    }

    if (type == PIECE_ROOK) {
        if (from == 0u) {
            board->castling &= (uint8_t)~CASTLE_WHITE_Q;
        } else if (from == 7u) {
            board->castling &= (uint8_t)~CASTLE_WHITE_K;
        } else if (from == 56u) {
            board->castling &= (uint8_t)~CASTLE_BLACK_Q;
        } else if (from == 63u) {
            board->castling &= (uint8_t)~CASTLE_BLACK_K;
        }
    }

    /* Capturing a rook on its home square also removes that side's castling right. */
    if (captured != PIECE_EMPTY && piece_type(captured) == PIECE_ROOK) {
        if (to == 0u) {
            board->castling &= (uint8_t)~CASTLE_WHITE_Q;
        } else if (to == 7u) {
            board->castling &= (uint8_t)~CASTLE_WHITE_K;
        } else if (to == 56u) {
            board->castling &= (uint8_t)~CASTLE_BLACK_Q;
        } else if (to == 63u) {
            board->castling &= (uint8_t)~CASTLE_BLACK_K;
        }
    }

    if (type == PIECE_PAWN) {
        int8_t delta = (int8_t)to - (int8_t)from;
        if (delta == 16 || delta == -16) {
            board->ep_square = (int8_t)((from + to) / 2u);
        }
    }

    if (flag >= MOVE_PROMOTE_Q && flag <= MOVE_PROMOTE_N) {
        static const uint8_t promoted_type[4] = {PIECE_QUEEN, PIECE_ROOK, PIECE_BISHOP, PIECE_KNIGHT};
        board->squares[to] = piece_color(piece) | promoted_type[flag - MOVE_PROMOTE_Q];
    } else {
        board->squares[to] = piece;
    }

    board->side_to_move = opposite_side(board->side_to_move);
}

static char promotion_char(uint8_t flag) {
    switch (flag) {
    case MOVE_PROMOTE_Q:
        return 'q';
    case MOVE_PROMOTE_R:
        return 'r';
    case MOVE_PROMOTE_B:
        return 'b';
    case MOVE_PROMOTE_N:
        return 'n';
    default:
        return '\0';
    }
}

void chess_format_move(chess_move_t move, char *out) {
    uint8_t from = MOVE_FROM(move);
    uint8_t to = MOVE_TO(move);
    char promo = promotion_char(MOVE_FLAG(move));

    out[0] = (char)('a' + file_of(from));
    out[1] = (char)('1' + rank_of(from));
    out[2] = (char)('a' + file_of(to));
    out[3] = (char)('1' + rank_of(to));

    if (promo != '\0') {
        out[4] = promo;
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

typedef struct {
    uint8_t found;
} probe_context_t;

static uint8_t probe_consumer(chess_move_t move, const chess_board_t *next, void *context) {
    (void)move;
    (void)next;
    ((probe_context_t *)context)->found = 1u;
    return 0u;
}

uint8_t chess_has_legal_move(const chess_board_t *board) {
    probe_context_t probe = {0u};
    for_each_legal_move(board, probe_consumer, &probe);
    return probe.found;
}

typedef struct {
    uint8_t from;
    uint8_t to;
    char promo;
    uint8_t found;
    chess_move_t move;
} parse_context_t;

static uint8_t parse_consumer(chess_move_t move, const chess_board_t *next, void *context) {
    parse_context_t *parse = (parse_context_t *)context;
    char promo = promotion_char(MOVE_FLAG(move));

    (void)next;

    if (MOVE_FROM(move) == parse->from && MOVE_TO(move) == parse->to) {
        if (parse->promo == '\0') {
            if (promo == '\0' || promo == 'q') {
                parse->found = 1u;
                parse->move = move;
                return 0u;
            }
        } else if (promo == parse->promo) {
            parse->found = 1u;
            parse->move = move;
            return 0u;
        }
    }

    return 1u;
}

uint8_t chess_parse_move(const chess_board_t *board, const char *text, chess_move_t *move) {
    parse_context_t parse;
    size_t len = strlen(text);

    if (len < 4u || text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8' ||
        text[2] < 'a' || text[2] > 'h' || text[3] < '1' || text[3] > '8') {
        return 0u;
    }

    parse.from = (uint8_t)((text[1] - '1') * 8 + (text[0] - 'a'));
    parse.to = (uint8_t)((text[3] - '1') * 8 + (text[2] - 'a'));
    parse.promo = '\0';
    parse.found = 0u;
    parse.move = 0u;

    if (len >= 5u) {
        char promo = text[4];
        if (promo >= 'A' && promo <= 'Z') {
            promo = (char)(promo + ('a' - 'A'));
        }
        parse.promo = promo;
    }

    for_each_legal_move(board, parse_consumer, &parse);
    if (!parse.found) {
        return 0u;
    }

    *move = parse.move;
    return 1u;
}

static int16_t negamax(const chess_board_t *board, uint8_t depth, int16_t alpha, int16_t beta);

typedef struct {
    uint8_t depth;
    int16_t alpha;
    int16_t beta;
    int16_t best_score;
    uint8_t found_move;
} search_context_t;

static uint8_t search_consumer(chess_move_t move, const chess_board_t *next, void *context) {
    search_context_t *search = (search_context_t *)context;
    int16_t score = (int16_t)-negamax(next, (uint8_t)(search->depth - 1u), (int16_t)-search->beta, (int16_t)-search->alpha);

    (void)move;

    /* Negamax lets both sides share one search routine by flipping the returned score. */
    if (!search->found_move || score > search->best_score) {
        search->best_score = score;
    }

    search->found_move = 1u;
    if (score > search->alpha) {
        search->alpha = score;
    }

    return search->alpha < search->beta;
}

static int16_t negamax(const chess_board_t *board, uint8_t depth, int16_t alpha, int16_t beta) {
    search_context_t search;

    if (depth == 0u) {
        return evaluate_board(board);
    }

    /* Alpha-beta pruning keeps the tiny engine responsive on an 8-bit MCU. */
    search.depth = depth;
    search.alpha = alpha;
    search.beta = beta;
    search.best_score = -32000;
    search.found_move = 0u;

    for_each_legal_move(board, search_consumer, &search);

    if (!search.found_move) {
        if (chess_is_in_check(board, board->side_to_move)) {
            return (int16_t)(-30000 + depth);
        }
        return 0;
    }

    return search.best_score;
}

typedef struct {
    uint8_t depth;
    int16_t best_score;
    chess_move_t best_move;
    uint8_t found_move;
} root_context_t;

static uint8_t root_consumer(chess_move_t move, const chess_board_t *next, void *context) {
    root_context_t *root = (root_context_t *)context;
    int16_t score = (int16_t)-negamax(next, (uint8_t)(root->depth - 1u), -32000, 32000);

    if (!root->found_move || score > root->best_score) {
        root->best_score = score;
        root->best_move = move;
    }

    root->found_move = 1u;
    return 1u;
}

uint8_t chess_best_move(const chess_board_t *board, uint8_t depth, chess_move_t *best_move) {
    root_context_t root;

    if (depth == 0u) {
        return 0u;
    }

    root.depth = depth;
    root.best_score = -32000;
    root.best_move = 0u;
    root.found_move = 0u;

    for_each_legal_move(board, root_consumer, &root);
    if (!root.found_move) {
        return 0u;
    }

    *best_move = root.best_move;
    return 1u;
}
