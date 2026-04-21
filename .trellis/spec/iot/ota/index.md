# OTA（本项目暂未实现）

> 本项目暂未实现 OTA（`storage` 分区已预留）。将来需要时参考 `.trellis/spec/_general_library/ota/system-update-playbook.md`。

## 复用安全（必读）

- OTA/写 flash 属于高风险动作，实施前必做 Fit Check：`../guides/spec-reuse-safety-playbook.md`
- 回滚路径与验收清单先行，状态机/校验顺序不可跳步
