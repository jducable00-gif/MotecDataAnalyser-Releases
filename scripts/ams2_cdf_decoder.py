#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Decoder for AMS2 CDFBIN / EDFIN binary files (chassis, engines).

Port of the type-decoding logic from GeneralFunction.cpp::ExtractData()
(Embarcadero project "GeneralFunction"). Record layout in these files:

    [type byte][4-byte identifier][payload ...]   (little-endian)

The 1-byte *type* code sits IMMEDIATELY BEFORE the 4-byte identifier.
`find_identifier()` locates an id and `decode_field_at()` decodes the
record that starts at it, using the type table below:

    0x20 = int8            (payload @ D0)
    0x21 = int32           (payload @ D0)
    0x22 = float32         (payload @ D0)
    0x23 + s               compound, payload starts after shape byte(s)
    0x24 + s[+s2]          compound ("shape" table below), payload after shapes
    0x26 = int8            tire-compound setting (chassis files only; the
                           engine-type variant is decoded from file tail and
                           is out of scope here)
    0x28 = NO DATA         "Not Used" -> decode_field_at() returns None

D0 := offset right after the identifier (= type_byte_pos + 5).
For nested shapes (second shape byte s2), payload starts at D0+2.

Only little-endian values are produced; floats are IEEE-754 single.
"""

import struct

# ---------------------------------------------------------------------------
# Shape tables for compound types. Each entry maps to a list of format codes:
#   'b' = int8 (1),  'i' = int32 LE (4),  'f' = float32 LE (4)
# The payload offset is computed from the number of shape bytes consumed, so
# no explicit offsets are needed here.
# ---------------------------------------------------------------------------

_TYPE_0x23_SHAPES = {
    0x01: ('f', 'f'),          # float, float
}

_TYPE_0x24_FLAT_SHAPES = {     # single shape byte s1 at D0, payload @ D0+1
    0x01: ('b',),              # Byte
    0x02: ('b', 'b'),          # Byte, Byte
    0x11: ('i',),              # Byte4 (int32)
    0x12: ('i', 'b'),          # int32, Byte
    0x21: ('f',),              # Float
    0x22: ('f', 'b'),          # Float, Byte
    0x42: ('b', 'i'),          # Byte, int32 (gearbox)
    0x52: ('i', 'i'),          # int32, int32
    0x82: ('b', 'f'),          # Byte, Float
}

_TYPE_0x24_NESTED_SHAPES = {   # shape bytes s1,s2 at D0,D0+1; payload @ D0+2
    0x03: {0x00: ('b', 'b', 'b'),          # Byte,Byte,Byte
           0x01: ('b', 'b', 'i'),          # Byte,Byte,int32
           0x02: ('b', 'b', 'f')},         # Byte,Byte,float
    0x04: {0x00: ('b', 'b', 'b', 'b'),     # Byte x4
           0x08: ('b', 'b', 'b', 'f')},    # Byte,Byte,Byte,float
    0x05: {0x00: ('b',) * 5,               # Byte x5
           0x28: ('b', 'b', 'b', 'f', 'f')},  # Byte,BYTE,BYTE,float,float
    0x13: {0x00: ('i', 'b', 'b'),          # int32,Byte,Byte
           0x02: ('i', 'b', 'f')},         # int32,Byte,float
    0x16: {0x00: ('i',) + ('b',) * 5,      # int32, Byte x5
           0xAA: ('i', 'b') + ('f',) * 4}, # int32,Byte,float x4
    0x23: {0x00: ('f', 'b', 'b'),          # float,Byte,Byte
           0x01: ('f', 'f', 'b'),          # float,float,Byte
           0x02: ('f', 'b', 'f')},         # float,Byte,float
    0x43: {0x00: ('b', 'i', 'b')},         # Byte,int32,Byte
    0x53: {0x00: ('i', 'i', 'b'),          # int32,int32,Byte
           0x01: ('i',) * 3,               # int32 x3 (per ExtractData read code)
           0x02: ('i', 'i', 'f')},         # int32,int32,float
    0x63: {0x00: ('f', 'i', 'b')},         # float,int32,Byte
    0x83: {0x00: ('b', 'f', 'b'),          # Byte,float,Byte
           0x01: ('b', 'f', 'i'),          # Byte,float,int32
           0x02: ('b', 'f', 'f')},         # Byte,float,float
    0x86: {0xAA: ('b',) + ('f',) * 5},     # Byte, float x5
    0x92: {},                             # handled as flat below (no s2 in C++)
    0x93: {0x00: ('i', 'f', 'b'),          # int32,float,Byte (per comment; the
                                           #   C++ read code pulls an extra word)
           0x02: ('i', 'f', 'f')},         # int32,float,float
    0x96: {0xAA: ('i',) + ('f',) * 5},     # int32, float x5
    0xA2: {},                             # handled as flat below (no s2 in C++)
    0xA3: {0x00: ('f', 'f', 'b'),          # float,float,Byte
           0x01: ('f', 'f', 'i'),          # float,float,int32
           0x02: ('f',) * 3},              # float x3 (e.g. Range min/step/max)
    0xA4: {0x0A: ('f',) * 4},              # float x4
    0xA5: {0x28: ('b', 'b', 'b') + ('f',) * 2,   # Byte,BYTE,BYTE,f,f (per C++)
           0x2A: ('f',) * 5},              # float x5
}

# Flat shapes that the C++ nests under a switch but reads without s2.
_TYPE_0x24_FLAT_SHAPES[0xA2] = ('f', 'f')   # Float, Float (payload @ D0+1)
_TYPE_0x24_FLAT_SHAPES[0x92] = ('i', 'f')   # int32, float  (payload @ D0+1)


class DecodeError(ValueError):
    """Raised when a record cannot be decoded."""

    def __init__(self, message: str, type_byte=None, shape_bytes=()):
        super().__init__(message)
        self.type_byte = type_byte
        self.shape_bytes = tuple(shape_bytes)


def find_identifier(data: bytes, id_bytes: bytes):
    """Return (offset_of_id, count). offset is -1 when not found.

    Mirrors rechercherChaine(): the 4-byte identifier sequence; callers use
    `decode_field_at()` which reads the type byte at offset-1.
    """
    if len(id_bytes) != 4:
        raise ValueError("identifier must be exactly 4 bytes")
    count = data.count(id_bytes)
    return (data.find(id_bytes), count)


def _payload(data, start, fmts):
    out = []
    off = start
    for code in fmts:
        if code == 'b':
            out.append(struct.unpack_from('b', data, off)[0])
            off += 1
        elif code == 'i':
            out.append(struct.unpack_from('<i', data, off)[0])
            off += 4
        elif code == 'f':
            out.append(round(struct.unpack_from('<f', data, off)[0], 6))
            off += 4
        else:
            raise DecodeError(f"unknown format code {code!r}")
    return out


def decode_field_at(data: bytes, id_off: int):
    """Decode the record whose identifier starts at `id_off`.

    Returns a list of decoded values (ints/floats), or None when the type byte
    is 0x28 ("Not Used" / no data). Raises DecodeError for unknown shapes.
    """
    if id_off < 1:
        raise DecodeError("no type byte before identifier")

    t = data[id_off - 1]
    d0 = id_off + 4                      # first payload/shape position

    def need(n):
        if len(data) < d0 + n:
            raise DecodeError(f"record truncated (need {n} bytes at D0)")

    if t == 0x28:                        # no data / not used
        return None

    if t in (0x20,):                     # int8
        need(1)
        return _payload(data, d0, ('b',))
    if t == 0x26:                        # tire compound setting (chassis)
        need(1)
        return _payload(data, d0, ('b',))

    if t in (0x21,):                     # int32
        need(4)
        return _payload(data, d0, ('i',))
    if t == 0x22:                        # float32
        need(4)
        return _payload(data, d0, ('f',))

    if t == 0x23:                        # shape byte + payload @ D0+1
        s = data[d0]
        fmts = _TYPE_0x23_SHAPES.get(s)
        if not fmts:
            raise DecodeError(f"unknown type 0x23 subtype 0x{s:02X}", t, (s,))
        need(1 + sum(_size(c) for c in fmts))
        return _payload(data, d0 + 1, fmts)

    if t == 0x24:                        # shape byte(s) + payload
        s1 = data[d0]
        nested_map = _TYPE_0x24_NESTED_SHAPES.get(s1)

        fmts = None
        shape_len = 1
        flat_fmts = _TYPE_0x24_FLAT_SHAPES.get(s1)   # single-byte shapes, incl. A2/92
        if flat_fmts is not None:                    # payload @ D0+1
            fmts = flat_fmts
        elif nested_map is not None and len(data) > d0 + 1:   # two-byte shape
            s2 = data[d0 + 1]
            sub = nested_map.get(s2)
            if sub is None:
                raise DecodeError(f"unknown type 0x24 shapes {s1:#04X} {s2:#04X}", t, (s1, s2))
            fmts = sub
            shape_len = 2
        elif nested_map is not None and len(data) <= d0 + 1:  # truncated record
            raise DecodeError(f"truncated type 0x24 record at id offset {id_off}", t, (s1,))

        if fmts is None:
            raise DecodeError(f"unknown type 0x24 shape {s1:#04X}", t, (s1,))

        need(shape_len + sum(_size(c) for c in fmts))
        return _payload(data, d0 + shape_len, fmts)

    raise DecodeError(f"unsupported record type byte 0x{t:02X} at id offset {id_off}", t)


def _size(code):
    return {'b': 1, 'i': 4, 'f': 4}[code]


# ---------------------------------------------------------------------------
# Convenience helpers for the extraction script
# ---------------------------------------------------------------------------

class FieldSpec:
    """A named field to locate in a binary buffer."""

    def __init__(self, name, id_bytes):
        self.name = name
        self.id_bytes = bytes(id_bytes)

    @property
    def hex_id(self):
        return ' '.join(f'{b:02X}' for b in self.id_bytes)


def extract_field(data: bytes, spec: FieldSpec, search_start=0, search_end=None):
    """Locate `spec` within data[search_start:search_end] and decode it.

    Returns (value_list_or_None, offset). value is None when the type byte is
    0x28 ("Not Used"). Raises LookupError if the identifier is absent in range.
    """
    end = len(data) if search_end is None else min(search_end, len(data))
    off = data.find(spec.id_bytes, search_start, end)
    if off < 0:
        raise LookupError(f"identifier {spec.hex_id} ({spec.name}) not found in [{search_start}:{end}]")
    return decode_field_at(data, off), off
