# -*- coding: utf-8 -*-
"""Gate A validator for ReportJsonWriter.h payloads (plan section 3 schema).

Usage: python validate_report_json.py <payload.json> [expected_language]
   expected_language: 0 = English, 1 = French (optional; enables accent check)

Exits non-zero on any failed assertion. Prints a PASS/FAIL summary line per check.
"""
import json
import sys


def fail(msg):
    print("FAIL:", msg)
    raise SystemExit(1)


def ok(msg):
    print("PASS:", msg)


def main():
    if len(sys.argv) < 2:
        fail("usage: validate_report_json.py <payload.json> [language]")

    path = sys.argv[1]
    expected_lang = int(sys.argv[2]) if len(sys.argv) > 2 else None

    with open(path, "rb") as f:
        raw = f.read()

    # Must be valid UTF-8 (no BOM tolerated at byte level; json.loads handles str).
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        fail("payload is not valid UTF-8: %s" % e)

    data = json.loads(text)  # raises on malformed JSON -> caught below by caller? no, let it raise.

    ok("valid JSON (%d bytes)" % len(raw))

    if data.get("version") != 1:
        fail('top-level "version" must be 1')
    ok("version == 1")

    meta = data.get("meta", {})
    for key in ("carName", "trackName", "lapCountIncluded", "hasData"):
        if key not in meta:
            fail("meta missing key %r" % key)
    if not isinstance(meta["lapCountIncluded"], int):
        fail("meta.lapCountIncluded must be an integer")
    if not isinstance(meta["hasData"], bool):
        fail("meta.hasData must be a boolean")
    ok("meta has carName/trackName/lapCountIncluded(%d)/hasData(%s)" % (meta["lapCountIncluded"], meta["hasData"]))

    sections = data.get("sections", {})
    expected_order = ["Summary", "General", "Tyre", "Brake", "Suspension", "EngineCooling", "Handling"]
    if list(sections.keys()) != expected_order:
        fail('section keys must be exactly %s in order, got %s' % (expected_order, list(sections.keys())))
    for k in expected_order:
        s = sections[k]
        if not isinstance(s.get("content"), str):
            fail("section %r content missing/not string" % k)
        if not isinstance(s.get("hasRecommendation"), bool):
            fail("section %r hasRecommendation missing/not boolean" % k)
    ok('all 7 section keys present in enum order with content+hasRecommendation')

    lap_stats = data.get("lapStats", {})

    for key, expected_len in (("tyreTempsAvg", 12), ("tyreTempsMin", 12), ("tyreTempsMax", 12)):
        if key not in lap_stats:
            ok("%s omitted (no cell data) - allowed" % key)
            continue
        arr = lap_stats[key]
        if len(arr) != expected_len:
            fail('%s must have exactly %d entries, got %d' % (key, expected_len, len(arr)))
        for i, v in enumerate(arr):
            if v is not None and not isinstance(v, (int, float)):
                fail("%s[%d] must be number or null" % (key, i))
    ok("tyre temp arrays: 12 entries each when present")

    brake_avg = lap_stats.get("brakeTempAvg")
    brake_max = lap_stats.get("brakeTempMax")
    if (brake_avg is None) != (brake_max is None):
        fail("brakeTempAvg and brakeTempMax must be all-or-nothing")
    for key, arr in (("brakeTempAvg", brake_avg), ("brakeTempMax", brake_max)):
        if arr is not None:
            if len(arr) != 4 or any(v is None for v in arr):
                fail("%s must have exactly 4 non-null entries when present" % key)
    ok("brake temp arrays all-or-nothing, 4 entries each")

    hist = lap_stats.get("damperHistograms", {})
    if not isinstance(hist, dict):
        fail('lapStats.damperHistograms must be an object')
    for corner in ("LF", "RF", "LR", "RR"):
        h = hist.get(corner)
        if h is None:
            ok("damperHistograms.%s omitted (no samples) - allowed" % corner)
            continue
        bins = h.get("bins")
        if not isinstance(bins, list) or len(bins) != 41:
            fail('damperHistograms.%s.bins must be a 41-element array' % corner)
        for i, b in enumerate(bins):
            if not isinstance(b, int) and (isinstance(b, float) and not b.is_integer()):
                fail("bins[%d] of %s must be an integer" % (i, corner))
        for key in ("mean", "stdDev"):
            v = h.get(key)
            if not isinstance(v, (int, float)):
                fail('damperHistograms.%s.%s missing/not number' % (corner, key))
    ok("damper histograms: 41 bins + mean/stdDev per present corner")

    # French accent check when requested.
    if expected_lang == 1:
        accents = ["é", "è", "à", "ç"]
        found_any = False
        for k in expected_order:
            content = sections[k]["content"]
            for a in accents:
                if a in content:
                    found_any = True
        # Also check meta strings.
        for v in (meta.get("carName", ""), meta.get("trackName", "")):
            for a in accents:
                if a in v:
                    found_any = True
        print('INFO: French accent characters present in payload: %s' % ("yes" if found_any else "no"))

    # Show sample of content strings so encoding can be eyeballed.
    summary = sections["Summary"]["content"][:120].replace("\n", "\\n")
    print("INFO: Summary preview -> %r" % summary)

    ok("ALL CHECKS PASSED for " + path)


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:  # json.JSONDecodeError etc.
        fail("exception during validation: %r" % e)
