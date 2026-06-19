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
import tensorflow_model_optimization as tfmot


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "ml_energy" / "outputs"
MODEL_DIR = OUT_DIR / "models"
FIG_DIR = OUT_DIR / "figures"
MODEL_DIR.mkdir(parents=True, exist_ok=True)
FIG_DIR.mkdir(parents=True, exist_ok=True)

DATASET_PATH = OUT_DIR / "energy_lstm_dataset_realistic.npz"
SCALER_PATH = OUT_DIR / "energy_scaler_realistic.json"
TRAINING_CSV = OUT_DIR / "energy_training_realistic.csv"

SEED = 42
np.random.seed(SEED)
tf.random.set_seed(SEED)


def load_data():
    data = np.load(DATASET_PATH)
    X = data["X"].astype(np.float32)
    y = data["y"].astype(np.float32)
    X_raw = data["X_raw"].astype(np.float32)
    y_raw = data["y_raw"].astype(np.float32)
    scaler = json.loads(SCALER_PATH.read_text(encoding="utf-8"))
    meta = pd.read_csv(OUT_DIR / "energy_lstm_sequence_meta.csv")
    return X, y, X_raw, y_raw, scaler, meta


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


def build_lstm(units: int, input_shape: tuple[int, int], output_dim: int, name: str) -> tf.keras.Model:
    inp = tf.keras.Input(shape=input_shape, name="energy_sequence")
    x = tf.keras.layers.LSTM(units, unroll=True, name="lstm")(inp)
    x = tf.keras.layers.Dense(max(8, units), activation="relu", name="dense_relu")(x)
    out = tf.keras.layers.Dense(output_dim, activation="sigmoid", name="scaled_outputs")(x)
    model = tf.keras.Model(inp, out, name=name)
    model.compile(optimizer=tf.keras.optimizers.Adam(0.002), loss="mse", metrics=["mae"])
    return model


def train_model(model, X_train, y_train, X_val, y_val, epochs: int, batch_size: int = 16):
    callbacks = [
        tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=20, restore_best_weights=True),
        tf.keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.5, patience=8, min_lr=1e-5),
    ]
    return model.fit(
        X_train,
        y_train,
        validation_data=(X_val, y_val),
        epochs=epochs,
        batch_size=batch_size,
        verbose=0,
        callbacks=callbacks,
    )


def inverse_y(y_scaled: np.ndarray, scaler: dict) -> np.ndarray:
    y_min = np.asarray(scaler["y_min"], dtype=np.float32)
    y_range = np.asarray(scaler["y_range"], dtype=np.float32)
    return y_scaled * y_range + y_min


def regression_metrics(y_true_raw: np.ndarray, y_pred_scaled: np.ndarray, scaler: dict) -> dict:
    target_names = scaler["output_targets"]
    y_pred_raw = inverse_y(y_pred_scaled, scaler)
    metrics = {}
    for i, name in enumerate(target_names):
        err = y_pred_raw[:, i] - y_true_raw[:, i]
        metrics[name] = {
            "mae": float(np.mean(np.abs(err))),
            "rmse": float(np.sqrt(np.mean(err * err))),
        }
    y_true_scaled = (y_true_raw - np.asarray(scaler["y_min"])) / np.asarray(scaler["y_range"])
    metrics["overall_mae_scaled"] = float(np.mean(np.abs(y_pred_scaled - y_true_scaled)))
    return metrics


def representative_dataset(X_ref: np.ndarray):
    def gen():
        for i in range(min(200, len(X_ref))):
            yield [X_ref[i:i + 1].astype(np.float32)]
    return gen


def save_tflite(model: tf.keras.Model, path: Path, mode: str, X_ref: np.ndarray | None = None) -> bytes:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    if mode == "float32":
        pass
    elif mode == "dynamic":
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
    elif mode == "int8":
        if X_ref is None:
            raise ValueError("int8 conversion requires representative data")
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        converter.representative_dataset = representative_dataset(X_ref)
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        converter.inference_input_type = tf.int8
        converter.inference_output_type = tf.int8
    else:
        raise ValueError(mode)
    blob = converter.convert()
    path.write_bytes(blob)
    return blob


