#!/usr/bin/env python3
"""
Clean and prepare the six energy-dispatch scenarios for model training.

Design goals:
1. Keep the data physically realistic for the current STM32F4 rule/state-machine logic.
2. Preserve the user's compressed-time experiment: 40 min recorded data represents about 6 h.
3. Remove inputs that should not train the energy model, especially heart rate and SpO2.
4. Produce a separate logic-difference report instead of silently hiding all mismatches.
5. Avoid artificial cross-scene transitions that do not happen in the real system.
"""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "ml_energy" / "outputs"
OUT_DIR.mkdir(parents=True, exist_ok=True)


@dataclass(frozen=True)
class SceneConfig:
    scene_id: int
    label: str
    file_suffix: str
    real_start_hour: float
    real_duration_h: float
    description: str


# 40 min experiment ~= 6 h real scene. 15 min ~= 2.25 h, 10 min ~= 1.5 h.
SCENE_CONFIGS = {
    1: SceneConfig(1, "T1_night_no_pv", "12.CSV", 0.0, 6.0, "0-6 night, no useful PV"),
    2: SceneConfig(2, "T2_dawn_weak_light", "12.CSV", 6.0, 6.0, "weak/unstable PV"),
    3: SceneConfig(3, "T3_noon_strong_light", "3.CSV", 12.0, 6.0, "strong PV, PV > load"),
    4: SceneConfig(4, "T4_home_full_car_charge", "4.CSV", 12.0, 2.25, "strong PV, home SOC >= 90%"),
    5: SceneConfig(5, "T5_dusk_load_increase", "5.CSV", 18.0, 6.0, "light decreases, load increases"),
    6: SceneConfig(6, "T6_low_home_v2h", "6.CSV", 0.0, 1.5, "home SOC <= 20%, no PV, V2H"),
}

RAW_COLUMNS = [
    "record_type", "tick_ms", "scene_raw", "lux", "pv_ok", "pv_v", "pv_i", "pv_p",
    "home_ok", "home_v", "home_i", "home_p", "home_soc",
    "load_ok", "load_v", "load_i", "load_p",
    "car_soc", "s3_valid", "s3_v", "s3_soc", "s3_hr", "s3_spo2", "s3_state",
    "pvsrc", "hsrc", "rigid", "led", "fan", "qi", "hchg", "cchg", "v2h",
]

# Inputs retained for LSTM/TFLite. Heart rate and SpO2 are intentionally absent.
INPUT_FEATURES = [
    "real_hour_sin", "real_hour_cos",
    "lux", "pv_v", "pv_p",
    "home_v", "home_soc",
    "load_p",
    "car_soc", "human_soc",
    "pvsrc", "hsrc", "rigid", "led", "fan", "qi", "hchg", "cchg", "v2h",
]

OUTPUT_TARGETS = [
    "future_pv_p", "future_load_p", "future_home_soc",
]

SEQ_LEN = 12                 # 12 samples * 1 min = 12 min history in training CSV
RESAMPLE_PERIOD_S = 60       # real-time training interval
FORECAST_HORIZON_MIN = 10    # prediction target 10 real minutes ahead
FUTURE_TARGET_AVG_MIN = 5     # target is future-window average, not one instantaneous point
PV_TARGET_MAX_RISE_W = 1.5    # avoid learning unrealistic PV jumps in one forecast step
PV_TARGET_MAX_RISE_RATIO = 0.45
PV_TARGET_TREND_GAIN = 1.2
PV_LOW_LIGHT_LUX = 150.0
PV_LOW_LIGHT_MAX_RISE_W = 0.2
HOME_SOC_TARGET_MAX_DROP_PCT = 3.0   # 10 min forecast should not learn voltage-step SOC collapse
HOME_SOC_TARGET_MAX_RISE_PCT = 3.0
STABLE_NIGHT_AUGMENT = True
STABLE_NIGHT_BATTERY_WH = 16.3

SOC_VOLTAGE_MV = np.array([6500, 6800, 7200, 7600, 8000, 8400], dtype=np.float32)
SOC_PERCENT = np.array([0, 20, 40, 65, 90, 100], dtype=np.float32)


