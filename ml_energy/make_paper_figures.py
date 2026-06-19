#!/usr/bin/env python3
from __future__ import annotations

import json
import os
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import tensorflow as tf


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "ml_energy" / "outputs"
FIG_DIR = OUT_DIR / "figures_paper"
TABLE_DIR = OUT_DIR / "tables_paper"
MODEL_DIR = OUT_DIR / "models"
FIG_DIR.mkdir(parents=True, exist_ok=True)
TABLE_DIR.mkdir(parents=True, exist_ok=True)

TRAINING_CSV = OUT_DIR / "energy_training_realistic.csv"
DATASET_PATH = OUT_DIR / "energy_lstm_dataset_realistic.npz"
META_PATH = OUT_DIR / "energy_lstm_sequence_meta.csv"
SCALER_PATH = OUT_DIR / "energy_scaler_realistic.json"
REPORT_PATH = OUT_DIR / "lstm_training_report.json"
CLEANING_SUMMARY_PATH = OUT_DIR / "cleaning_summary.json"

TARGET_LABELS = {
    "future_pv_p": "Future PV power (W)",
    "future_load_p": "Future load power (W)",
    "future_home_soc": "Future home SOC (%)",
}

FEATURE_UNITS = {
    "real_hour_sin": "-",
    "real_hour_cos": "-",
    "lux": "lux",
    "pv_v": "V",
    "pv_p": "W",
    "home_v": "V",
    "home_soc": "%",
    "load_p": "W",
    "car_soc": "%",
    "human_soc": "%",
    "pvsrc": "0/1",
    "hsrc": "0/1",
    "rigid": "0/1",
    "led": "0/1",
    "fan": "0/1",
    "qi": "0/1",
    "hchg": "0/1",
    "cchg": "0/1",
    "v2h": "0/1",
}

FEATURE_NOTES = {
    "real_hour_sin": "Daily time encoded by sine to preserve periodicity.",
    "real_hour_cos": "Daily time encoded by cosine to preserve periodicity.",
    "lux": "Ambient illumination measured by the light sensor.",
    "pv_v": "PV bus voltage measured by INA226.",
    "pv_p": "PV output power measured by INA226.",
    "home_v": "Home battery voltage measured by INA226.",
    "home_soc": "Home battery SOC recalculated from 6.5-8.4 V table.",
    "load_p": "Home load power measured by INA226.",
    "car_soc": "Vehicle battery SOC received through CAN.",
    "human_soc": "Wearable/human-node battery SOC received through UDP.",
    "pvsrc": "PV source MOS state.",
    "hsrc": "Home battery source MOS state.",
    "rigid": "Rigid load switch state.",
    "led": "LED load switch state.",
    "fan": "Fan load switch state.",
    "qi": "Qi wireless charging switch state.",
    "hchg": "PV-to-home-battery charging MOS state.",
    "cchg": "PV-to-car-battery charging MOS state.",
    "v2h": "Vehicle-to-home request/active state.",
}


def set_style() -> None:
    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "font.size": 10,
        "axes.titlesize": 11,
        "axes.labelsize": 10,
        "legend.fontsize": 8,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "axes.grid": True,
        "grid.alpha": 0.25,
        "figure.dpi": 140,
        "savefig.dpi": 320,
        "savefig.bbox": "tight",
    })


def savefig(fig: plt.Figure, name: str) -> None:
    for ext in ["png", "pdf", "svg"]:
        fig.savefig(FIG_DIR / f"{name}.{ext}")
    plt.close(fig)


def load_all():
    df = pd.read_csv(TRAINING_CSV)
    data = np.load(DATASET_PATH)
    meta = pd.read_csv(META_PATH)
    scaler = json.loads(SCALER_PATH.read_text(encoding="utf-8"))
    report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    cleaning = json.loads(CLEANING_SUMMARY_PATH.read_text(encoding="utf-8"))
    return df, data, meta, scaler, report, cleaning


