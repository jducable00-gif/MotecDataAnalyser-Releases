#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Unit tests for ams2_cdf_decoder using synthetic buffers."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from ams2_cdf_decoder import (decode_field_at, find_identifier, extract_field,
                              FieldSpec, DecodeError)


def rec(type_byte: bytes, id_bytes: bytes, payload: bytes = b'') -> bytes:
    return type_byte + id_bytes + payload


def mk_id(v):                                  # build a 4-byte LE id from an int
    return v.to_bytes(4, 'little')

def f32(x):
    """Exact float32 round-trip of x (matches what the decoder returns)."""
    return round(struct.unpack('<f', struct.pack('<f', x))[0], 6)

FAILS = []


def check(name, cond, detail=''):
    status = 'PASS' if cond else 'FAIL'
    print(f"[{status}] {name}" + (f" -- {detail}" if detail and not cond else ''))
    if not cond:
        FAILS.append(name)


# --- 0x21 int32 -------------------------------------------------------------
buf = rec(b'\x21', mk_id(0xAA), struct.pack('<i', -42))
off, cnt = find_identifier(buf, mk_id(0xAA))
check("int32: found", off == 1 and cnt == 1)
val = decode_field_at(buf, off)
check("int32: value", val == [-42], f"got {val}")

# --- 0x22 float -------------------------------------------------------------
buf = rec(b'\x22', mk_id(0xBB), struct.pack('<f', 158.7))
off, _ = find_identifier(buf, mk_id(0xBB))
check("float: value", decode_field_at(buf, off) == [f32(158.7)], f"got {decode_field_at(buf, off)}")

# --- 0x28 no data -----------------------------------------------------------
buf = rec(b'\x28', mk_id(0xCC))              # type + id only, NO payload
off, _ = find_identifier(buf, mk_id(0xCC))
check("no-data: None", decode_field_at(buf, off) is None)

# --- 0x24 A3 02 : float x3 (Range min/step/max) ------------------------------
buf = rec(b'\x24', mk_id(0xDD), bytes([0xA3, 0x02]) + struct.pack('<fff', -1.5, 0.1, 1.8))
off, _ = find_identifier(buf, mk_id(0xDD))
val = decode_field_at(buf, off)
check("fxfxf: value", val == [-1.5, 0.1, round(1.8, 6)], f"got {val}")

# --- 0x24 A3 00 : float,float,Byte ------------------------------------------
buf = rec(b'\x24', mk_id(0xEE), bytes([0xA3, 0x00]) + struct.pack('<ffb', -1.5, 1.8, 7))
off, _ = find_identifier(buf, mk_id(0xEE))
val = decode_field_at(buf, off)
check("fxfxb: value", val == [-1.5, round(1.8, 6), 7], f"got {val}")

# --- 0x24 A2 : float,float (flat single shape byte) --------------------------
buf = rec(b'\x24', mk_id(0xFF), bytes([0xA2]) + struct.pack('<ff', -1.5, 1.8))
off, _ = find_identifier(buf, mk_id(0xFF))
val = decode_field_at(buf, off)
check("A2 fxf: value", val == [-1.5, round(1.8, 6)], f"got {val}")

# --- 0x24 93 02 : int32,float,float ------------------------------------------
buf = rec(b'\x24', mk_id(0xF0), bytes([0x93, 0x02]) + struct.pack('<iff', -1587, 6.0, 12.0))
off, _ = find_identifier(buf, mk_id(0xF0))
val = decode_field_at(buf, off)
check("i fxf: value", val == [-1587, 6.0, round(12.0, 6)], f"got {val}")

# --- 0x24 93 00 : int32,float,BYTE ---------------------------------------------
buf = rec(b'\x24', mk_id(0xF1), bytes([0x93, 0x00]) + struct.pack('<ifb', -1587, 6.0, 3))
off, _ = find_identifier(buf, mk_id(0xF1))
val = decode_field_at(buf, off)
check("i f b: value", val == [-1587, round(6.0, 6), 3], f"got {val}")

# --- 0x24 92 : int32,float (flat single shape byte) ---------------------------
buf = rec(b'\x24', mk_id(0xF2), bytes([0x92]) + struct.pack('<if', -1587, 6.0))
off, _ = find_identifier(buf, mk_id(0xF2))
val = decode_field_at(buf, off)
check("92 i f: value", val == [-1587, round(6.0, 6)], f"got {val}")

# --- 0x24 52 : int32,int32 (flat single shape byte) ---------------------------
buf = rec(b'\x24', mk_id(0xF6), bytes([0x52]) + struct.pack('<ii', -1, 987))
off, _ = find_identifier(buf, mk_id(0xF6))
val = decode_field_at(buf, off)
check("52 i x i: value", val == [-1, 987], f"got {val}")

# --- 0x24 A4 0A : float x4 ----------------------------------------------------
buf = rec(b'\x24', mk_id(0xF3), bytes([0xA4, 0x0A]) + struct.pack('<ffff', 1.0, 2.5, -3.25, 99.7))
off, _ = find_identifier(buf, mk_id(0xF3))
val = decode_field_at(buf, off)
check("fxfxfxf: value", val == [f32(1.0), f32(2.5), f32(-3.25), f32(99.7)], f"got {val}")

# --- 0x24 A5 2A : float x5 ----------------------------------------------------
buf = rec(b'\x24', mk_id(0xF4), bytes([0xA5, 0x2A]) + struct.pack('<fffff', *range(1, 6)))
off, _ = find_identifier(buf, mk_id(0xF4))
val = decode_field_at(buf, off)
check("fx5: value", val == [float(i) for i in range(1, 6)], f"got {val}")

# --- 0x23 01 : float,float ----------------------------------------------------
buf = rec(b'\x23', mk_id(0xF5), bytes([0x01]) + struct.pack('<ff', -4.75, 8.2))
off, _ = find_identifier(buf, mk_id(0xF5))
val = decode_field_at(buf, off)
check("t23 fxf: value", val == [round(-4.75, 6), round(8.2, 6)], f"got {val}")

# --- unknown shape raises ------------------------------------------------------
buf = rec(b'\x24', mk_id(0xF6), bytes([0xDE, 0xAD]) + b'\x00' * 12)
off, _ = find_identifier(buf, mk_id(0xF6))
try:
    decode_field_at(buf, off)
    check("unknown shape raises", False)
except DecodeError as e:
    check("unknown shape raises", True, str(e))

# --- extract_field with range + missing id -------------------------------------
data = b'\x00' * 16 + rec(b'\x22', mk_id(0xB7), struct.pack('<f', 3.5)) + b'\x00' * 8
spec = FieldSpec('test_float', mk_id(0xB7))
val, off = extract_field(data, spec)
check("extract: value+off", val == [round(3.5, 6)] and off == 17, f"got {val} @ {off}")

try:
    extract_field(data, FieldSpec('missing', mk_id(0x99)), search_start=24)
    check("extract: missing raises", False)
except LookupError as e:
    check("extract: missing raises", True, str(e))


print()
if FAILS:
    print(f"{len(FAILS)} test(s) FAILED")
    sys.exit(1)
print("All tests passed.")
