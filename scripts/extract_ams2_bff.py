#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
Extract AMS2 physics data from every .bff car pack.

Pipeline per car (see /memories/session/plan.md):
  1. Unpack the BFF with `D:\AMS2\AMS2_Mod\PCTools\unpack AMS2_car.bat`
     (interactive: pipes "<car name>\r\nn\r\n" on stdin -> answers N to the
      "extraire tous les fichiers BFF generaux ?" question).
  2. Decode [PCTools]\<car>\vehicles\physics\chassis\*.CDFBIN and
     \engines\*.EDFBIN with ams2_cdf_decoder (port of ExtractData()).
  3. Merge results into data/reference/ams2_vehicles_db.json under the car's
     "physics" key, then delete the temp folder [PCTools]\<car>.

Stdlib only. Usage:
    python scripts\extract_ams2_bff.py                 # all cars in Steam dir
    python scripts\extract_ams2_bff.py --car Alpine_A110_GT4_Evo   # one car
"""

import argparse
import json
import os
import shutil
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ams2_cdf_decoder import FieldSpec, extract_field, DecodeError  # noqa: E402

# ---------------------------------------------------------------------------
# Paths (exe-relative to project root; Steam/PCTools are fixed external paths)
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parents[1]
DB_FILE = PROJECT_ROOT / "data" / "reference" / "ams2_vehicles_db.json"
WORK_DIR = PROJECT_ROOT / "data" / "working"

STEAM_VEHICLES_DIR = (r"C:\Program Files (x86)\Steam\steamapps\common"
                      r"\Automobilista 2\Pakfiles\Vehicles")
PCTOOLS_DIR = Path(r"D:\AMS2\AMS2_Mod\PCTools")
UNPACK_BAT = PCTOOLS_DIR / "unpack AMS2_car.bat"

# ---------------------------------------------------------------------------
# Field identifiers (4-byte LE ids, from GeneralFunction.cpp::ExtractData)
# ---------------------------------------------------------------------------
CHASSIS_FIELDS = {
    # name: id bytes
    "Mass":            b"\x67\x0B\x57\xAB",   # int32  [kg]
    "CGHeight":        b"\x18\x24\xEA\xA8",   # float  [m]
    "CGRightRange":    b"\xDF\x8D\x93\xCF",   # f,f,f (min,step,max)
    "CGRightSetting":  b"\x00\x9D\x8A\xCF",   # value or NO DATA(0x28)
    "CGRearRange":     b"\xBE\xBA\x67\x7B",   # f,f,f (min,step,max)
    "CGRearSetting":   b"\xD4\x4C\x53\xC4",   # value or NO DATA(0x28)
}

# Brake corner section markers: raw 5-byte delimiters in the CDFBIN.
BRAKE_MARKERS = {
    "FL": bytes.fromhex("E07D16E29F"),
    "FR": bytes.fromhex("E0D6537908"),
    "RL": bytes.fromhex("E0BF9F5BA2"),
    "RR": bytes.fromhex("E0CFF2C932"),
}

BRAKE_FIELDS = {  # per corner, searched only inside that corner's slice
    "OptimumTempRange":       b"\x92\xAA\x06\x24",   # f,f (min,max) [deg C]
    "FadeTorqueLossBelow":    b"\x02\x3F\x01\xF1",   # float fraction 0..1
    "FadeTorqueLossAbove":    b"\x45\x81\x36\x93",   # float fraction 0..1
    "FadeRangeBelow":         b"\xE7\x95\xA8\xD0",   # f,f (min,max) [deg C]
    "FadeRangeAbove":         b"\xD0\x03\x18\x55",   # f,f (min,max) [deg C]
}

ENGINE_FIELDS = {
    "OptimumOilTemp":     b"\xAF\xD7\x8A\xDD",   # float [deg C]
    "OilWarningTemp":     b"\x3F\x6B\x7B\xE7",   # float [deg C]
    "WaterWarningTemp":   b"\x6D\x47\xC1\xB2",   # float [deg C]
}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _single(values):
    """Collapse a 1-element decode list to its scalar; None stays None."""
    if values is None:
        return None
    return values[0] if len(values) == 1 else values


def log(msg):    print(datetime.now().strftime("[%H:%M:%S] ") + msg)


# ---------------------------------------------------------------------------
# Phase B: file parsers
# ---------------------------------------------------------------------------

def parse_chassis(data: bytes, warnings: list) -> dict:
    """Decode a chassis CDFBIN into the physics payload."""
    out = {}

    for name, idb in CHASSIS_FIELDS.items():
        spec = FieldSpec(name, idb)
        try:
            values, _off = extract_field(data, spec)
        except LookupError as e:
            warnings.append(str(e))
            continue
        except DecodeError as e:
            warnings.append(f"{name}: decode error ({e})")
            continue

        if name in ("CGRearRange", "CGRightRange"):
            # Keep the raw f,f,f triple for now; final shape (scalar vs {min,step,max}) is decided below based on the matching *Setting.
            out[name] = list(values) if values else None
        else:  # Mass / CGHeight / *Setting (setting may be NO DATA -> None)
            out[name] = _single(values)

    # --- resolve Range shape from its Setting ---------------------------------
    # Setting == null → fixed position, store scalar (= min of the triple).
    # Setting != null → adjustable parameter, keep {min, step, max}.
    for range_key, setting_key in (("CGRearRange", "CGRearSetting"), ("CGRightRange", "CGRightSetting")):
        raw = out.get(range_key)
        if not isinstance(raw, list):
            continue  # missing / already scalar — leave as-is (None or value[0] fallback below)
        setting = out.get(setting_key)
        if setting is None:
            out[range_key] = raw[0]
        else:
            out[range_key] = {"min": raw[0], "step": raw[1], "max": raw[2]}

    # --- brake corners, bounded by the raw section markers -----------------
    marker_offsets = {}
    for corner, mk in BRAKE_MARKERS.items():
        idx = data.find(mk)
        if idx < 0:
            warnings.append(f"brake marker {corner} not found")
            continue
        marker_offsets[corner] = idx

    # slice per corner: from its own marker to the next one (or EOF).
    slices = {}
    ordered_offs = sorted(marker_offsets.values())
    by_off = {off: corner for corner, off in marker_offsets.items()}
    for i, off in enumerate(ordered_offs):
        end = ordered_offs[i + 1] if i + 1 < len(ordered_offs) else len(data)
        slices[by_off[off]] = (off, end)

    brakes = {}
    for corner, mk in BRAKE_MARKERS.items():
        cdata = {}
        found_any = False
        if corner in slices:
            start, end = slices[corner]
            search_start = start + len(mk)      # fields live after the marker
            for name, idb in BRAKE_FIELDS.items():
                spec = FieldSpec(f"{name}_{corner}", idb)
                try:
                    values, _off = extract_field(data, spec,
                                                 search_start=search_start,
                                                 search_end=end)
                except LookupError as e:
                    warnings.append(str(e))
                    continue
                except DecodeError as e:
                    warnings.append(f"{name} {corner}: decode error ({e})")
                    continue
                if values is None:              # 0x28 "Not Used"
                    cdata[name] = {"value": "not_used"}
                elif name.endswith("Range"):    # f,f pair -> [min, max]
                    cdata[name] = list(values)
                else:                           # scalar float
                    cdata[name] = _single(values)
                found_any = True

        brakes[corner] = cdata if found_any else {}   # missing marker => {} + warning

    out["brakes"] = brakes
    return out


def parse_engine(data: bytes, warnings: list) -> dict:
    """Decode an engine EDFBIN into the physics payload."""
    out = {}
    for name, idb in ENGINE_FIELDS.items():
        spec = FieldSpec(name, idb)
        try:
            values, _off = extract_field(data, spec)
        except LookupError as e:
            warnings.append(str(e))
            continue
        except DecodeError as e:
            warnings.append(f"{name}: decode error ({e})")
            continue
        out[name] = _single(values)
    return out


# ---------------------------------------------------------------------------
# Phase C/D: unpack orchestration + DB merge
# ---------------------------------------------------------------------------

def run_unpack(car_name: str, timeout_s: int):
    """Run the interactive batch for one car; answer N to general-files Q.

    Returns (returncode:int|None, log_text:str). returncode None = timed out.
    Caller must have wiped PCTools of non-tool dirs before calling this and
    should wipe again afterwards — see list_physics_files() docstring.
    """
    stdin_data = f"{car_name}\r\nn\r\n"
    try:
        proc = subprocess.run(
            [str(UNPACK_BAT)],
            input=stdin_data.encode("ascii"),
            capture_output=True,
            timeout=timeout_s,
            cwd=str(PCTOOLS_DIR),
        )
        rc = proc.returncode
        out_b = (proc.stdout or b"") + (b"\n[stderr]\n" if proc.stderr else b"") \
                + (proc.stderr or b"")
    except subprocess.TimeoutExpired as e:
        return None, f"[timeout after {timeout_s}s] " + \
            ((e.output or b"").decode(errors="replace"))

    log_text = out_b.decode("cp850", errors="replace") if rc is not None else ""
    # keep only the tail to bound file size
    return rc, (log_text[-20_000:] if len(log_text) > 20_000 else log_text)


# PCarsTools names its output folder after the INTERNAL vehicle id, not the
# BFF name: Clio_Cup14.bff -> [Kart_01]\..., mini_coopers_SPEC.bff ->
# [BMW_2002_Turbo]\....  So we cannot look in PCTOOLS_DIR/<car>.
#
# Strategy (mtime diffs are unreliable — PCarsTools preserves the original
# file timestamps from inside the BFF, so a stale leftover can have an old
# mtime and pass any "recently modified" test):
#   * _cleanup_non_tool_dirs() runs BEFORE every unpack -> PCTools contains no
#     car folders at all;
#   * therefore ANY *.CDFBIN / *.EDFBIN present AFTER the unpack belongs to
#     THIS BFF, whatever folder it lives in.
def list_physics_files() -> list:
    """All CDFBIN/EDFBIN files currently under PCTools (sorted)."""
    out = []
    for pat in ("*.CDFBIN", "*.EDFBIN"):
        try:
            it = PCTOOLS_DIR.rglob(pat)
            for p in sorted(it):
                if p.is_file():
                    out.append(p)
        except OSError as e:                 # pragma: no cover
            log(f"  [!] physics scan failed ({pat}): {e}")
    return sorted(out, key=lambda p: str(p))


def _cleanup_non_tool_dirs():
    """Delete every non-tool top-level dir in PCTools (car + *_extracted).

    Guarantees a pristine tree for the next car's snapshot diff.  The numbered
    tool folders (02_bmt2xml, ...) are kept.
    """
    try:
        entries = list(PCTOOLS_DIR.iterdir())
    except OSError as e:                     # pragma: no cover
        log(f"  [!] cannot list PCTools for cleanup: {e}")
        return
    for d in entries:
        if not (d.is_dir() and re.match(r"^\d+_", d.name)):
            try:
                shutil.rmtree(d, ignore_errors=True)
            except Exception as e:           # pragma: no cover
                log(f"  [!] could not remove {d}: {e}")


def process_car(bff_path: Path, db: dict, log_entries: list, timeout_s: int):
    """Full pipeline for one .bff. Returns True on success."""
    car = bff_path.stem
    warnings = []

    # 1) unpack.  PCTools must be free of car folders before we start (main()
    #    and every prior process_car call guarantee this via
    #    _cleanup_non_tool_dirs); sanity-check it:
    stale = list_physics_files()
    if stale:
        log(f"  [!] WARNING: physics files present BEFORE unpack "
            f"({len(stale)}): {stale[:3]} — wiping")
        for p in stale:
            try:
                p.unlink()
            except OSError as e:             # pragma: no cover
                log(f"  [!] could not remove stale {p}: {e}")
    rc, bat_log = run_unpack(car, timeout_s)
    if rc is None or rc != 0:
        entry = {"car": car, "status": "unpack_failed",
                 "returncode": rc,
                 "time": datetime.now(timezone.utc).isoformat()}
        log_entries.append(entry)
        _save_bat_log(car, bat_log)
        # wipe whatever the BAT left behind so it cannot contaminate the
        # NEXT car (stale *_extracted folders are exactly how cross-
        # contamination happened in the first batch run).
        _cleanup_non_tool_dirs()
        return False

    new_files = list_physics_files()          # all belong to THIS BFF now
    cdfbin_path = next((p for p in new_files if p.suffix.upper() == ".CDFBIN"), None)
    edfbin_path = next((p for p in new_files if p.suffix.upper() == ".EDFBIN"), None)
    if len(new_files) > 2:
        log(f"  [!] {len(new_files)} physics files found (expected <=2): "
            f"{[str(p.relative_to(PCTOOLS_DIR)) for p in new_files]}")

    physics = {}
    engine_out = {}
    if cdfbin_path is None:
        warnings.append("no new CDFBIN appeared after unpack")
    else:
        data = cdfbin_path.read_bytes()
        physics["chassis"] = parse_chassis(data, warnings)

    if edfbin_path is None:
        warnings.append("no new EDFBIN appeared after unpack")
    else:
        data = edfbin_path.read_bytes()
        engine_out["engine"] = parse_engine(data, warnings)

    if not cdfbin_path and not edfbin_path:
        entry = {"car": car, "status": "no_physics_files",
                 "time": datetime.now(timezone.utc).isoformat()}
        log_entries.append(entry)
        _save_bat_log(car, bat_log + "\n[warn] no physics files found")
        return False

    # 3) merge into DB ---------------------------------------------------------
    car_entry = db.setdefault(car, {})
    merged = dict(car_entry.get("physics") or {})   # idempotent per-car overwrite
    files_map = dict(merged.get("_files") or {})

    if cdfbin_path is not None:
        merged["chassis"] = physics.get("chassis", {})
        files_map["chassis"] = str(cdfbin_path.name)
    if edfbin_path is not None:
        merged["engine"] = engine_out.get("engine", {})
        files_map["engine"] = str(edfbin_path.name)

    if files_map:
        merged["_files"] = files_map
    car_entry["physics"] = merged
    if warnings:                                # replace stale list on re-runs
        merged["_warnings"] = warnings
    else:
        merged.pop("_warnings", None)

    # 4) cleanup — wipe ALL non-tool dirs (PCarsTools may have created a folder
    #    named after the internal vehicle id, e.g. Kart_01 for Clio_Cup14)
    _cleanup_non_tool_dirs()

    entry = {"car": car, "status": "ok",
             "warnings_count": len(warnings),
             "time": datetime.now(timezone.utc).isoformat()}
    if warnings:
        entry["warnings"] = warnings[:20]
    log_entries.append(entry)
    _save_bat_log(car, bat_log + ("\n[warn]\n" + "\n".join(warnings)
                                  if warnings else ""))

    mass = (physics.get("chassis") or {}).get("Mass")
    oil = (engine_out.get("engine") or {}).get("OptimumOilTemp")
    log(f"  OK {car} | Mass={mass} kg, OptOil={oil}")
    return True


def _save_bat_log(car: str, text: str):
    try:
        WORK_DIR.mkdir(parents=True, exist_ok=True)
        (WORK_DIR / f"unpack_{car}.log").write_text(text, encoding="utf-8")
    except Exception as e:                       # pragma: no cover
        log(f"  [!] could not save bat log for {car}: {e}")


def load_db() -> dict:
    if DB_FILE.exists():
        try:
            return json.loads(DB_FILE.read_text(encoding="utf-8"))
        except Exception as e:
            raise SystemExit(f"[FATAL] cannot read existing DB "
                             f"{DB_FILE}: {e}")
    return {}


def save_db(db: dict):
    DB_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = DB_FILE.with_suffix(".json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(db, f, indent=4, ensure_ascii=False)
    os.replace(str(tmp), str(DB_FILE))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--car", help="process a single car (BFF basename)")
    ap.add_argument("--timeout", type=int, default=900,
                    help="per-car unpack timeout in seconds (default 900)")
    args = ap.parse_args()

    if not UNPACK_BAT.exists():
        raise SystemExit(f"[FATAL] batch script missing: {UNPACK_BAT}")
    if not Path(STEAM_VEHICLES_DIR).is_dir():
        raise SystemExit(
            f"[FATAL] Steam vehicles dir missing: {STEAM_VEHICLES_DIR}")

    bffs = sorted(Path(STEAM_VEHICLES_DIR).glob("*.bff"))
    if args.car:
        wanted = [b for b in bffs if b.stem == args.car]
        if not wanted:
            raise SystemExit(f"[FATAL] no BFF named {args.car!r} found")
        bffs = wanted
    else:
        # Full batch: start from a pristine PCTools so the snapshot diff is
        # meaningful (leftover car folders would be mis-attributed).
        _cleanup_non_tool_dirs()

    log(f"{len(bffs)} car(s) to process. DB -> {DB_FILE}")
    db = load_db()
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    log_file = WORK_DIR / "bff_extract_log.json"

    prev_entries = []
    if log_file.exists():
        try:
            prev_entries = json.loads(log_file.read_text(encoding="utf-8"))
        except Exception:
            pass

    def _flush():
        """Persist DB + log after every car so a crash loses no progress.

        Log is deduped per-car (latest entry wins) so re-runs of individual
        cars don't accumulate duplicate lines.
        """
        save_db(db)
        latest = {}
        for e in prev_entries:
            if isinstance(e, dict) and "car" in e:
                latest[e["car"]] = e
        with open(log_file, "w", encoding="utf-8") as f:
            json.dump(list(latest.values()), f,
                      indent=2, ensure_ascii=False)

    ok = fail = 0
    for i, bff in enumerate(bffs, 1):
        log(f"[{i}/{len(bffs)}] {bff.stem}")
        if process_car(bff, db, prev_entries, args.timeout):
            ok += 1
        else:
            fail += 1
        _flush()

    _flush()

    log("=" * 50)
    log(f"Done. ok={ok} failed={fail}. DB updated ({len(db)} cars total).")


if __name__ == "__main__":
    main()