def split_indices(meta: pd.DataFrame):
    train, val, test = [], [], []
    for _, g in meta.groupby("scene_id", sort=True):
        idx = g.index.to_numpy()
        n = len(idx)
        n_train = max(1, int(n * 0.70))
        n_val = max(1, int(n * 0.15))
        train.extend(idx[:n_train])
        val.extend(idx[n_train:n_train + n_val])
        test.extend(idx[n_train + n_val:])
    return np.array(train), np.array(val), np.array(test)


def inverse_y(y_scaled: np.ndarray, scaler: dict) -> np.ndarray:
    y_min = np.asarray(scaler["y_min"], dtype=np.float32)
    y_range = np.asarray(scaler["y_range"], dtype=np.float32)
    return y_scaled * y_range + y_min


def run_tflite(tflite_path: Path, X: np.ndarray) -> np.ndarray:
    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    preds = []
    for sample in X:
        x = sample[np.newaxis, ...].astype(np.float32)
        if input_details["dtype"] == np.int8:
            scale, zp = input_details["quantization"]
            x = np.round(x / scale + zp).clip(-128, 127).astype(np.int8)
        interpreter.set_tensor(input_details["index"], x)
        interpreter.invoke()
        out = interpreter.get_tensor(output_details["index"])
        if output_details["dtype"] == np.int8:
            scale, zp = output_details["quantization"]
            out = (out.astype(np.float32) - zp) * scale
        preds.append(out[0])
    return np.asarray(preds, dtype=np.float32)


def plot_scenario_overview(df: pd.DataFrame) -> None:
    scene_order = list(df["scene_label"].drop_duplicates())
    colors = plt.get_cmap("tab10").colors
    fig, axes = plt.subplots(4, 1, figsize=(11, 8.2), sharex=True)
    signals = [
        ("lux", "Illuminance (lux)"),
        ("pv_p", "PV power (W)"),
        ("load_p", "Load power (W)"),
        ("home_soc", "Home SOC (%)"),
    ]
    for i, (col, label) in enumerate(signals):
        ax = axes[i]
        offset = 0.0
        for j, scene in enumerate(scene_order):
            g = df[df["scene_label"] == scene].copy()
            x = offset + g["real_elapsed_s"].to_numpy() / 3600.0
            ax.plot(x, g[col].to_numpy(), color=colors[j % len(colors)], linewidth=1.25, label=scene)
            if i == 0:
                ax.axvspan(x.min(), x.max(), color=colors[j % len(colors)], alpha=0.08)
                ax.text((x.min() + x.max()) / 2, ax.get_ylim()[1] * 0.92, f"T{j + 1}",
                        ha="center", va="top", fontsize=8, color=colors[j % len(colors)])
            offset = x.max() + 0.15
        ax.set_ylabel(label)
    axes[0].legend(loc="upper left", ncol=3, frameon=False)
    axes[-1].set_xlabel("Concatenated mapped scene time (h)")
    fig.suptitle("Energy-dispatch dataset under six representative scenarios")
    fig.tight_layout(rect=[0, 0, 1, 0.97])
    savefig(fig, "01_scenario_overview")


def plot_feature_target_heatmap(df: pd.DataFrame, scaler: dict) -> None:
    features = scaler["input_features"]
    targets = scaler["output_targets"]
    corr = df[features + targets].corr(numeric_only=True).loc[features, targets]
    fig, ax = plt.subplots(figsize=(7.4, 7.8))
    im = ax.imshow(corr.to_numpy(), cmap="RdBu_r", vmin=-1, vmax=1, aspect="auto")
    ax.set_xticks(np.arange(len(targets)))
    ax.set_xticklabels([TARGET_LABELS.get(t, t) for t in targets], rotation=25, ha="right")
    ax.set_yticks(np.arange(len(features)))
    ax.set_yticklabels(features)
    for r in range(corr.shape[0]):
        for c in range(corr.shape[1]):
            val = corr.iloc[r, c]
            ax.text(c, r, f"{val:.2f}", ha="center", va="center", fontsize=6,
                    color="white" if abs(val) > 0.55 else "black")
    cbar = fig.colorbar(im, ax=ax, fraction=0.035, pad=0.02)
    cbar.set_label("Pearson correlation")
    ax.set_title("Input-feature correlation with prediction targets")
    fig.tight_layout()
    savefig(fig, "02_feature_target_correlation")


