# IoT Shared（跨域规则与验收基线）

> 适用范围：`.trellis/spec/iot/**` 下的所有工作（固件 / UI / 协议 / OTA / 主机工具链）。
>
> 目标：把“跨层一致性 + 高风险动作边界 + 最小验收闭环”集中到一个地方，避免每次都从零对齐。

---

## 开始之前（必做）

| 事项 | 说明 | 参考 |
| --- | --- | --- |
| 复用前先 Fit Check | 相似场景≠相同背景，未经适配直接照搬实现细节属于高风险 | `../guides/spec-reuse-safety-playbook.md` |
| 先定义契约再实现 | 跨端接口/协议/语义先写 `*contract.md`，再写代码 | `../protocol/index.md` |
| 验收要可复现 | 变更必须能被“最小步骤”复现验证，不依赖玄学 | `../guides/anti-regression-acceptance-checklist.md` |

---

## 核心规则（MANDATORY）

### 1) 跨层契约优先（Contract-First）

- 任何会影响跨端行为的变更（协议字段、状态机、错误码、版本策略、时间戳/单位、资源路径语义）必须先更新/新增 `*contract.md`。
- 合并/重构解析逻辑前，先确保“契约描述”能覆盖所有分支，否则只是在把不确定性藏起来。

### 2) 版本与兼容策略（只追加、可回退）

- 优先采用“**只追加、不改语义**”策略：新增字段/命令应保持旧端可忽略。
- 如果必须破坏兼容：
  - 明确版本边界（例如 `protocol_version`/`feature_flags`）
  - 写清升级与回退路径（尤其涉及 OTA 或资源布局时）
  - 在验收清单里体现“新旧端组合”的行为

### 3) 线程与所有权（Owner 明确，避免隐式共享）

- UI 线程/回调只做轻量逻辑：不做阻塞 IO、不做大内存分配、不做复杂解析。
- 对共享资源（Flash/NVS、协议发送队列、UI 状态缓存、音频播放链路等）必须明确 owner：
  - 单写者（single writer）
  - 串行化入口（队列/互斥/事件）
  - 可观测的失败策略（日志 + 错误码 + 回退）

### 4) 高风险动作（Flash/OTA/持久化）

- 涉及写 Flash / OTA / NVS 的改动默认视为高风险：
  - 必须有回滚/中断恢复策略
  - 必须有验收路径（至少包含断电/失败/重试/回滚等边界）

---

## 数据与协议通用约定（建议统一）

> 这里不是强行规定“必须这样”，而是建议把你项目真实约定集中在一起。若已在具体 `*contract.md` 写明，以契约为准。

- **单位与时间戳**：默认建议统一用毫秒（ms），并在字段名里体现（如 `timestamp_ms`、`timeout_ms`）。
- **编码**：固定长度字符串字段要定义 UTF-8 截断规则（避免出现 `�`），见：`../host-tools/utf8-fixed-size-truncate-pitfall.md`。
- **字节序/对齐**：二进制 payload 必须写明 endian 与 packed 规则，并提供跨端对齐方式（如 Python `struct.pack` / C `#pragma pack` 的等价约束）。

---

## 变更时的最小同步范围（别漏）

| 你改了什么 | 默认需要同步的东西 |
| --- | --- |
| 协议字段/命令/状态机 | `../protocol/*contract.md` + 固件解析 + 主机解析/工具 + 验收清单 |
| UI 行为/页面栈/输入方式 | `../device-ui/index.md` + 对应 playbook/pitfall + 线程边界契约 |
| OTA 流程/分区/回滚 | `../ota/index.md` + 对应 contract/playbook + 验收清单 |
| 资源/文件系统布局 | 资源布局 playbook/contract + 生成/打包脚本（如有） + 验收清单 |

---

## 质量与验收（建议作为 Done 定义的一部分）

- 最小验收：按 `../guides/anti-regression-acceptance-checklist.md` 跑一遍与你改动相关的条目。
- 如果你在“反复烧录调试”里打转：优先考虑加主机侧契约测试/模拟验证，见 `../guides/host-based-contract-tests-to-break-flash-debug-loop-playbook.md`。
- 如果你在提升测试覆盖：按“主机模拟覆盖阶梯”推进，见 `../guides/iot-host-simulation-test-coverage-ladder-checklist.md`。

