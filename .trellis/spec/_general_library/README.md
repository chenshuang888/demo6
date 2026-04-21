# 通用 IoT 经验库（`.trellis/spec/_general_library/`）

> **定位**：跨项目通用的 IoT / ESP-IDF / LVGL / Python 经验。本项目 demo6 暂时用不到这些，但留档备查。
>
> **来源**：2026-04-21 spec 二次筛选任务（`archive/2026-04/04-21-spec-second-pass-filter/`），从 `iot/` 按"不是本项目高频硬规则"筛出 57 条归档于此。
>
> **什么时候翻这里**：
> - 换项目时想捡回某条通用经验
> - 本项目偶然遇到以前没碰过的场景（比如将来真的做 OTA / WiFi / HTTP）
> - 想看某个通用模式的完整背景（本项目 `iot/` 里只保留硬规则，来龙去脉在这里）

---

## 子目录索引

| 目录 | 条数 | 覆盖主题 |
|---|---|---|
| [`./firmware/`](./firmware/) | 24 | 通用 ESP-IDF 组件依赖 / FreeRTOS smell / LVGL 内存管理 / WiFi / HTTP / LittleFS / 通用踩坑 |
| [`./device-ui/`](./device-ui/) | 20 | 通用 LVGL 屏幕/触摸 bring-up、字体策略、页面框架（OLED 项目蒸馏的）、样式生命周期坑 |
| [`./guides/`](./guides/) | 10 | 通用方法论（README evidence-driven / handoff-docs / legacy 清理 / host-based 测试 / page-stack 契约 / build ownership 等） |
| [`./host-tools/`](./host-tools/) | 1 | Tkinter DPI awareness |
| [`./ota/`](./ota/) | 1 | OTA 升级链路通用 playbook（本项目未实现） |

---

## 检索方式

- 想从这里挖条目：用 `grep` 或 `ripgrep` 扫文件名关键词
- 找到合适条目后，如果想让它回到主目录：`mv` 回 `iot/<相应子目录>/` 并更新对应 `index.md` 链接
- 本目录内**不做内容维护**；如果内容和某个项目代码漂移，应该先考虑"是否挪回主目录"再更新，避免浪费精力

---

## 与 `_archived_unrelated/` 的区别

| 目录 | 语义 |
|---|---|
| `_general_library/`（本目录） | 跨项目通用经验，换项目可能用得上 |
| `_archived_unrelated/2026-04-21/` | 和本项目场景完全不相关（STM32 / ASR-Pro / KVM / 动态 App / ble-control-service 退役契约等），理论上本项目永远不会用 |

两者都不影响主 `iot/` 索引；区别在"保留意图"不同。