def plot_quantization_bars(report: dict) -> None:
    targets = ["future_pv_p", "future_load_p", "future_home_soc"]
    variants = ["float32", "dynamic", "int8"]
    variant_labels = ["Float32", "Dynamic", "INT8"]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.4), gridspec_kw={"width_ratios": [2.2, 1]})
    ax = axes[0]
    x = np.arange(len(targets))
    width = 0.24
    for i, variant in enumerate(variants):
        vals = [report["tflite"][variant][t]["mae"] for t in targets]
        ax.bar(x + (i - 1) * width, vals, width, label=variant_labels[i])
    ax.set_xticks(x)
    ax.set_xticklabels(["PV power\n(W)", "Load power\n(W)", "Home SOC\n(%)"])
    ax.set_ylabel("MAE in physical unit")
    ax.set_title("Prediction error after TFLite conversion")
    ax.legend(frameon=False)

    ax = axes[1]
    sizes = [report["tflite"][v]["size_bytes"] / 1024.0 for v in variants]
    bars = ax.bar(variant_labels, sizes, color=["#4C78A8", "#59A14F", "#E15759"])
    ax.set_ylabel("Model size (KB)")
    ax.set_title("Deployment footprint")
    for bar, size in zip(bars, sizes):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(), f"{size:.1f}",
                ha="center", va="bottom", fontsize=8)
    fig.tight_layout()
    savefig(fig, "03_quantization_error_and_size")


def plot_prediction_parity(data, meta: pd.DataFrame, scaler: dict) -> None:
    X = data["X"].astype(np.float32)
    y_raw = data["y_raw"].astype(np.float32)
    _, _, test_idx = split_indices(meta)
    X_test = X[test_idx]
    y_true = y_raw[test_idx]
    pred_scaled = run_tflite(MODEL_DIR / "energy_lstm_int8.tflite", X_test)
    y_pred = inverse_y(pred_scaled, scaler)
    targets = scaler["output_targets"]

    fig, axes = plt.subplots(1, len(targets), figsize=(11.2, 3.8))
    for ax, target in zip(axes, targets):
        i = targets.index(target)
        yt = y_true[:, i]
        yp = y_pred[:, i]
        err = yp - yt
        mae = np.mean(np.abs(err))
        rmse = np.sqrt(np.mean(err * err))
        ss_res = np.sum(err * err)
        ss_tot = np.sum((yt - yt.mean()) ** 2)
        r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else np.nan
        ax.scatter(yt, yp, s=13, alpha=0.68, edgecolors="none")
        lo = min(yt.min(), yp.min())
        hi = max(yt.max(), yp.max())
        pad = (hi - lo) * 0.05 if hi > lo else 1.0
        ax.plot([lo - pad, hi + pad], [lo - pad, hi + pad], "k--", linewidth=1)
        ax.set_xlim(lo - pad, hi + pad)
        ax.set_ylim(lo - pad, hi + pad)
        ax.set_title(TARGET_LABELS.get(target, target))
        ax.set_xlabel("Measured target")
        ax.set_ylabel("INT8 predicted")
        ax.text(0.04, 0.96, f"MAE={mae:.3f}\nRMSE={rmse:.3f}\nR2={r2:.3f}",
                transform=ax.transAxes, ha="left", va="top",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", edgecolor="#CCCCCC", alpha=0.9),
                fontsize=8)
    fig.suptitle("INT8 LSTM prediction parity on the held-out test set")
    fig.tight_layout(rect=[0, 0, 1, 0.92])
    savefig(fig, "04_int8_prediction_parity")

    pred_df = pd.DataFrame()
    pred_df["test_sequence_index"] = np.arange(len(test_idx))
    pred_df["scene_id"] = meta.loc[test_idx, "scene_id"].to_numpy()
    pred_df["scene_label"] = meta.loc[test_idx, "scene_label"].to_numpy()
    for i, target in enumerate(targets):
        pred_df[f"true_{target}"] = y_true[:, i]
        pred_df[f"pred_int8_{target}"] = y_pred[:, i]
        pred_df[f"err_int8_{target}"] = y_pred[:, i] - y_true[:, i]
    pred_df.to_csv(TABLE_DIR / "prediction_samples_int8.csv", index=False, encoding="utf-8-sig")

    plot_prediction_trajectory(pred_df, scaler)
    plot_error_distribution(pred_df, scaler)


