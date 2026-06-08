/***************************************************************************
--                      tiny_chess - fast_attack_asm
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

/*
 * Module: fast_attack_asm
 *
 * Implements sliding-piece ray scanning for the chess engine's attack
 * detection, hand-written in AVR assembly to keep the inner loop as tight
 * as possible on the ATmega328P.
 *
 * The board is represented as a flat 64-element byte array (squares[64]).
 * Each byte encodes a piece as:
 *   bits [2:0]  piece type  (0=empty, 1=pawn, 2=knight, 3=bishop,
 *                             4=rook,  5=queen, 6=king)
 *   bit  [3]    side        (0=white, 8=black)
 *
 * Exported functions
 * ------------------
 *   uint8_t asm_scan_ray(const uint8_t *squares,  -- r25:r24
 *                        uint8_t  square,          -- r22
 *                        int8_t   delta,           -- r20
 *                        uint8_t  steps,           -- r18
 *                        uint8_t  attacker_side,   -- r16
 *                        uint8_t  target_a,        -- r14
 *                        uint8_t  target_b)        -- r12
 *
 *   Walks up to `steps` squares along the ray starting at `square`,
 *   advancing by `delta` each step (e.g. +1 east, -1 west, +8 north,
 *   -9 south-west, etc.).  Returns 1 if the first occupied square on the
 *   ray contains a piece of `attacker_side` whose type matches either
 *   `target_a` or `target_b` (used to check queen+rook or queen+bishop
 *   simultaneously).  Returns 0 if the ray is empty, blocked by a piece
 *   of the wrong side, or blocked by a piece of the right side but wrong
 *   type.
 *
 * Register allocation inside the function:
 *   r27:r26  (X)  base pointer to squares array (copied from r25:r24)
 *   r31:r30  (Z)  scratch pointer used for each individual array access
 *   r19           current square index (walks along the ray)
 *   r21           piece value loaded from the current square
 *   r22           side bits of the loaded piece (reuses the arg register)
 *   r18           remaining step count (counts down)
 *   r20           delta (direction step, unchanged throughout)
 *   r16           attacker_side (unchanged throughout)
 *   r14           target_a piece type (unchanged throughout)
 *   r12           target_b piece type (unchanged throughout)
 *   r1            always 0 in AVR-GCC (used for the carry-propagation adc)
 */

.text
.global asm_scan_ray
.type asm_scan_ray, @function

asm_scan_ray:
    movw r26, r24       ; X (r27:r26) = squares pointer -- set up base address for array access
    mov  r19, r22       ; r19 = square -- working copy of the starting index

1:  ; --- loop: advance one step along the ray and examine the square ---
    tst  r18            ; test steps counter (are there any steps left?)
    breq 4f             ; if steps == 0, no attacker found -- return 0

    add  r19, r20       ; current_square += delta -- move one step in the ray direction
    movw r30, r26       ; Z = X (squares base pointer) -- reload base each iteration
    add  r30, r19       ; Z low byte += current_square index -- address = squares + current_square
    adc  r31, r1        ; Z high byte += carry from previous add + 0 -- propagate carry into pointer high byte
    ld   r21, Z         ; r21 = squares[current_square] -- load the piece at this square

    tst  r21            ; is the square empty? (piece byte == 0)
    breq 3f             ; yes -- decrement step count and keep scanning

    ; --- square is occupied: check if it is the right attacker ---
    mov  r22, r21       ; r22 = piece byte (preserve r21 for piece-type check below)
    andi r22, 0x08      ; isolate bit 3 (the side bit: 0x00=white, 0x08=black)
    cp   r22, r16       ; does the piece side match attacker_side?
    brne 4f             ; no -- ray is blocked by a friendly piece, return 0

    andi r21, 0x07      ; isolate bits [2:0] (the piece-type field)
    cp   r21, r14       ; is the piece type equal to target_a?
    breq 5f             ; yes -- attacker found, return 1
    cp   r21, r12       ; is the piece type equal to target_b?
    breq 5f             ; yes -- attacker found, return 1
    rjmp 4f             ; right side but wrong piece type (e.g. king on a queen ray), return 0

3:  ; --- empty square: consume one step and loop ---
    dec  r18            ; steps-- (one more empty square consumed)
    rjmp 1b             ; go back to the top of the loop

4:  ; --- no attacker found ---
    clr  r24            ; return value = 0 (false)
    ret

5:  ; --- attacker found ---
    ldi  r24, 1         ; return value = 1 (true)
    ret

.size asm_scan_ray, .-asm_scan_ray
