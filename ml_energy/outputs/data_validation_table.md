| 检查项 | 当前结果 | 说明 |
| --- | --- | --- |
| PV≈4W 样本数 | 63 | future_pv_p 最大 6.51W，最大上升 2.02W |
| PV≈2.4W 样本数 | 1 | future_pv_p 最大 3.78W，最大上升 1.50W |
| SOC 86%-91% 样本数 | 348 | future_home_soc 最低 83.32%，最大下降 3.00% |
| LSTM 输入形状 | 12 x 19 | 12 个 1min 时间点，每个时间点 19 个输入特征 |
| 输出维度 | 3 | future_pv_p、future_load_p、future_home_soc |