def plot_prediction_trajectory(pred_df: pd.DataFrame, scaler: dict) -> None:
    targets = scaler["output_targets"]
    fig, axes = plt.subplots(len(targets), 1, figsize=(11, 7.2), sharex=True)
    x = pred_df["test_sequence_index"].to_numpy()
    scene_ids = pred_df["scene_id"].to_numpy()
    colors = plt.get_cmap("tab10").colors

    for ax, target in zip(axes, targets):
        ax.plot(x, pred_df[f"true_{target}"], color="#1F77B4", linewidth=1.7, label="Measured target")
        ax.plot(x, pred_df[f"pred_int8_{target}"], color="#E15759", linewidth=1.2,
                linestyle="--", label="INT8 prediction")
        start = 0
        while start < len(x):
            end = start + 1
            while end < len(x) and scene_ids[end] == scene_ids[start]:
                end += 1
            ax.axvspan(x[start], x[end - 1], color=colors[int(scene_ids[start]) % len(colors)], alpha=0.07)
            ax.text((x[start] + x[end - 1]) / 2, ax.get_ylim()[1], f"T{int(scene_ids[start])}",
                    ha="center", va="top", fontsize=7, color="#555555")
            start = end
        ax.set_ylabel(TARGET_LABELS.get(target, target))
    axes[0].legend(frameon=False, ncol=2)
    axes[-1].set_xlabel("Held-out test sequence index")
    fig.suptitle("INT8 LSTM prediction trajectories across test scenarios")
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    savefig(fig, "07_int8_prediction_trajectory")


def plot_error_distribution(pred_df: pd.DataFrame, scaler: dict) -> None:
    targets = scaler["output_targets"]
    fig, axes = plt.subplots(1, len(targets), figsize=(11, 3.8))
    for ax, target in zip(axes, targets):
        err = pred_df[f"err_int8_{target}"].to_numpy()
        ax.boxplot(err, vert=True, widths=0.45, showmeans=True,
                   meanprops={"marker": "o", "markerfacecolor": "#E15759", "markeredgecolor": "#E15759"},
                   medianprops={"color": "#1F77B4", "linewidth": 1.5})
        ax.axhline(0, color="black", linestyle="--", linewidth=1)
        ax.set_xticks([1])
        ax.set_xticklabels(["INT8"])
        ax.set_title(TARGET_LABELS.get(target, target))
        ax.set_ylabel("Prediction error")
        q1, med, q3 = np.percentile(err, [25, 50, 75])
        ax.text(0.04, 0.96, f"Q1={q1:.3f}\nMedian={med:.3f}\nQ3={q3:.3f}",
                transform=ax.transAxes, ha="left", va="top",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="white", edgecolor="#CCCCCC", alpha=0.9),
                fontsize=8)
    fig.suptitle("INT8 prediction error distribution on the held-out test set")
    fig.tight_layout(rect=[0, 0, 1, 0.92])
    savefig(fig, "08_int8_error_distribution")


