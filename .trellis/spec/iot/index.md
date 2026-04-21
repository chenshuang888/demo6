# 本项目 Spec 知识库（`.trellis/spec/iot`）

> **定位**：本项目（ESP32-S3 N16R8 demo6）每次改动都要查的**29 条硬规则**，按"改什么→看哪条"快速导航。
> **精简历程**：原始 138 条 → 第一轮 Fit Check 归档 54 条场景无关 → 第二轮按使用频率归档 57 条通用经验（见 `../_general_library/`），最终 29 条（含一次 sanity check 回调整：port-layering DR 和 freertos smell checklist 挪回，侧边栏导航和 utf8-truncate 挪出）。
> **语言**：中文。专有名词、API、文件名保留英文。

---

## 按场景快速导航（Quick Navigation）

| 我要改什么 | 先看哪条 |
|---|---|
| 改 BLE（加 service / 改协议） | `./protocol/ble-custom-uuid-allocation-decision-record.md` + 对应 `./protocol/ble-*-contract.md` |
| ESP 要主动问 PC 要数据 | `./protocol/esp-to-pc-notify-request-pattern-playbook.md` |
| 改板型 / sdkconfig / Flash / PSRAM | `./firmware/esp32s3-n16r8-qio-flash-oct-psram-pitfall.md` + `./firmware/sdkconfig-defaults-regen-pitfall.md` |
| 改 NVS / 持久化 | `./firmware/nvs-single-writer-contract.md` + `./firmware/nvs-persist-settings-store-layering-playbook.md` |
| 加 LVGL 页面 | `./device-ui/lvgl-page-router-minimal-contract.md` |
| 改字体 / 图标 | `./device-ui/tiny-ttf-plus-fontawesome-fallback-playbook.md` |
| 改 PC 端（desktop_companion / ble_time_sync） | `./host-tools/desktop-companion-bleak-multiplex-playbook.md` |
| 任何改动前 | `./guides/spec-reuse-safety-playbook.md` |
| 任何改动后 | `./guides/anti-regression-acceptance-checklist.md` |

---

## 入口索引（按子目录）

| 目录 | 条数 | 聚焦 |
|---|---|---|
| [`./firmware/`](./firmware/index.md) | 10 | N16R8 硬件基线 + NVS 持久化 + 本项目 ble_conn/notify_manager + FreeRTOS smell |
| [`./device-ui/`](./device-ui/index.md) | 7 | 40 行部分刷新 + port 分层 DR/playbook + Tiny TTF 字体链 + page_router 契约 |
| [`./protocol/`](./protocol/index.md) | 6 | 5 个 BLE service 的 payload 契约 + UUID DR + 反向请求模式 |
| [`./host-tools/`](./host-tools/index.md) | 2 | desktop_companion.py + Windows SMTC |
| [`./guides/`](./guides/index.md) | 4 | Fit Check + 防复发 + UI/NimBLE 线程契约 |
| [`./ota/`](./ota/index.md) | 0 | 本项目未实现；参考 `_general_library/ota/` |
| [`./shared/`](./shared/index.md) | 0 | 跨域硬规则（暂空壳） |

---

## 核心硬规则（每次改动都要守）

1. **Fit Check 再复用**：照搬前检查平台/SDK/外设/资源/并发模型
2. **BLE 跨层语义有契约**：`protocol/*-contract.md` 是单一事实源
3. **UI 线程不阻塞 IO**：重活 → 后台任务 → 队列桥接
4. **NVS 单写者**：跨任务/回调都要遵守，参考 `firmware/nvs-*`
5. **触发端与响应端同 service**：BLE notify 不跨 service 复用（v3 规则）
6. **改 protocol/flow/config 同步更新**：固件 + 主机工具 + 契约 + 验收清单四处同步

---

## 通用 IoT 经验库

凡不在上表的通用经验（ESP-IDF / LVGL / FreeRTOS 泛化做法、WiFi/HTTP/LittleFS 等本项目不用的技术、各种 playbook / checklist）已归档到 [`../_general_library/`](../_general_library/README.md)，本项目理论用不到但留档备查。

---

## 与 Trellis 工作流的结合

新任务 PRD 落地时，按本 index 的"Quick Navigation"找入口即可。不再推荐 `task.py init-context` / `add-context`（本项目实践表明用不上那套 multi-agent pipeline）。
