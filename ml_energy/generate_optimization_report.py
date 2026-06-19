from __future__ import annotations

import json
from pathlib import Path

import pandas as pd


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "outputs"
TABLE_DIR = OUT / "tables_paper"


def fmt(value: float, digits: int = 3) -> str:
    return f"{value:.{digits}f}"


def write_markdown_table(df: pd.DataFrame, path: Path) -> None:
    headers = [str(col) for col in df.columns]
    rows = df.astype(str).values.tolist()
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]
    for row in rows:
        escaped = [cell.replace("|", "\\|").replace("\n", "<br>") for cell in row]
        lines.append("| " + " | ".join(escaped) + " |")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    cleaning = json.loads((OUT / "cleaning_summary.json").read_text(encoding="utf-8"))
    scaler = json.loads((OUT / "energy_scaler_realistic.json").read_text(encoding="utf-8"))
    report = json.loads((OUT / "lstm_training_report.json").read_text(encoding="utf-8"))
    train = pd.read_csv(OUT / "energy_training_realistic.csv")

    pv_4w = train[(train["pv_p"] >= 3.5) & (train["pv_p"] <= 4.5)].copy()
    pv_24w = train[(train["pv_p"] >= 2.0) & (train["pv_p"] <= 2.8)].copy()
    soc_high = train[(train["home_soc"] >= 86.0) & (train["home_soc"] <= 91.0)].copy()

    pv_4w_max_future = float(pv_4w["future_pv_p"].max()) if len(pv_4w) else float("nan")
    pv_4w_max_rise = float((pv_4w["future_pv_p"] - pv_4w["pv_p"]).max()) if len(pv_4w) else float("nan")
    pv_24w_max_future = float(pv_24w["future_pv_p"].max()) if len(pv_24w) else float("nan")
    pv_24w_max_rise = float((pv_24w["future_pv_p"] - pv_24w["pv_p"]).max()) if len(pv_24w) else float("nan")
    soc_min_future = float(soc_high["future_home_soc"].min()) if len(soc_high) else float("nan")
    soc_max_drop = float((soc_high["home_soc"] - soc_high["future_home_soc"]).max()) if len(soc_high) else float("nan")

    data_rows = [
        {
            "优化项": "输入特征",
            "优化前": "包含 s3_hr、s3_spo2 等弱相关生命体征输入，容易增加噪声",
            "优化后": f"{cleaning['feature_count']} 个输入特征，移除 {', '.join(cleaning['removed_input_columns'])}",
            "优化作用": "降低输入维度和无关噪声，便于 ESP32-S3 端部署",
        },
        {
            "优化项": "输出目标",
            "优化前": "输出目标较多，容易分散小模型表达能力",
            "优化后": ", ".join(scaler["output_targets"]),
            "优化作用": "只预测调度最有用的 PV、负载、家电池 SOC 三个量",
        },
        {
            "优化项": "家电池 SOC 标定",
            "优化前": "按 6.0V-8.4V 插值，低压段会把不可用电量算进去",
            "优化后": "按 6.5V-8.4V 有效输出区间重新计算",
            "优化作用": "符合实测 6.5V 停止输出，避免 14% 附近突然消失",
        },
        {
            "优化项": "启动异常数据",
            "优化前": "保留启动瞬态，可能引入继电器/传感器未稳定数据",
            "优化后": f"删除前 30s 暖机数据，共 {cleaning['cleaning_changes'][0]['rows']} 行",
            "优化作用": "减少非稳态样本对训练的干扰",
        },
        {
            "优化项": "时间映射",
            "优化前": "10s 采样直接训练，压缩实验时间和真实场景不一致",
            "优化后": f"{scaler['resample_period_s']}s 真实时间重采样，{scaler['sequence_length']} 点窗口，预测 {scaler['forecast_horizon_min']}min",
            "优化作用": "让 40min 实验数据对应 6h 场景，同时模型输入符合 ESP32 实际发送节奏",
        },
        {
            "优化项": "PV 未来标签",
            "优化前": "弱光/切换阶段可能学到 4W 或 2.4W 突然跳到 8W",
            "优化后": f"未来 {scaler['future_target_avg_min']}min 均值 + 最大上升 {scaler['pv_target_max_rise_w']}W / {int(scaler['pv_target_max_rise_ratio']*100)}%",
            "优化作用": f"当前 PV≈4W 时未来最大约 {fmt(pv_4w_max_future, 2)}W；PV≈2.4W 时未来最大约 {fmt(pv_24w_max_future, 2)}W",
        },
        {
            "优化项": "SOC 未来标签",
            "优化前": "可能出现 88.8% 在 12min 后预测到 70.2% 的非物理跳变",
            "优化后": f"未来 SOC 最大下降 {scaler['home_soc_target_max_drop_pct']}%，最大上升 {scaler['home_soc_target_max_rise_pct']}%",
            "优化作用": f"当前 SOC 86%-91% 时未来最低约 {fmt(soc_min_future, 2)}%，最大下降 {fmt(soc_max_drop, 2)}%",
        },
        {
            "优化项": "场景一致性",
            "优化前": "夜间、弱光、强光、满电车充、傍晚、V2H 场景边界未统一进训练描述",
            "优化后": f"保留 {len(cleaning['scene_summary'])} 个场景，不生成跨场景拼接数据",
            "优化作用": "减少不真实的跨场景跳变，让训练数据更贴近演示流程",
        },
    ]
    data_df = pd.DataFrame(data_rows)

    int8 = report["tflite"]["int8"]
    float32 = report["tflite"]["float32"]
    dynamic = report["tflite"]["dynamic"]
    metrics_rows = []
    for name, data in [
        ("float32", float32),
        ("dynamic", dynamic),
        ("int8", int8),
    ]:
        metrics_rows.append(
            {
                "模型": name,
                "PV功率MAE(W)": round(data["future_pv_p"]["mae"], 4),
                "负载功率MAE(W)": round(data["future_load_p"]["mae"], 4),
                "家电池SOC_MAE(%)": round(data["future_home_soc"]["mae"], 4),
                "整体归一化MAE": round(data["overall_mae_scaled"], 5),
                "模型大小(bytes)": data["size_bytes"],
            }
        )
    metrics_df = pd.DataFrame(metrics_rows)

    validation_rows = [
        {
            "检查项": "PV≈4W 样本数",
            "当前结果": len(pv_4w),
            "说明": f"future_pv_p 最大 {fmt(pv_4w_max_future, 2)}W，最大上升 {fmt(pv_4w_max_rise, 2)}W",
        },
        {
            "检查项": "PV≈2.4W 样本数",
            "当前结果": len(pv_24w),
            "说明": f"future_pv_p 最大 {fmt(pv_24w_max_future, 2)}W，最大上升 {fmt(pv_24w_max_rise, 2)}W",
        },
        {
            "检查项": "SOC 86%-91% 样本数",
            "当前结果": len(soc_high),
            "说明": f"future_home_soc 最低 {fmt(soc_min_future, 2)}%，最大下降 {fmt(soc_max_drop, 2)}%",
        },
        {
            "检查项": "LSTM 输入形状",
            "当前结果": f"{report['dataset']['input_shape'][0]} x {report['dataset']['input_shape'][1]}",
            "说明": "12 个 1min 时间点，每个时间点 19 个输入特征",
        },
        {
            "检查项": "输出维度",
            "当前结果": report["dataset"]["output_dim"],
            "说明": "future_pv_p、future_load_p、future_home_soc",
        },
    ]
    validation_df = pd.DataFrame(validation_rows)

    for name, df in [
        ("optimization_comparison_table", data_df),
        ("model_metrics_comparison_table", metrics_df),
        ("data_validation_table", validation_df),
    ]:
        df.to_csv(OUT / f"{name}.csv", index=False, encoding="utf-8-sig")
        write_markdown_table(df, OUT / f"{name}.md")

    xlsx_path = OUT / "energy_lstm_optimization_tables.xlsx"
    with pd.ExcelWriter(xlsx_path, engine="openpyxl") as writer:
        data_df.to_excel(writer, sheet_name="优化前后对比", index=False)
        metrics_df.to_excel(writer, sheet_name="模型指标", index=False)
        validation_df.to_excel(writer, sheet_name="数据边界检查", index=False)
        pd.DataFrame(cleaning["scene_summary"]).T.reset_index(names="scene").to_excel(
            writer, sheet_name="场景统计", index=False
        )

    summary = [
        "# LSTM 数据与模型优化说明",
        "",
        "## 核心结论",
        "",
        "- 当前训练数据已经移除心率、血氧等弱相关输入，仅保留 19 个能源调度相关输入。",
        "- 家电池 SOC 已按实测 6.5V-8.4V 有效输出区间重新计算。",
        "- 未来 PV 标签使用短窗口均值和最大上升约束，避免弱光样本直接学到强光跳变。",
        "- 未来 home_soc 标签加入物理斜率约束，避免 88.8% 在十几分钟内预测到 70.2% 这种非物理结果。",
        "- 新模型已重新导出 float32、dynamic、int8 TFLite 和 ESP32 可用 C 数组。",
        "",
        "## 关键文件",
        "",
        "- `ml_energy/outputs/energy_training_realistic.csv`",
        "- `ml_energy/outputs/energy_lstm_dataset_realistic.npz`",
        "- `ml_energy/outputs/models/energy_lstm_int8.tflite`",
        "- `ml_energy/outputs/models/energy_lstm_int8_model.h`",
        "- `ml_energy/outputs/figures_paper/`",
        "- `ml_energy/outputs/tables_paper/`",
        "- `ml_energy/outputs/energy_lstm_optimization_tables.xlsx`",
    ]
    (OUT / "energy_lstm_optimization_summary.md").write_text("\n".join(summary), encoding="utf-8")

    print(json.dumps(
        {
            "comparison_csv": str(OUT / "optimization_comparison_table.csv"),
            "comparison_md": str(OUT / "optimization_comparison_table.md"),
            "metrics_csv": str(OUT / "model_metrics_comparison_table.csv"),
            "validation_csv": str(OUT / "data_validation_table.csv"),
            "xlsx": str(xlsx_path),
            "summary": str(OUT / "energy_lstm_optimization_summary.md"),
        },
        ensure_ascii=False,
        indent=2,
    ))


if __name__ == "__main__":
    main()