def plot_pipeline_diagram(scaler: dict, report: dict) -> None:
    fig, ax = plt.subplots(figsize=(11, 3.8))
    ax.axis("off")
    boxes = [
        ("STM32F407\nsensors + MOS states\n19 raw features", 0.05, 0.55, "#D9EAF7"),
        ("ESP32-S3\n1 min resampling\n12-step window", 0.28, 0.55, "#E7F4DD"),
        ("Normalization\nx'=(x-x_min)/x_range", 0.50, 0.55, "#FFF2CC"),
        ("INT8 LSTM\nTFLite Micro\n53.3 KB", 0.70, 0.55, "#FCE4D6"),
        ("STM32F407\n3 predicted values\nrule-based safety gate", 0.88, 0.55, "#EADCF8"),
    ]
    for text, x, y, color in boxes:
        ax.text(x, y, text, ha="center", va="center", fontsize=10,
                bbox=dict(boxstyle="round,pad=0.55", facecolor=color, edgecolor="#555555", linewidth=1.0))
    for x0, x1 in [(0.15, 0.22), (0.38, 0.44), (0.58, 0.64), (0.78, 0.83)]:
        ax.annotate("", xy=(x1, 0.55), xytext=(x0, 0.55),
                    arrowprops=dict(arrowstyle="->", linewidth=1.6, color="#333333"))
    targets = ", ".join(scaler["output_targets"])
    ax.text(0.5, 0.20,
            f"Input shape: {report['dataset']['input_shape'][0]} x {report['dataset']['input_shape'][1]} | "
            f"Forecast horizon: 10 min | Outputs: {targets}",
            ha="center", va="center", fontsize=10)
    ax.set_title("Edge-AI energy forecasting and safe dispatch integration")
    fig.tight_layout()
    savefig(fig, "05_edge_ai_pipeline")


def plot_window_diagram() -> None:
    fig, ax = plt.subplots(figsize=(10.5, 3.5))
    ax.axis("off")
    for i in range(12):
        x = 0.08 + i * 0.058
        ax.add_patch(plt.Rectangle((x, 0.52), 0.045, 0.18, facecolor="#D9EAF7", edgecolor="#4C78A8"))
        if i in [0, 5, 11]:
            ax.text(x + 0.0225, 0.76, f"t-{11-i}", ha="center", fontsize=8)
    ax.text(0.405, 0.38, "12 historical points, 1 min interval", ha="center", fontsize=10)
    ax.annotate("", xy=(0.83, 0.61), xytext=(0.78, 0.61),
                arrowprops=dict(arrowstyle="->", linewidth=1.6))
    ax.text(0.89, 0.61, "Forecast at t+10 min\nPV power, load power,\nhome SOC",
            ha="center", va="center", fontsize=10,
            bbox=dict(boxstyle="round,pad=0.45", facecolor="#E7F4DD", edgecolor="#59A14F"))
    ax.text(0.08, 0.18,
            "STM32 sends raw data every 10 s; ESP32 forms one 1-min representative sample, then updates the rolling LSTM window.",
            ha="left", fontsize=9)
    ax.set_title("LSTM sequence construction used for deployment")
    fig.tight_layout()
    savefig(fig, "06_lstm_window_definition")


