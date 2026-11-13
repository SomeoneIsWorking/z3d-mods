"""CityHash64 (v1.1), the hash Azahar names custom textures by.

Ported from Azahar's src/common/cityhash.cpp (MIT, Copyright (c) 2011 Google, Inc.).
"""

from __future__ import annotations

import struct

MASK = (1 << 64) - 1
K0 = 0xC3A5C85C97CB3127
K1 = 0xB492B66FBE98F273
K2 = 0x9AE16A3B2F90404F
K_MUL = 0x9DDFEA08EB382D69


def _fetch64(s: bytes, at: int) -> int:
    return struct.unpack_from("<Q", s, at)[0]


def _fetch32(s: bytes, at: int) -> int:
    return struct.unpack_from("<I", s, at)[0]


def _rotate(value: int, shift: int) -> int:
    return value if shift == 0 else ((value >> shift) | (value << (64 - shift))) & MASK


def _shift_mix(value: int) -> int:
    return value ^ (value >> 47)


def _swap64(value: int) -> int:
    return int.from_bytes(value.to_bytes(8, "little"), "big")


def _len16(u: int, v: int, mul: int = K_MUL) -> int:
    a = ((u ^ v) * mul) & MASK
    a ^= a >> 47
    b = ((v ^ a) * mul) & MASK
    b ^= b >> 47
    return (b * mul) & MASK


def _len0to16(s: bytes) -> int:
    n = len(s)
    if n >= 8:
        mul = K2 + n * 2
        a = (_fetch64(s, 0) + K2) & MASK
        b = _fetch64(s, n - 8)
        c = (_rotate(b, 37) * mul + a) & MASK
        d = ((_rotate(a, 25) + b) * mul) & MASK
        return _len16(c, d, mul)
    if n >= 4:
        mul = K2 + n * 2
        return _len16(n + (_fetch32(s, 0) << 3), _fetch32(s, n - 4), mul)
    if n > 0:
        y = s[0] + (s[n >> 1] << 8)
        z = n + (s[n - 1] << 2)
        return (_shift_mix(((y * K2) ^ (z * K0)) & MASK) * K2) & MASK
    return K2


def _len17to32(s: bytes) -> int:
    n = len(s)
    mul = K2 + n * 2
    a = (_fetch64(s, 0) * K1) & MASK
    b = _fetch64(s, 8)
    c = (_fetch64(s, n - 8) * mul) & MASK
    d = (_fetch64(s, n - 16) * K2) & MASK
    return _len16((_rotate((a + b) & MASK, 43) + _rotate(c, 30) + d) & MASK,
                  (a + _rotate((b + K2) & MASK, 18) + c) & MASK, mul)


def _weak32(s: bytes, at: int, a: int, b: int) -> tuple[int, int]:
    w, x, y, z = struct.unpack_from("<4Q", s, at)
    a = (a + w) & MASK
    b = _rotate((b + a + z) & MASK, 21)
    c = a
    a = (a + x + y) & MASK
    b = (b + _rotate(a, 44)) & MASK
    return (a + z) & MASK, (b + c) & MASK


def _len33to64(s: bytes) -> int:
    n = len(s)
    mul = K2 + n * 2
    a = (_fetch64(s, 0) * K2) & MASK
    b = _fetch64(s, 8)
    c = _fetch64(s, n - 24)
    d = _fetch64(s, n - 32)
    e = (_fetch64(s, 16) * K2) & MASK
    f = (_fetch64(s, 24) * 9) & MASK
    g = _fetch64(s, n - 8)
    h = (_fetch64(s, n - 16) * mul) & MASK
    u = (_rotate((a + g) & MASK, 43) + (_rotate(b, 30) + c) * 9) & MASK
    v = (((a + g) & MASK ^ d) + f + 1) & MASK
    w = (_swap64(((u + v) * mul) & MASK) + h) & MASK
    x = (_rotate((e + f) & MASK, 42) + c) & MASK
    y = ((_swap64(((v + w) * mul) & MASK) + g) * mul) & MASK
    z = (e + f + c) & MASK
    a = (_swap64(((x + z) * mul + y) & MASK) + b) & MASK
    b = (_shift_mix(((z + a) * mul + d + h) & MASK) * mul) & MASK
    return (b + x) & MASK


def hash64(s: bytes) -> int:
    n = len(s)
    if n <= 16:
        return _len0to16(s)
    if n <= 32:
        return _len17to32(s)
    if n <= 64:
        return _len33to64(s)
    x = _fetch64(s, n - 40)
    y = (_fetch64(s, n - 16) + _fetch64(s, n - 56)) & MASK
    z = _len16((_fetch64(s, n - 48) + n) & MASK, _fetch64(s, n - 24))
    v = _weak32(s, n - 64, n, z)
    w = _weak32(s, n - 32, (y + K1) & MASK, x)
    x = (x * K1 + _fetch64(s, 0)) & MASK
    for at in range(0, (n - 1) & ~63, 64):
        x = (_rotate((x + y + v[0] + _fetch64(s, at + 8)) & MASK, 37) * K1) & MASK
        y = (_rotate((y + v[1] + _fetch64(s, at + 48)) & MASK, 42) * K1) & MASK
        x ^= w[1]
        y = (y + v[0] + _fetch64(s, at + 40)) & MASK
        z = (_rotate((z + w[0]) & MASK, 33) * K1) & MASK
        v = _weak32(s, at, (v[1] * K1) & MASK, (x + w[0]) & MASK)
        w = _weak32(s, at + 32, (z + w[1]) & MASK, (y + _fetch64(s, at + 16)) & MASK)
        z, x = x, z
    return _len16((_len16(v[0], w[0]) + _shift_mix(y) * K1 + z) & MASK,
                  (_len16(v[1], w[1]) + x) & MASK)
