# 蒸馏待办（全量块）— `C--Users-ChenShuang-Desktop-esp32-demo6.md`

> 说明：这是“从头到尾切块”的全量清单（按 score 排序）。用于优先抓最值钱的块。

- 行数：12214
- 文件大小：720157 bytes
- 时间范围：2026-04-16T12:17:13 → 2026-04-19T09:26:45
- main(U/A)：208/463
- sub(U/A)：7/21

## 块清单（按 score 降序）
| score | 域 | lvl | 起止行 | 代码块 | 标题 | 结构信号 |
| ----: | --- | --: | ---- | ----: | ---- | ---- |
| 1561 | `protocol` | 1 | 1-6241 | 229 | Conversation — `C--Users-ChenShuang-Desktop-esp32-demo6` | 结论×3 根因×2 修复×24 风险×4 协议×13 状态机×1 串口×10 NVS×14 LVGL×115 重构×12 |
| 677 | `protocol` | 1 | 7693-9267 | 46 | 返回 JSON: 温度 23.5, 湿度 62, 天气代码 2 (晴间多云), ... | 结论×5 根因×1 修复×1 风险×2 协议×10 串口×4 OTA×1 NVS×45 LVGL×19 重构×5 |
| 516 | `protocol` | 1 | 6242-7420 | 66 | data 现在是: b'\xea\x07\x04\x12\x0c\x22\x38\x05\x00\x00'  (10 字节) | 修复×1 风险×4 协议×6 状态机×1 OTA×3 NVS×7 LVGL×11 |
| 396 | `protocol` | 1 | 10945-12214 | 95 | 3) 分区布局变化，首次烧录必须 erase | 结论×1 协议×2 NVS×1 LVGL×47 |
| 224 | `protocol` | 1 | 9702-10518 | 8 | 或联调用 --dry-run 只打印不触发系统动作 | 结论×1 根因×2 回退×1 协议×2 状态机×1 OTA×2 NVS×8 LVGL×22 |
| 138 | `protocol` | 1 | 10543-10808 | 12 | Name,     Type, SubType, Offset,   Size | 风险×2 回退×4 协议×1 OTA×1 NVS×2 LVGL×12 |
| 66 | `protocol` | 1 | 9273-9531 | 16 | 第 2 轮：确认事件 OK 后跑实际动作 | 协议×1 OTA×3 |
| 51 | `ota` | 1 | 10816-10937 | 4 | 3) 分区表变动→首次烧录必须 erase | 结论×1 风险×1 回退×2 |
| 30 | `device-ui` | 1 | 7421-7564 | 2 | 伪代码骨架 | LVGL×1 |
| 29 | `device-ui` | 1 | 9534-9701 | 16 | info.playback_status (PLAYING/PAUSED) / info.position_ticks |  |
| 25 | `protocol` | 1 | 7577-7681 | 8 | 4. 在 ESP32 屏幕上：时间页 → Menu → Weather 查看 | 协议×1 LVGL×2 |
| 20 | `guides` | 1 | 10519-10534 | 4 | ESPTOOLPY_OCT_FLASH 保持未启用 |  |
| 4 | `ota` | 1 | 10537-10542 | 2 | （移除 _INTERNAL=y） |  |
| 4 | `ota` | 1 | 10812-10815 | 0 | 2) 分区表变了，build 前全清 |  |
| 4 | `ota` | 1 | 10941-10944 | 0 | 2) 分区表/嵌入文件都变了，全清重建 |  |
| 2 | `guides` | 1 | 7682-7692 | 2 | 返回 {"lat": 31.23, "lon": 121.47, "city": "Shanghai"} |  |
| 2 | `guides` | 1 | 10535-10536 | 0 | 现在是 INTERNAL；切成 EXTERNAL |  |
| 1 | `device-ui` | 1 | 9268-9269 | 0 | 如果 ble_time_sync.py GUI 还连着 ESP32，先断开或关闭它，避免扫描冲突 |  |
| 0 | `guides` | 1 | 7565-7567 | 0 | 1. 构建 + 烧录 |  |
| 0 | `guides` | 1 | 7568-7571 | 0 | 2. 安装 PC 依赖 |  |
| 0 | `guides` | 1 | 7572-7576 | 0 | 3. 运行守护脚本 |  |
| 0 | `guides` | 1 | 9270-9272 | 0 | 第 1 轮：联调，只看事件不执行动作 |  |
| 0 | `guides` | 1 | 9532-9533 | 0 | props.title / props.artist / props.album_title |  |
| 0 | `guides` | 1 | 10809-10811 | 0 | 1) defaults 改过了必须清旧 sdkconfig |  |
| 0 | `guides` | 1 | 10938-10940 | 0 | 1) 旧 sdkconfig 基于 4MB/无 PSRAM 生成，必须删 |  |