def make_tables(df: pd.DataFrame, meta: pd.DataFrame, scaler: dict, report: dict, cleaning: dict) -> None:
    scene_rows = []
    for scene, info in cleaning["scene_summary"].items():
        scene_rows.append({
            "scene_label": scene,
            "cleaned_rows": info["rows_cleaned"],
            "real_start_hour": info["real_start_hour"],
            "real_duration_h": info["real_duration_h"],
            "avg_lux": info["avg_lux"],
            "pvsrc_pct": info["pvsrc_pct"],
            "hsrc_pct": info["hsrc_pct"],
            "hchg_pct": info["hchg_pct"],
            "cchg_pct": info["cchg_pct"],
            "v2h_pct": info["v2h_pct"],
            "home_soc_start": info["home_soc_start"],
            "home_soc_end": info["home_soc_end"],
        })
    pd.DataFrame(scene_rows).to_csv(TABLE_DIR / "table_scene_summary.csv", index=False, encoding="utf-8-sig")

    train_idx, val_idx, test_idx = split_indices(meta)
    split_rows = []
    for name, idx in [("train", train_idx), ("validation", val_idx), ("test", test_idx)]:
        m = meta.loc[idx]
        split_rows.append({
            "split": name,
            "sequences": len(idx),
            "scene_count": m["scene_id"].nunique(),
            "min_scene_id": int(m["scene_id"].min()),
            "max_scene_id": int(m["scene_id"].max()),
        })
    pd.DataFrame(split_rows).to_csv(TABLE_DIR / "table_dataset_split.csv", index=False, encoding="utf-8-sig")

    feature_rows = []
    for i, feature in enumerate(scaler["input_features"]):
        feature_rows.append({
            "index": i,
            "feature": feature,
            "unit": FEATURE_UNITS.get(feature, ""),
            "x_min": scaler["x_min"][i],
            "x_range": scaler["x_range"][i],
            "deployment_source": "STM32F407 UDP payload",
            "note": FEATURE_NOTES.get(feature, ""),
        })
    pd.DataFrame(feature_rows).to_csv(TABLE_DIR / "table_input_features_and_scaler.csv", index=False, encoding="utf-8-sig")

    metric_rows = []
    for family, models in report.items():
        if family not in ["keras", "tflite"]:
            continue
        for model_name, metrics in models.items():
            row = {"family": family, "model": model_name}
            for target in scaler["output_targets"]:
                row[f"{target}_mae"] = metrics[target]["mae"]
                row[f"{target}_rmse"] = metrics[target]["rmse"]
            row["overall_mae_scaled"] = metrics["overall_mae_scaled"]
            if "size_bytes" in metrics:
                row["size_bytes"] = metrics["size_bytes"]
                row["size_kb"] = metrics["size_bytes"] / 1024.0
            metric_rows.append(row)
    pd.DataFrame(metric_rows).to_csv(TABLE_DIR / "table_model_metrics.csv", index=False, encoding="utf-8-sig")

    dataset_summary = pd.DataFrame([{
        "cleaned_rows": cleaning["cleaned_rows"],
        "training_rows_1min_real_time": cleaning["training_rows_1min_real_time"],
        "lstm_sequences": cleaning["sequences"],
        "input_features": cleaning["feature_count"],
        "output_targets": cleaning["target_count"],
        "sequence_length": scaler["sequence_length"],
        "resample_period_s": scaler["resample_period_s"],
        "forecast_horizon_min": scaler["forecast_horizon_min"],
        "test_sequences": report["dataset"]["test"],
        "int8_model_size_bytes": report["tflite"]["int8"]["size_bytes"],
    }])
    dataset_summary.to_csv(TABLE_DIR / "table_dataset_model_summary.csv", index=False, encoding="utf-8-sig")

    figure_index = [
        ("01_scenario_overview", "Six-scenario data overview", "Use in dataset/experimental design section."),
        ("02_feature_target_correlation", "Input-output correlation heatmap", "Use to justify feature selection."),
        ("03_quantization_error_and_size", "TFLite error and model size", "Use to compare float/dynamic/INT8 deployment."),
        ("04_int8_prediction_parity", "INT8 predicted-vs-measured scatter", "Use to show prediction agreement."),
        ("05_edge_ai_pipeline", "STM32-ESP32 edge-AI pipeline", "Use in system architecture section."),
        ("06_lstm_window_definition", "12-step sequence construction", "Use to explain deployment timing."),
        ("07_int8_prediction_trajectory", "INT8 test-set trajectory", "Use to show time-series tracking effect."),
        ("08_int8_error_distribution", "INT8 error distribution", "Use to discuss residual error and robustness."),
    ]
    pd.DataFrame(figure_index, columns=["figure_name", "title", "recommended_use"]).to_csv(
        TABLE_DIR / "table_figure_index.csv", index=False, encoding="utf-8-sig")


def main() -> None:
    set_style()
    df, data, meta, scaler, report, cleaning = load_all()
    plot_scenario_overview(df)
    plot_feature_target_heatmap(df, scaler)
    plot_quantization_bars(report)
    plot_prediction_parity(data, meta, scaler)
    plot_pipeline_diagram(scaler, report)
    plot_window_diagram()
    make_tables(df, meta, scaler, report, cleaning)
    print(f"figures: {FIG_DIR}")
    print(f"tables: {TABLE_DIR}")


if __name__ == "__main__":
    main()
