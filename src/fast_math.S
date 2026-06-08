/***************************************************************************
--                      tiny_chess - fast_math_asm
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
 * Module: fast_math_asm
 *
 * Provides a small set of math helpers hand-written in AVR assembly to avoid
 * the overhead that a generic C implementation would carry on the ATmega328P.
 *
 * Exported functions
 * ------------------
 *   int8_t asm_abs8(int8_t value)
 *       Returns the absolute value of a signed 8-bit integer.
 *       Argument : r24  (first byte-sized argument per AVR-GCC ABI)
 *       Return   : r24  (return value register)
 */

.text
.global asm_abs8
.type asm_abs8, @function

/*
 * asm_abs8 -- 8-bit absolute value
 *
 * If value >= 0 the function returns immediately; if negative it negates
 * before returning.  Two's-complement negation on AVR sets the V flag for
 * INT8_MIN (-128), but that edge case is not relevant to the chess engine.
 *
 * Registers on entry:
 *   r24 = value (int8_t, the number whose absolute value is wanted)
 */
asm_abs8:
    tst  r24        ; set flags (N, Z, V) based on r24 without changing it
    brpl 1f         ; branch if plus (N flag clear, i.e. value >= 0) -- skip negation
    neg  r24        ; negate r24: r24 = 0 - r24  (two's-complement, value was negative)
1:
    ret             ; return with |value| in r24

.size asm_abs8, .-asm_abs8
