# OTA（系统更新 / 槽位 / 回滚）

> 本目录聚焦：OTA 的协议、实现顺序、状态机与验收。
>
> 本项目暂未实现 OTA（`storage` 分区预留），本目录内容作为将来加 OTA 时的起点参考。

## 复用安全（必读）

- 复用任何条目前先做 Fit Check：`../guides/spec-reuse-safety-playbook.md`（OTA/写 flash 属于高风险动作，不做适配禁止落地）

## 文档

- `./system-update-playbook.md`：从"协议扩展"到"页面落地"的实施顺序与验证路径（通用思路）。
