# 指南（Fit Check / 线程边界 / 防复发）

> 本目录聚焦：**本项目**日常每次改动都要守的 4 条元规则 + 线程契约。
> 通用方法论（README / handoff-docs / legacy 清理 / host-based 测试 / page-stack 等）已移到 `.trellis/spec/_general_library/guides/`。

## 硬规则（Must Read）

- `./spec-reuse-safety-playbook.md`：**Fit Check** —— 复用任何 spec 条目前必走的适配检查
- `./anti-regression-acceptance-checklist.md`：改动完成后的防复发验收清单

## 线程与并发

- `./lvgl-ui-thread-callback-and-background-task-contract.md`：LVGL UI 线程、回调、后台任务边界（UI 不阻塞，IO 不进回调）
- `./nimble-ui-thread-communication-contract.md`：NimBLE host 线程 ↔ UI 线程通信契约（队列入队 + UI 线程消费 + `volatile` 策略）
