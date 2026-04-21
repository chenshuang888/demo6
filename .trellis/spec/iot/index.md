# 物联网 / 嵌入式知识库（`.trellis/spec/iot`）

> 目标：把“可复用的落地路径 / 契约 / 坑位 / 验收方式”沉淀成**可导航的入口**，让 AI/新人在同类场景下不重复踩坑，按稳定工作流推进。
>
> 约定：本目录默认使用简体中文；专有名词、命令名、API 名、文件名保留英文。

---

## 范围（你会在这里找到什么）

- 固件：ESP-IDF / FreeRTOS / 驱动 / 存储与并发（NVS 等）
- 设备 UI：LVGL / 屏幕 / 触摸 / 字体 / 内存与性能
- 协议与工具链：串口/蓝牙/网络协议、命令表、状态机、解析与兼容策略
- OTA：分区/槽位、回滚、版本检查、下载与写入、进度回调
- 主机端工具：PC/Web/Electron、串口管理、Uploader/Downloader
- 跨层一致性：设备 ↔ 主机 ↔ Web/云 的语义对齐与版本兼容

---

## Related Guidelines（跨域必读）

| Guideline | 位置 | 什么时候读 |
| --- | --- | --- |
| IoT Shared（跨域硬规则） | `./shared/index.md` | 总是（任何 IoT 变更前） |
| 复用安全（Fit Check） | `./guides/spec-reuse-safety-playbook.md` | 复用任何条目前 |
| 防复发验收清单 | `./guides/anti-regression-acceptance-checklist.md` | 改动完成后验收前 |

---

## 入口索引（从这里开始）

| 领域 | 入口 | 说明 | 优先级 |
| --- | --- | --- | --- |
| 共享规则 | `./shared/index.md` | 跨层语义、版本、错误码、验收与协作约束 | **Must Read** |
| 固件 | `./firmware/index.md` | ESP-IDF/FreeRTOS、并发/存储、项目基线、典型坑位 | **Must Read** |
| 设备 UI | `./device-ui/index.md` | LVGL port 分层、旋转/坐标、字体与资源、稳定性边界 | **Must Read** |
| 协议 | `./protocol/index.md` | 帧格式、命令表、状态机、错误码、兼容策略、契约 | **Must Read** |
| OTA | `./ota/index.md` | 升级流程、分区/槽位、回滚、验收路径 | **Must Read** |
| 主机工具链 | `./host-tools/index.md` | 串口与协议栈分层、前后端拆分、共享脚本风险 | Reference |
| 使用指南 | `./guides/index.md` | 如何复用条目、如何验收、如何脱离 flash-debug 死循环 | Reference |

---

## 与 Trellis 工作流的结合（最小用法）

```bash
# 为某个任务初始化上下文（让 AI 默认拿到 IoT 入口 spec）
python ./.trellis/scripts/task.py init-context <taskDir> iot

# 根据任务需要再补充注入更具体的条目（固件/UI/协议/OTA/主机工具）
python ./.trellis/scripts/task.py add-context <taskDir> implement .trellis/spec/iot/<path>.md "<reason>"
```

---

## 按任务快速找文档（Quick Navigation）

| 我要做什么 | 先读这些 |
| --- | --- |
| 复用一个“看起来很像”的条目 | `./guides/spec-reuse-safety-playbook.md` |
| ESP-IDF 新项目/迁移基线 | `./firmware/esp-idf-project-baseline-checklist.md` |
| NVS/持久化（并发风险） | `./firmware/nvs-single-writer-contract.md`、`./firmware/nvs-concurrency-pitfall.md` |
| LVGL port 分层/bring-up | `./device-ui/lvgl-port-layering-playbook.md` |
| 协议改动/新增命令（跨端一致） | `./protocol/index.md`（先找对应 `*contract.md`） |
| 做一次 OTA 升级链路（含回滚） | `./ota/system-update-playbook.md` |
| 把“反复烧录调试”变成“本机快速验证” | `./guides/host-based-contract-tests-to-break-flash-debug-loop-playbook.md` |

---

## 核心硬规则摘要（Core Rules Summary）

1. **先 Fit Check 再复用**：相似场景常常“背景不同”，未经适配直接照搬实现细节，优先视为高风险。  
2. **跨层语义必须有契约**：协议字段/编码/长度/对齐/单位/版本策略要写进 `*contract.md`，避免“两边各写一套解析”。  
3. **UI 线程/回调不做阻塞 IO**：UI 只做轻量渲染与事件分发，重活/IO 下沉到后台任务，通过队列/通知桥接。  
4. **持久化遵循单写者**：NVS/Flash 写入必须明确 owner 与串行化策略，避免“偶发坏掉”。  
5. **OTA 属于高风险动作**：必须设计回滚路径与验收清单，流程/状态机/校验顺序不可随意跳步。  
6. **变更要同步更新文档与工具链**：协议/流程/配置变更默认要同步更新：固件实现、主机工具、契约文档、验收清单。  

---

## 文档类型（如何写、如何读）

本知识库条目以 5 类为主（模板位于 `./_templates/`）：

1. **Playbook**：从 0 到通的落地步骤（包含验证顺序）。  
2. **Pitfall**：症状 → 根因 → 修复 → 预防 → 验收。  
3. **Checklist**：防漏检查表（上线/重构/跨层变更）。  
4. **Contract**：协议/数据结构/语义 + 版本兼容规则（跨层一致性核心）。  
5. **Decision Record**：取舍与边界（为什么选 A 不选 B，如何回退）。  

---

## 与其他 spec 的关系

- `.trellis/spec/guides/`：通用思维导图（根因分析、跨层思考、复用思考等）。
- `.trellis/spec/iot/`：按 **IoT 真实链路**组织（固件 ↔ UI ↔ 协议 ↔ OTA ↔ 主机工具），并补齐跨层契约与验收。
