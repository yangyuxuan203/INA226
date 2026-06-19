# LSTM 数据与模型优化说明

## 核心结论

- 当前训练数据已经移除心率、血氧等弱相关输入，仅保留 19 个能源调度相关输入。
- 家电池 SOC 已按实测 6.5V-8.4V 有效输出区间重新计算。
- 未来 PV 标签使用短窗口均值和最大上升约束，避免弱光样本直接学到强光跳变。
- 未来 home_soc 标签加入物理斜率约束，避免 88.8% 在十几分钟内预测到 70.2% 这种非物理结果。
- 新模型已重新导出 float32、dynamic、int8 TFLite 和 ESP32 可用 C 数组。

## 关键文件

- `ml_energy/outputs/energy_training_realistic.csv`
- `ml_energy/outputs/energy_lstm_dataset_realistic.npz`
- `ml_energy/outputs/models/energy_lstm_int8.tflite`
- `ml_energy/outputs/models/energy_lstm_int8_model.h`
- `ml_energy/outputs/figures_paper/`
- `ml_energy/outputs/tables_paper/`
- `ml_energy/outputs/energy_lstm_optimization_tables.xlsx`