def voltage_to_home_soc(voltage: pd.Series) -> pd.Series:
    mv = voltage.astype(float).to_numpy() * 1000.0
    soc = np.interp(mv, SOC_VOLTAGE_MV, SOC_PERCENT, left=0.0, right=100.0)
    return pd.Series(soc, index=voltage.index)


def home_soc_to_voltage(soc: np.ndarray) -> np.ndarray:
    mv = np.interp(soc.astype(float), SOC_PERCENT, SOC_VOLTAGE_MV, left=6500.0, right=8400.0)
    return mv / 1000.0


def find_file_by_suffix(suffix: str) -> Path:
    matches = sorted([p for p in ROOT.glob("*.CSV") if p.name.endswith(suffix)])
    if not matches:
        matches = sorted([p for p in ROOT.glob("*.csv") if p.name.endswith(suffix)])
    if not matches:
        raise FileNotFoundError(f"Cannot find CSV ending with {suffix}")
    return matches[0]


def parse_logger_csv(path: Path) -> pd.DataFrame:
    """Logger rows are often stored as one quoted cell containing DATA,..."""
    rows: list[list[str]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for outer in csv.reader(f):
            if not outer:
                continue
            line = outer[0].strip()
            if not line:
                continue
            parsed = next(csv.reader([line]))
            if len(parsed) == 1 and "," in parsed[0]:
                parsed = parsed[0].split(",")
            rows.append(parsed)

    if not rows:
        return pd.DataFrame(columns=RAW_COLUMNS)

    header = rows[0]
    data_rows = [r for r in rows[1:] if len(r) == len(header)]
    df = pd.DataFrame(data_rows, columns=RAW_COLUMNS[: len(header)])
    for col in df.columns:
        if col != "record_type":
            df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df[df["record_type"].eq("DATA")].copy()
    return df


def split_scene12(df12: pd.DataFrame) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Split T1/T2 stored in one file by sustained PV/lux rise."""
    df12 = df12.sort_values("tick_ms").reset_index(drop=True)
    weak_light = (df12["pv_v"].rolling(3, min_periods=1).mean() > 1.5) & (
        df12["lux"].rolling(3, min_periods=1).mean() > 200.0
    )
    if weak_light.any():
        split_idx = int(np.argmax(weak_light.to_numpy()))
    else:
        pvsrc = df12["pvsrc"].fillna(0).to_numpy()
        split_idx = int(np.argmax(pvsrc == 1)) if (pvsrc == 1).any() else len(df12)

    # Keep enough rows in both halves if the automatic threshold triggers too early.
    split_idx = max(20, min(split_idx, len(df12) - 20))
    return df12.iloc[:split_idx].copy(), df12.iloc[split_idx:].copy()


def load_raw_scenes() -> pd.DataFrame:
    frames = []
    scene12 = parse_logger_csv(find_file_by_suffix("12.CSV"))
    t1, t2 = split_scene12(scene12)
    for scene_id, part in [(1, t1), (2, t2)]:
        cfg = SCENE_CONFIGS[scene_id]
        part = part.copy()
        part["scene_id"] = scene_id
        part["scene_label"] = cfg.label
        part["source_file"] = find_file_by_suffix("12.CSV").name
        frames.append(part)

    for scene_id in [3, 4, 5, 6]:
        cfg = SCENE_CONFIGS[scene_id]
        path = find_file_by_suffix(cfg.file_suffix)
        df = parse_logger_csv(path)
        df["scene_id"] = scene_id
        df["scene_label"] = cfg.label
        df["source_file"] = path.name
        frames.append(df)

    return pd.concat(frames, ignore_index=True)


def add_real_time(df: pd.DataFrame) -> pd.DataFrame:
    out = []
    for scene_id, g in df.groupby("scene_id", sort=True):
        cfg = SCENE_CONFIGS[int(scene_id)]
        g = g.sort_values("tick_ms").copy()
        pos = np.linspace(0.0, 1.0, len(g)) if len(g) > 1 else np.zeros(len(g))
        g["real_elapsed_s"] = pos * cfg.real_duration_h * 3600.0
        g["real_hour"] = (cfg.real_start_hour + g["real_elapsed_s"] / 3600.0) % 24.0
        g["real_hour_sin"] = np.sin(2.0 * np.pi * g["real_hour"] / 24.0)
        g["real_hour_cos"] = np.cos(2.0 * np.pi * g["real_hour"] / 24.0)
        g["real_duration_h"] = cfg.real_duration_h
        out.append(g)
    return pd.concat(out, ignore_index=True)


def apply_physical_cleaning(df: pd.DataFrame) -> tuple[pd.DataFrame, list[dict]]:
    df = df.copy()
    changes: list[dict] = []

    # Remove scene warm-up rows where source MOS state has not yet caught up.
    warmup_n = 3
    before = len(df)
    df = pd.concat(
        [g.sort_values("tick_ms").iloc[warmup_n:] for _, g in df.groupby("scene_id", sort=True)],
        ignore_index=True,
    )
    changes.append({"type": "drop_warmup", "rows": before - len(df), "reason": "first 30s are startup transients"})

    # Clip continuous sensors to realistic ranges.
    ranges = {
        "lux": (0.0, 2500.0),
        "pv_v": (0.0, 12.0),
        "pv_i": (0.0, 1.5),
        "pv_p": (0.0, 20.0),
        "home_v": (0.0, 8.5),
        "home_i": (-3.0, 3.0),
        "home_p": (-25.0, 25.0),
        "home_soc": (0.0, 100.0),
        "load_v": (0.0, 5.5),
        "load_i": (0.0, 3.0),
        "load_p": (0.0, 20.0),
        "car_soc": (0.0, 100.0),
        "s3_v": (-1.0, 5.0),
        "s3_soc": (-1.0, 100.0),
    }
    for col, (lo, hi) in ranges.items():
        bad = df[col].notna() & ((df[col] < lo) | (df[col] > hi))
        if bad.any():
            changes.append({"type": "clip", "column": col, "rows": int(bad.sum()), "range": [lo, hi]})
            df[col] = df[col].clip(lo, hi)

    switch_cols = ["pv_ok", "home_ok", "load_ok", "s3_valid", "pvsrc", "hsrc", "rigid", "led", "fan", "qi", "hchg", "cchg", "v2h"]
    for col in switch_cols:
        df[col] = df[col].fillna(0).round().clip(0, 1).astype(int)

    # The home battery output cuts off around 6.5V, so SOC is recalculated from
    # the same 6.5V-8.4V table used by the STM32 firmware.
    raw_home_soc = df["home_soc"].copy()
    df["home_soc_raw"] = raw_home_soc
    df["home_soc"] = voltage_to_home_soc(df["home_v"]).clip(0.0, 100.0)
    soc_changed = (df["home_soc"] - raw_home_soc).abs() > 1.0
    if soc_changed.any():
        changes.append({
            "type": "recalculate_home_soc_from_6p5v_cutoff",
            "rows": int(soc_changed.sum()),
            "reason": "home battery output stops near 6.5V, old 6.0V SOC table overstated low SOC",
        })

    # Human wearable: keep SOC for Qi policy, remove HR/SpO2 from model inputs.
    df.loc[df["s3_valid"].eq(0), ["s3_v", "s3_soc"]] = [-1.0, -1.0]
    df["human_soc"] = np.where(df["s3_valid"].eq(1), df["s3_soc"], -1.0)
    df = df.drop(columns=["s3_hr", "s3_spo2"], errors="ignore")

    # Keep measured powers coherent after clipping.
    df["pv_p"] = (df["pv_v"] * df["pv_i"]).clip(0.0, 20.0)
    df["load_p"] = (df["load_v"] * df["load_i"]).clip(0.0, 20.0)
    df["home_p"] = (df["home_v"] * df["home_i"]).clip(-25.0, 25.0)
    df["pv_surplus_w"] = df["pv_p"] - df["load_p"]

    return df, changes


def find_logic_differences(df: pd.DataFrame) -> pd.DataFrame:
    columns = [
        "row_index", "scene_id", "scene_label", "real_hour", "tick_ms", "kind",
        "expected_by_code_logic", "observed", "lux", "pv_v", "pv_p", "home_v",
        "home_soc", "load_p", "car_soc", "pvsrc", "hsrc", "v2h", "hchg", "cchg",
    ]
    rows = []
    for idx, r in df.iterrows():
        source_count = int(r["pvsrc"] + r["hsrc"] + r["v2h"])
        load_on = int(r["rigid"] + r["led"] + r["fan"] + r["qi"]) > 0

        def add(kind: str, expected: str, observed: str) -> None:
            rows.append({
                "row_index": int(idx),
                "scene_id": int(r["scene_id"]),
                "scene_label": r["scene_label"],
                "real_hour": round(float(r["real_hour"]), 4),
                "tick_ms": int(r["tick_ms"]),
                "kind": kind,
                "expected_by_code_logic": expected,
                "observed": observed,
                "lux": float(r["lux"]),
                "pv_v": float(r["pv_v"]),
                "pv_p": float(r["pv_p"]),
                "home_v": float(r["home_v"]),
                "home_soc": float(r["home_soc"]),
                "load_p": float(r["load_p"]),
                "car_soc": float(r["car_soc"]),
                "pvsrc": int(r["pvsrc"]),
                "hsrc": int(r["hsrc"]),
                "v2h": int(r["v2h"]),
                "hchg": int(r["hchg"]),
                "cchg": int(r["cchg"]),
            })

        if source_count == 0 and load_on:
            add("load_without_source", "loads off unless pvsrc/hsrc/v2h is available", f"loads_on, source_count={source_count}")
        if r["pvsrc"] == 1 and r["pv_v"] < 5.5:
            add("pvsrc_low_pv_voltage", "pvsrc requires loaded PV voltage to be usable", f"pv_v={r['pv_v']:.3f}")
        if r["pvsrc"] == 1 and r["hsrc"] == 1:
            add("pv_and_home_source_overlap", "software prefers one source at a time", "pvsrc=1,hsrc=1")
        if r["hchg"] == 1 and r["cchg"] == 1:
            add("dual_charge_outputs", "home and car charge MOS should not be on together", "hchg=1,cchg=1")
        if r["hchg"] == 1 and r["pv_v"] <= r["home_v"]:
            add("home_charge_voltage_invalid", "home charge requires PV voltage > home battery voltage", f"pv_v={r['pv_v']:.3f},home_v={r['home_v']:.3f}")
        if r["cchg"] == 1 and r["home_soc"] < 90.0:
            add("car_charge_before_home_full", "car charge only after home_soc >= 90%", f"home_soc={r['home_soc']:.1f}")
        if r["v2h"] == 1 and r["car_soc"] <= 20.0:
            add("v2h_car_low", "V2H stops when car_soc <= 20%", f"car_soc={r['car_soc']:.1f}")
        if r["v2h"] == 1 and r["pvsrc"] == 1:
            add("v2h_with_pv_source", "V2H stops when PV recovers", "v2h=1,pvsrc=1")

    return pd.DataFrame(rows, columns=columns)


def enforce_rule_consistency(df: pd.DataFrame) -> tuple[pd.DataFrame, list[dict]]:
    """Fix only derived training states. The raw difference report preserves what changed."""
    df = df.copy()
    fixes = []

    source_count = df[["pvsrc", "hsrc", "v2h"]].sum(axis=1)
    no_source = source_count.eq(0)
    loads = ["rigid", "led", "fan", "qi"]
    bad_load = no_source & df[loads].sum(axis=1).gt(0)
    if bad_load.any():
        df.loc[bad_load, loads] = 0
        fixes.append({"type": "turn_off_load_without_source", "rows": int(bad_load.sum())})

    bad_hchg = df["hchg"].eq(1) & (df["pv_v"] <= df["home_v"])
    if bad_hchg.any():
        df.loc[bad_hchg, "hchg"] = 0
        fixes.append({"type": "disable_home_charge_pv_not_above_home", "rows": int(bad_hchg.sum())})

    bad_cchg = df["cchg"].eq(1) & (df["home_soc"] < 90.0)
    if bad_cchg.any():
        df.loc[bad_cchg, "cchg"] = 0
        fixes.append({"type": "disable_car_charge_before_home_full", "rows": int(bad_cchg.sum())})

    dual_charge = df["hchg"].eq(1) & df["cchg"].eq(1)
    if dual_charge.any():
        df.loc[dual_charge, "cchg"] = 0
        fixes.append({"type": "disable_car_charge_when_home_charge_on", "rows": int(dual_charge.sum())})

    df["active_sources"] = df[["pvsrc", "hsrc", "v2h"]].sum(axis=1)
    df["comfort_loads_on"] = df[["led", "fan", "qi"]].sum(axis=1)
    return df, fixes


def resample_to_real_minutes(df: pd.DataFrame) -> pd.DataFrame:
    """Create a realistic-time training set. No cross-scene synthetic transitions."""
    continuous = [
        "real_elapsed_s", "real_hour", "real_hour_sin", "real_hour_cos", "lux",
        "pv_v", "pv_i", "pv_p", "home_v", "home_i", "home_p", "home_soc",
        "load_v", "load_i", "load_p", "car_soc", "s3_v", "s3_soc", "human_soc",
        "pv_surplus_w",
    ]
    discrete = ["pv_ok", "home_ok", "load_ok", "s3_valid", "pvsrc", "hsrc", "rigid", "led", "fan", "qi", "hchg", "cchg", "v2h"]
    frames = []

    for scene_id, g in df.groupby("scene_id", sort=True):
        cfg = SCENE_CONFIGS[int(scene_id)]
        g = g.sort_values("real_elapsed_s").copy()
        if len(g) < 2:
            continue
        new_t = np.arange(0.0, cfg.real_duration_h * 3600.0 + 0.1, RESAMPLE_PERIOD_S)
        out = pd.DataFrame({"real_elapsed_s": new_t})
        old_t = g["real_elapsed_s"].to_numpy(dtype=float)
        for col in continuous:
            if col == "real_elapsed_s":
                continue
            out[col] = np.interp(new_t, old_t, g[col].to_numpy(dtype=float))
        for col in discrete:
            vals = np.interp(new_t, old_t, g[col].to_numpy(dtype=float))
            out[col] = np.rint(vals).clip(0, 1).astype(int)
        out["scene_id"] = int(scene_id)
        out["scene_label"] = cfg.label
        out["source_file"] = "real_time_resampled"
        out["real_duration_h"] = cfg.real_duration_h
        out["sample_period_s"] = RESAMPLE_PERIOD_S
        out["real_hour"] = (cfg.real_start_hour + out["real_elapsed_s"] / 3600.0) % 24.0
        out["real_hour_sin"] = np.sin(2.0 * np.pi * out["real_hour"] / 24.0)
        out["real_hour_cos"] = np.cos(2.0 * np.pi * out["real_hour"] / 24.0)
        frames.append(out)

    resampled = pd.concat(frames, ignore_index=True)
    resampled["pv_p"] = (resampled["pv_v"] * resampled["pv_i"]).clip(0.0, 20.0)
    resampled["load_p"] = (resampled["load_v"] * resampled["load_i"]).clip(0.0, 20.0)
    resampled["home_p"] = (resampled["home_v"] * resampled["home_i"]).clip(-25.0, 25.0)
    resampled["pv_surplus_w"] = resampled["pv_p"] - resampled["load_p"]
    resampled, _ = enforce_rule_consistency(resampled)
    return resampled


def augment_stable_night_load(df: pd.DataFrame) -> pd.DataFrame:
    """Add physically constrained night samples for loads observed in hardware tests."""
    if not STABLE_NIGHT_AUGMENT:
        return df

    frames = [df]
    scene_id = 100
    duration_min = 120
    times_s = np.arange(0, (duration_min + 1) * RESAMPLE_PERIOD_S, RESAMPLE_PERIOD_S, dtype=float)
    load_levels = [0.9, 1.3, 1.7]
    soc_starts = [60.0, 70.0, 80.0, 90.0]

    for load_w in load_levels:
        for soc_start in soc_starts:
            scene_id += 1
            elapsed_h = times_s / 3600.0
            soc_drop = (load_w * elapsed_h / STABLE_NIGHT_BATTERY_WH) * 100.0
            home_soc = np.clip(soc_start - soc_drop, 0.0, 100.0)
            home_v = home_soc_to_voltage(home_soc)
            load_v = np.full_like(times_s, 4.85, dtype=float)
            load_i = load_w / load_v
            real_hour = (times_s / 3600.0) % 24.0
            lux = np.full_like(times_s, 55.0, dtype=float)

            out = pd.DataFrame({
                "real_elapsed_s": times_s,
                "real_hour": real_hour,
                "real_hour_sin": np.sin(2.0 * np.pi * real_hour / 24.0),
                "real_hour_cos": np.cos(2.0 * np.pi * real_hour / 24.0),
                "lux": lux,
                "pv_v": np.full_like(times_s, 0.66, dtype=float),
                "pv_i": np.zeros_like(times_s, dtype=float),
                "pv_p": np.zeros_like(times_s, dtype=float),
                "home_v": home_v,
                "home_i": load_w / home_v,
                "home_p": np.full_like(times_s, load_w, dtype=float),
                "home_soc": home_soc,
                "load_v": load_v,
                "load_i": load_i,
                "load_p": np.full_like(times_s, load_w, dtype=float),
                "car_soc": np.full_like(times_s, 64.0, dtype=float),
                "s3_v": np.full_like(times_s, 3.98, dtype=float),
                "s3_soc": np.full_like(times_s, 75.0, dtype=float),
                "human_soc": np.full_like(times_s, 75.0, dtype=float),
                "pv_surplus_w": np.full_like(times_s, -load_w, dtype=float),
                "pv_ok": np.zeros_like(times_s, dtype=int),
                "home_ok": np.ones_like(times_s, dtype=int),
                "load_ok": np.ones_like(times_s, dtype=int),
                "s3_valid": np.ones_like(times_s, dtype=int),
                "pvsrc": np.zeros_like(times_s, dtype=int),
                "hsrc": np.ones_like(times_s, dtype=int),
                "rigid": np.ones_like(times_s, dtype=int),
                "led": np.full_like(times_s, 1 if load_w >= 1.2 else 0, dtype=int),
                "fan": np.zeros_like(times_s, dtype=int),
                "qi": np.zeros_like(times_s, dtype=int),
                "hchg": np.zeros_like(times_s, dtype=int),
                "cchg": np.zeros_like(times_s, dtype=int),
                "v2h": np.zeros_like(times_s, dtype=int),
                "scene_id": scene_id,
                "scene_label": f"T1_aug_night_load_{load_w:.1f}W_soc_{int(soc_start)}",
                "source_file": "stable_night_physics_aug",
                "real_duration_h": duration_min / 60.0,
                "sample_period_s": RESAMPLE_PERIOD_S,
            })
            frames.append(out)

    return pd.concat(frames, ignore_index=True)


def add_training_targets(df: pd.DataFrame) -> pd.DataFrame:
    rows = []
    horizon_steps = max(1, int(round(FORECAST_HORIZON_MIN * 60 / RESAMPLE_PERIOD_S)))
    avg_steps = max(1, int(round(FUTURE_TARGET_AVG_MIN * 60 / RESAMPLE_PERIOD_S)))
    history_steps = max(2, SEQ_LEN)
    for scene_id, g in df.groupby("scene_id", sort=True):
        g = g.sort_values("real_elapsed_s").copy()
        future_pv = g["pv_p"].shift(-horizon_steps).rolling(avg_steps, min_periods=1).mean()
        future_load = g["load_p"].shift(-horizon_steps).rolling(avg_steps, min_periods=1).mean()
        future_soc = g["home_soc"].shift(-horizon_steps).rolling(avg_steps, min_periods=1).mean()

        pv_now = g["pv_p"].astype(float)
        pv_prev = g["pv_p"].shift(history_steps - 1)
        lux_now = g["lux"].astype(float)
        lux_prev = g["lux"].shift(history_steps - 1)
        pv_trend = ((pv_now - pv_prev) / float(history_steps - 1)).fillna(0.0)
        lux_trend = ((lux_now - lux_prev) / float(history_steps - 1)).fillna(0.0)
        pv_allowed_rise = np.maximum(
            PV_TARGET_MAX_RISE_W,
            pv_now * PV_TARGET_MAX_RISE_RATIO,
        ) + np.maximum(0.0, pv_trend) * horizon_steps * PV_TARGET_TREND_GAIN
        low_light_stable = (lux_now < PV_LOW_LIGHT_LUX) & (lux_trend <= 0.0) & (pv_trend <= 0.0)
        pv_allowed_rise = np.where(low_light_stable, PV_LOW_LIGHT_MAX_RISE_W, pv_allowed_rise)

        g["future_pv_p"] = np.minimum(future_pv, pv_now + pv_allowed_rise)
        g["future_load_p"] = future_load
        g["future_home_soc"] = future_soc.clip(
            lower=g["home_soc"] - HOME_SOC_TARGET_MAX_DROP_PCT,
            upper=g["home_soc"] + HOME_SOC_TARGET_MAX_RISE_PCT,
        ).clip(0.0, 100.0)

        rows.append(g)

    out = pd.concat(rows, ignore_index=True)
    return out.dropna(subset=OUTPUT_TARGETS).reset_index(drop=True)


def build_sequence_npz(training_rows: pd.DataFrame) -> dict:
    X, y, meta = [], [], []
    for scene_id, g in training_rows.groupby("scene_id", sort=True):
        g = g.sort_values("real_elapsed_s").reset_index(drop=True)
        features = g[INPUT_FEATURES].to_numpy(dtype=np.float32)
        targets = g[OUTPUT_TARGETS].to_numpy(dtype=np.float32)
        for end in range(SEQ_LEN - 1, len(g)):
            X.append(features[end - SEQ_LEN + 1:end + 1])
            y.append(targets[end])
            meta.append({
                "scene_id": int(scene_id),
                "scene_label": str(g.loc[end, "scene_label"]),
                "real_hour": float(g.loc[end, "real_hour"]),
                "real_elapsed_s": float(g.loc[end, "real_elapsed_s"]),
            })

    X_raw = np.asarray(X, dtype=np.float32)
    y_raw = np.asarray(y, dtype=np.float32)
    x_min = X_raw.reshape(-1, X_raw.shape[-1]).min(axis=0)
    x_max = X_raw.reshape(-1, X_raw.shape[-1]).max(axis=0)
    x_range = np.where(np.abs(x_max - x_min) < 1e-6, 1.0, x_max - x_min)
    y_min = y_raw.min(axis=0)
    y_max = y_raw.max(axis=0)
    y_range = np.where(np.abs(y_max - y_min) < 1e-6, 1.0, y_max - y_min)
    X = (X_raw - x_min) / x_range
    y = (y_raw - y_min) / y_range

    np.savez_compressed(OUT_DIR / "energy_lstm_dataset_realistic.npz", X=X.astype(np.float32), y=y.astype(np.float32), X_raw=X_raw, y_raw=y_raw)
    pd.DataFrame(meta).to_csv(OUT_DIR / "energy_lstm_sequence_meta.csv", index=False, encoding="utf-8-sig")

    scaler = {
        "input_features": INPUT_FEATURES,
        "output_targets": OUTPUT_TARGETS,
        "sequence_length": SEQ_LEN,
        "resample_period_s": RESAMPLE_PERIOD_S,
        "forecast_horizon_min": FORECAST_HORIZON_MIN,
        "future_target_avg_min": FUTURE_TARGET_AVG_MIN,
        "pv_target_max_rise_w": PV_TARGET_MAX_RISE_W,
        "pv_target_max_rise_ratio": PV_TARGET_MAX_RISE_RATIO,
        "pv_low_light_lux": PV_LOW_LIGHT_LUX,
        "pv_low_light_max_rise_w": PV_LOW_LIGHT_MAX_RISE_W,
        "home_soc_target_max_drop_pct": HOME_SOC_TARGET_MAX_DROP_PCT,
        "home_soc_target_max_rise_pct": HOME_SOC_TARGET_MAX_RISE_PCT,
        "stable_night_augment": STABLE_NIGHT_AUGMENT,
        "stable_night_battery_wh": STABLE_NIGHT_BATTERY_WH,
        "x_min": x_min.tolist(),
        "x_range": x_range.tolist(),
        "y_min": y_min.tolist(),
        "y_range": y_range.tolist(),
    }
    (OUT_DIR / "energy_scaler_realistic.json").write_text(json.dumps(scaler, indent=2), encoding="utf-8")
    return {"sequences": int(len(X)), "feature_count": len(INPUT_FEATURES), "target_count": len(OUTPUT_TARGETS)}


def build_summary(cleaned: pd.DataFrame, training_rows: pd.DataFrame, diffs: pd.DataFrame, clean_changes: list[dict], fixes: list[dict], seq_info: dict) -> dict:
    scene_summary = {}
    for scene_id, g in cleaned.groupby("scene_id", sort=True):
        cfg = SCENE_CONFIGS[int(scene_id)]
        scene_summary[cfg.label] = {
            "rows_cleaned": int(len(g)),
            "real_start_hour": cfg.real_start_hour,
            "real_duration_h": cfg.real_duration_h,
            "avg_lux": round(float(g["lux"].mean()), 3),
            "pvsrc_pct": round(float(g["pvsrc"].mean() * 100), 3),
            "hsrc_pct": round(float(g["hsrc"].mean() * 100), 3),
            "v2h_pct": round(float(g["v2h"].mean() * 100), 3),
            "hchg_pct": round(float(g["hchg"].mean() * 100), 3),
            "cchg_pct": round(float(g["cchg"].mean() * 100), 3),
            "home_soc_start": round(float(g["home_soc"].iloc[0]), 3),
            "home_soc_end": round(float(g["home_soc"].iloc[-1]), 3),
        }

    diff_counts = diffs.groupby("kind").size().astype(int).to_dict() if not diffs.empty else {}
    return {
        "cleaned_rows": int(len(cleaned)),
        "training_rows_1min_real_time": int(len(training_rows)),
        **seq_info,
        "removed_input_columns": ["s3_hr", "s3_spo2"],
        "cleaning_changes": clean_changes,
        "training_rule_fixes": fixes,
        "logic_difference_counts_before_fix": diff_counts,
        "scene_summary": scene_summary,
        "notes": [
            "No synthetic cross-scene transition rows are generated.",
            "Real-time features map compressed experimental time to scene time windows.",
            "Future targets use a short future-window average plus PV/SOC slew limiting to avoid training on relay/probe or voltage-step spikes.",
            "Stable night physics augmentation covers measured 0.9-1.7W home-battery load without teaching SOC cliff drops.",
            "logic_difference_report.csv records observed differences before training-state fixes.",
        ],
    }


def main() -> None:
    raw = load_raw_scenes()
    timed = add_real_time(raw)
    raw_diffs = find_logic_differences(timed)
    cleaned, clean_changes = apply_physical_cleaning(timed)
    cleaned_diffs = find_logic_differences(cleaned)
    training_base, fixes = enforce_rule_consistency(cleaned)
    training_resampled = resample_to_real_minutes(training_base)
    training_resampled = augment_stable_night_load(training_resampled)
    training_rows = add_training_targets(training_resampled)
    seq_info = build_sequence_npz(training_rows)

    cleaned.to_csv(OUT_DIR / "energy_cleaned_all.csv", index=False, encoding="utf-8-sig", float_format="%.6f")
    export_cols = INPUT_FEATURES + OUTPUT_TARGETS + ["scene_id", "scene_label", "real_hour", "real_elapsed_s", "sample_period_s"]
    training_rows[export_cols].to_csv(
        OUT_DIR / "energy_training_realistic.csv", index=False, encoding="utf-8-sig", float_format="%.6f"
    )
    # Compatibility filename: overwrite the older unrealistic augmented file so it is not used by mistake.
    training_rows[export_cols].to_csv(
        OUT_DIR / "energy_training_augmented.csv", index=False, encoding="utf-8-sig", float_format="%.6f"
    )
    raw_diffs.to_csv(OUT_DIR / "logic_difference_report.csv", index=False, encoding="utf-8-sig")
    cleaned_diffs.to_csv(OUT_DIR / "logic_difference_after_cleaning.csv", index=False, encoding="utf-8-sig")

    summary = build_summary(cleaned, training_rows, raw_diffs, clean_changes, fixes, seq_info)
    (OUT_DIR / "cleaning_summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