def run_tflite(tflite_path: Path, X: np.ndarray) -> np.ndarray:
    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    y = []
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
        y.append(out[0])
    return np.asarray(y, dtype=np.float32)


def export_c_array(tflite_path: Path, header_path: Path, array_name: str = "g_energy_lstm_int8_tflite") -> None:
    data = tflite_path.read_bytes()
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"const unsigned int {array_name}_len = {len(data)};",
        f"const unsigned char {array_name}[] = {{",
    ]
    for i in range(0, len(data), 12):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        lines.append(f"  {chunk},")
    lines.append("};")
    header_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def plot_training_rows():
    df = pd.read_csv(TRAINING_CSV)
    fig, axes = plt.subplots(4, 1, figsize=(11, 10), sharex=False)
    for scene, g in df.groupby("scene_label", sort=False):
        x = g["real_hour"].to_numpy()
        axes[0].plot(x, g["lux"], label=scene, linewidth=1)
        axes[1].plot(x, g["pv_p"], label=scene, linewidth=1)
        axes[2].plot(x, g["load_p"], label=scene, linewidth=1)
        axes[3].plot(x, g["home_soc"], label=scene, linewidth=1)
    axes[0].set_ylabel("lux")
    axes[1].set_ylabel("pv_p W")
    axes[2].set_ylabel("load_p W")
    axes[3].set_ylabel("home_soc %")
    axes[3].set_xlabel("mapped real hour")
    axes[0].legend(fontsize=7, ncol=2)
    fig.suptitle("LSTM inputs and target-related signals")
    fig.tight_layout()
    fig.savefig(FIG_DIR / "input_output_timeseries.png", dpi=160)
    plt.close(fig)


def plot_predictions(y_true_raw, preds: dict[str, np.ndarray], scaler: dict):
    target_names = scaler["output_targets"]
    key_targets = ["future_pv_p", "future_load_p", "future_home_soc"]
    fig, axes = plt.subplots(len(key_targets), 1, figsize=(11, 10), sharex=True)
    x = np.arange(len(y_true_raw))
    for ax, target in zip(axes, key_targets):
        i = target_names.index(target)
        ax.plot(x, y_true_raw[:, i], label="true", linewidth=1.5)
        for name, pred_scaled in preds.items():
            ax.plot(x, inverse_y(pred_scaled, scaler)[:, i], label=name, linewidth=1)
        ax.set_ylabel(target)
        ax.grid(True, alpha=0.3)
    axes[0].legend(fontsize=8)
    axes[-1].set_xlabel("test sequence index")
    fig.suptitle("Float/QAT/INT8 prediction comparison")
    fig.tight_layout()
    fig.savefig(FIG_DIR / "prediction_quantization_comparison.png", dpi=160)
    plt.close(fig)


def try_prune_model(model, X_train, y_train, X_val, y_val):
    # Deployment-friendly magnitude pruning: no pruning wrappers are left in the model.
    pruned = tf.keras.models.clone_model(model)
    pruned.set_weights(model.get_weights())
    pruned.compile(optimizer=tf.keras.optimizers.Adam(0.001), loss="mse", metrics=["mae"])

    for layer in pruned.layers:
        if isinstance(layer, tf.keras.layers.Dense):
            weights = layer.get_weights()
            if not weights:
                continue
            kernel = weights[0]
            threshold = np.percentile(np.abs(kernel), 50.0)
            kernel = np.where(np.abs(kernel) < threshold, 0.0, kernel)
            weights[0] = kernel
            layer.set_weights(weights)

    pruned.fit(X_train, y_train, validation_data=(X_val, y_val), epochs=20, batch_size=16, verbose=0)
    return pruned


def try_qat_model(model, X_train, y_train, X_val, y_val):
    try:
        def annotate_dense(layer):
            if isinstance(layer, tf.keras.layers.Dense):
                return tfmot.quantization.keras.quantize_annotate_layer(layer)
            return layer

        annotated = tf.keras.models.clone_model(model, clone_function=annotate_dense)
        annotated.set_weights(model.get_weights())
        qat = tfmot.quantization.keras.quantize_apply(annotated)
        qat.compile(optimizer=tf.keras.optimizers.Adam(0.0005), loss="mse", metrics=["mae"])
        qat.fit(X_train, y_train, validation_data=(X_val, y_val), epochs=15, batch_size=16, verbose=0)
        return qat, None
    except Exception as exc:
        return model, str(exc)


