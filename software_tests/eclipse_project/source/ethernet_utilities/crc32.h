// vim: ts=4 sw=4 expandtab

// THIS IS GENERATED C CODE.
// https://bues.ch/h/crcgen
// 
// This code is Public Domain.
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted.
// 
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
// RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
// USE OR PERFORMANCE OF THIS SOFTWARE.

#ifndef CRC_H_
#define CRC_H_

#include <stdint.h>

// CRC polynomial coefficients: x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1
//                              0xEDB88320 (hex)
// CRC width:                   32 bits
// CRC shift direction:         right (little endian)
// Input word width:            8 bits

#ifdef b
# undef b
#endif
#define b(x, b) (((x) >> (b)) & 1u)

static uint32_t crc(uint32_t crcIn, uint8_t data)
{
    uint32_t ret;
    ret  = (uint32_t)(b(crcIn, 2) ^ b(crcIn, 8) ^ b(data, 2)) << 0;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 3) ^ b(crcIn, 9) ^ b(data, 0) ^ b(data, 3)) << 1;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 4) ^ b(crcIn, 10) ^ b(data, 0) ^ b(data, 1) ^ b(data, 4)) << 2;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 5) ^ b(crcIn, 11) ^ b(data, 1) ^ b(data, 2) ^ b(data, 5)) << 3;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 6) ^ b(crcIn, 12) ^ b(data, 0) ^ b(data, 2) ^ b(data, 3) ^ b(data, 6)) << 4;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 7) ^ b(crcIn, 13) ^ b(data, 1) ^ b(data, 3) ^ b(data, 4) ^ b(data, 7)) << 5;
    ret |= (uint32_t)(b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 14) ^ b(data, 4) ^ b(data, 5)) << 6;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(crcIn, 15) ^ b(data, 0) ^ b(data, 5) ^ b(data, 6)) << 7;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(crcIn, 16) ^ b(data, 1) ^ b(data, 6) ^ b(data, 7)) << 8;
    ret |= (uint32_t)(b(crcIn, 7) ^ b(crcIn, 17) ^ b(data, 7)) << 9;
    ret |= (uint32_t)(b(crcIn, 2) ^ b(crcIn, 18) ^ b(data, 2)) << 10;
    ret |= (uint32_t)(b(crcIn, 3) ^ b(crcIn, 19) ^ b(data, 3)) << 11;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 4) ^ b(crcIn, 20) ^ b(data, 0) ^ b(data, 4)) << 12;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 5) ^ b(crcIn, 21) ^ b(data, 0) ^ b(data, 1) ^ b(data, 5)) << 13;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 6) ^ b(crcIn, 22) ^ b(data, 1) ^ b(data, 2) ^ b(data, 6)) << 14;
    ret |= (uint32_t)(b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 7) ^ b(crcIn, 23) ^ b(data, 2) ^ b(data, 3) ^ b(data, 7)) << 15;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 24) ^ b(data, 0) ^ b(data, 2) ^ b(data, 3) ^ b(data, 4)) << 16;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 25) ^ b(data, 0) ^ b(data, 1) ^ b(data, 3) ^ b(data, 4) ^ b(data, 5)) << 17;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(crcIn, 26) ^ b(data, 0) ^ b(data, 1) ^ b(data, 2) ^ b(data, 4) ^ b(data, 5) ^ b(data, 6)) << 18;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(crcIn, 27) ^ b(data, 1) ^ b(data, 2) ^ b(data, 3) ^ b(data, 5) ^ b(data, 6) ^ b(data, 7)) << 19;
    ret |= (uint32_t)(b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(crcIn, 28) ^ b(data, 3) ^ b(data, 4) ^ b(data, 6) ^ b(data, 7)) << 20;
    ret |= (uint32_t)(b(crcIn, 2) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 7) ^ b(crcIn, 29) ^ b(data, 2) ^ b(data, 4) ^ b(data, 5) ^ b(data, 7)) << 21;
    ret |= (uint32_t)(b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(crcIn, 30) ^ b(data, 2) ^ b(data, 3) ^ b(data, 5) ^ b(data, 6)) << 22;
    ret |= (uint32_t)(b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(crcIn, 31) ^ b(data, 3) ^ b(data, 4) ^ b(data, 6) ^ b(data, 7)) << 23;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 2) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 7) ^ b(data, 0) ^ b(data, 2) ^ b(data, 4) ^ b(data, 5) ^ b(data, 7)) << 24;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(data, 0) ^ b(data, 1) ^ b(data, 2) ^ b(data, 3) ^ b(data, 5) ^ b(data, 6)) << 25;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 2) ^ b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(data, 0) ^ b(data, 1) ^ b(data, 2) ^ b(data, 3) ^ b(data, 4) ^ b(data, 6) ^ b(data, 7)) << 26;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 3) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 7) ^ b(data, 1) ^ b(data, 3) ^ b(data, 4) ^ b(data, 5) ^ b(data, 7)) << 27;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 4) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(data, 0) ^ b(data, 4) ^ b(data, 5) ^ b(data, 6)) << 28;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 5) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(data, 0) ^ b(data, 1) ^ b(data, 5) ^ b(data, 6) ^ b(data, 7)) << 29;
    ret |= (uint32_t)(b(crcIn, 0) ^ b(crcIn, 1) ^ b(crcIn, 6) ^ b(crcIn, 7) ^ b(data, 0) ^ b(data, 1) ^ b(data, 6) ^ b(data, 7)) << 30;
    ret |= (uint32_t)(b(crcIn, 1) ^ b(crcIn, 7) ^ b(data, 1) ^ b(data, 7)) << 31;
    return ret;
}
#undef b

#endif /* CRC_H_ */