def main():
    X, y, X_raw, y_raw, scaler, meta = load_data()
    train_idx, val_idx, test_idx = split_indices(meta)
    X_train, y_train = X[train_idx], y[train_idx]
    X_val, y_val = X[val_idx], y[val_idx]
    X_test, y_test = X[test_idx], y[test_idx]
    y_test_raw = y_raw[test_idx]

    plot_training_rows()

    input_shape = X.shape[1:]
    output_dim = y.shape[1]

    teacher = build_lstm(24, input_shape, output_dim, "teacher_lstm")
    train_model(teacher, X_train, y_train, X_val, y_val, epochs=160)
    teacher.save(MODEL_DIR / "teacher_lstm.keras")
    teacher_pred_train = teacher.predict(X_train, verbose=0)

    student = build_lstm(8, input_shape, output_dim, "student_lstm")
    y_distill = 0.55 * y_train + 0.45 * teacher_pred_train
    train_model(student, X_train, y_distill, X_val, y_val, epochs=180)
    student.save(MODEL_DIR / "student_lstm_distilled.keras")

    pruned = try_prune_model(student, X_train, y_train, X_val, y_val)
    pruned.save(MODEL_DIR / "student_lstm_pruned.keras")

    qat_model, qat_error = try_qat_model(pruned, X_train, y_train, X_val, y_val)
    qat_model.save(MODEL_DIR / "student_lstm_qat.keras")

    models = {
        "teacher": teacher,
        "student_distilled": student,
        "student_pruned": pruned,
        "qat_or_pruned": qat_model,
    }
    metrics = {
        "dataset": {
            "samples": int(len(X)),
            "train": int(len(train_idx)),
            "val": int(len(val_idx)),
            "test": int(len(test_idx)),
            "input_shape": list(input_shape),
            "output_dim": int(output_dim),
        },
        "qat_error": qat_error,
        "keras": {},
        "tflite": {},
    }

    pred_scaled = {}
    for name, model in models.items():
        pred = model.predict(X_test, verbose=0)
        metrics["keras"][name] = regression_metrics(y_test_raw, pred, scaler)
        pred_scaled[name] = pred

    deploy_model_name = min(
        metrics["keras"],
        key=lambda name: metrics["keras"][name]["overall_mae_scaled"],
    )
    base_model = models[deploy_model_name]
    metrics["deploy_model"] = deploy_model_name
    tflite_paths = {
        "float32": MODEL_DIR / "energy_lstm_float32.tflite",
        "dynamic": MODEL_DIR / "energy_lstm_dynamic_quant.tflite",
        "int8": MODEL_DIR / "energy_lstm_int8.tflite",
    }
    save_tflite(base_model, tflite_paths["float32"], "float32")
    save_tflite(base_model, tflite_paths["dynamic"], "dynamic")
    save_tflite(base_model, tflite_paths["int8"], "int8", X_train)

    for name, path in tflite_paths.items():
        pred = run_tflite(path, X_test)
        metrics["tflite"][name] = regression_metrics(y_test_raw, pred, scaler)
        metrics["tflite"][name]["size_bytes"] = path.stat().st_size
        pred_scaled[f"tflite_{name}"] = pred

    int8_header = MODEL_DIR / "energy_lstm_int8_model.h"
    export_c_array(tflite_paths["int8"], int8_header)
    metrics["c_array_header"] = str(int8_header)

    plot_predictions(
        y_test_raw,
        {
            "keras_float": pred_scaled["qat_or_pruned"],
            "tflite_float32": pred_scaled["tflite_float32"],
            "tflite_int8": pred_scaled["tflite_int8"],
        },
        scaler,
    )

    (OUT_DIR / "lstm_training_report.json").write_text(json.dumps(metrics, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(metrics, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
