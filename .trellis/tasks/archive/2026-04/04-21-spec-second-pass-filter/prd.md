# PRD：Spec 二次筛选与清漆（激进 + 同步）

## 背景 / 目标

第一轮 bootstrap Fit Check（2026-04-21，见 `archive/2026-04/00-bootstrap-guidelines/`）归档了 54 条"场景无关"条目（STM32、ASR-Pro、KVM、多 App 等），剩下 87 条 + 7 个 index.md。

但实际使用观察（从本项目 docs/ 日志 + 本次任务经历）显示：
- **真正高频引用**的只有 3-5 条（UUID DR、反向请求 playbook、nimble-ui-thread-comm、N16R8 板型、sdkconfig regen）
- 剩余 80+ 条里大量是"通用 IoT 经验"（任何 ESP-IDF 项目都适用，不绑定本项目）或"已随代码漂移"（内容和当前代码不一致）
- 87 条平铺导致检索成本高，AI 主要靠 grep 找不靠浏览

**本轮目标**：
1. 按"本项目强绑定度"二次筛选，只保留 ~15 条真正贴身硬规则
2. 通用 IoT 经验整体搬到新的 `_general_library/` 子目录（留档但不污染主索引）
3. 对"保留条目"做内容同步清漆，消除与当前代码的漂移
4. 重写 6 个 index + 顶层 iot/index，让"随手翻一眼就知道该看哪条"

**非目标**：
- 不做物理删除，全部留档可审计
- 不重写尚未漂移的内容（只做分类 + 漂移修复）
- 不改动 `_archived_unrelated/` 里第一轮归档的 54 条

## 分类标准

每条 spec 按下表归类：

| 类别 | 标记 | 判定标准 | 处理 |
|---|---|---|---|
| **强绑定本项目** | ✅ 保留 | 满足任一：(a) 引用本项目实际代码路径/行号；(b) 描述本项目独有的决策（板型/UUID/线程模型）；(c) 改动时大概率要查 | 留在 `iot/<子目录>/`，index 保留链接 |
| **通用 IoT 经验** | 📦 归档 | 任何 ESP-IDF / NimBLE / LVGL 项目都适用；不引用本项目特定代码；更像"通用教程/方法论" | 搬到 `.trellis/spec/_general_library/<子目录>/`，主 iot/index 不再链接 |
| **已漂移** | ⚠️ 修或弃 | 内容和当前代码明显不一致（例如提到已删除的 control_service、已退役的按钮 id）| 若修复 ≤ 30 行 diff：就地修；否则归档 |
| **重复** | 🔄 合并 | 两条说的是同一件事（比如 font 相关可能有多条） | 留更贴合本项目的一条，其余归档 |

**边界判定原则（遇到分歧时）**：
- 默认偏保留（倾向 📦 归档而非 ✅ 保留时选归档，避免 iot/ 继续臃肿）
- 但"每次改 BLE 都要查"的硬规则一律保留，不管它多"通用"
- 已经在本项目 docs/ 工作日志里引用过至少一次 → 一律保留

## 归档目录结构

```
.trellis/spec/
├── iot/                             # 保留：本项目强绑定 ~15 条
│   ├── firmware/   ~5 条
│   ├── device-ui/  ~4 条
│   ├── protocol/   ~5 条（含本次刚做的退役说明）
│   ├── host-tools/ ~1 条
│   ├── ota/        0 条（整个目录归档，没落地）
│   ├── shared/     不变
│   └── index.md
├── _general_library/                # 新建：通用经验库
│   ├── firmware/
│   ├── device-ui/
│   ├── protocol/
│   ├── host-tools/
│   ├── guides/
│   └── README.md                    # 说明"这些是跨项目通用经验，保留但不常用"
└── _archived_unrelated/
    └── 2026-04-21/ (第一轮 54 条 + 本轮新归档的 ble-control-service-event-contract / ble-service-boundary DR)
```

**关键决策：为什么分 `_general_library` 和 `_archived_unrelated` 两个归档？**
- `_archived_unrelated/` 是"**场景完全不相关**"（本项目永远不会用，比如 STM32 专属）
- `_general_library/` 是"**通用但本项目没必要高频看**"（换项目可能会捡回来）
- 这两个语义不同，分开保留

## Phase 分解

一次跑完所有 Phase，不中途汇报。

### Phase 1：Inventory 盘点（并行 Agent）

派 3 个并行 Agent 分别扫描：
- Agent A：`firmware/` (34 条)
- Agent B：`device-ui/` (27 条)
- Agent C：`protocol/` + `host-tools/` + `guides/` + `ota/` + `shared/` (31 条)

每个 Agent 返回统一格式的分类表：

```
[✅|📦|⚠️|🔄] <filename>
  依据：<本项目引用证据 或 "无代码引用">
  漂移点：<若有，一句话>
  建议目的地：iot/<子目录>/ | _general_library/<子目录>/ | _archived_unrelated/
```

合并为总 inventory 表。**如果 ✅ 保留数 > 25 或 < 8 → 停下来找你调整标准**（偏差过大说明默认阈值不对）。

### Phase 2：批量归档通用条目

- `mkdir -p .trellis/spec/_general_library/{firmware,device-ui,protocol,host-tools,guides,ota}`
- 按 inventory 表 `mv` 所有 📦 条目到 `_general_library/<相应子目录>/`
- ⚠️ 且归档的（修复量大）也 mv 到 `_general_library/`（不是 `_archived_unrelated/`——它们仍然是"有用的通用经验"）

### Phase 3：就地修复漂移

对所有 ⚠️ 且选择修复的条目：
- 读当前代码或 docs/ 日志，核对事实
- 改 spec 里不一致的部分
- 典型漂移：提到 `control_service` / `page_control` / `control_event_t` / button id=0-4 这些已退役的东西

### Phase 4：合并重复

如 Phase 1 发现 🔄 重复对，保留"更贴合本项目"的那条（有代码引用的 > 纯理论的），其余归档到 `_general_library/`。

### Phase 5：重写 6 个 index + iot 顶层

- `iot/index.md`：只列保留的子目录，加 `_general_library/` 指针做"扩展阅读"
- `iot/firmware/index.md`、`iot/device-ui/index.md`、`iot/protocol/index.md`、`iot/host-tools/index.md`、`iot/guides/index.md`、`iot/ota/index.md`：只列本子目录保留条目
- `_general_library/README.md`：按子目录列所有归档条目，声明"跨项目通用经验，非本项目硬规则"

### Phase 6：回归验收

- 断链扫描：`grep` 所有 index 里的 `./xxx.md`，逐个验证文件存在
- 孤儿扫描：反向比对，iot/ 下有文件但无 index 链接的 = 遗漏（强绑定应 100% 被索引）
- 数字统计写入 `task.json` 的 `meta.verification`

## 验收清单

- [ ] `.trellis/spec/iot/` 下 .md 文件数从 87 → ≤20（含 6-7 index）
- [ ] `_general_library/` 下归档 ~65 条，按子目录分布
- [ ] 所有保留条目内容与当前代码同步（无 `control_service` 等已退役符号残留）
- [ ] `iot/index.md` 30 秒读完能找到"这次改 X 该看哪条"
- [ ] 6 个子目录 index 断链 0 / 孤儿 0
- [ ] `_general_library/README.md` 存在且分类清晰

## 风险 / 停手规则

| 风险 | 对策 |
|---|---|
| Agent 分类标准不一致（同类条目一个判 ✅ 一个判 📦） | 每个 Agent 同一 Prompt 模板；合并时由我做二次仲裁 |
| 保留数偏差过大 | Phase 1 结束先汇报数字，>25 或 <8 停下来 |
| 某条"通用 playbook"其实被本项目代码间接依赖 | inventory 时 grep 该 playbook 提到的关键词在代码里是否出现；出现则升级为 ✅ |
| 批量 mv 出错 | 每个 Agent 返回列表，Phase 2 用脚本批量 mv；先 dry-run 打印再执行 |

**整体停手**：Phase 1 后如果保留数偏差过大 → 停；Phase 3 发现多于 15 条需要修复 → 停（说明漂移严重超预期，需要重新评估）。

## 影响面（估计）

| 项 | 数 |
|---|---|
| 原始条目 | 87 + 7 index |
| 预计保留 | ~15 条 + 6 index |
| 预计归档到 `_general_library/` | ~65 条 |
| 预计就地修复漂移 | ~5-10 条 |
| 新建目录/文件 | `_general_library/` + 6 子目录 + 1 README |

## 与上下游关系

- **上游**：`archive/2026-04/00-bootstrap-guidelines/`（第一轮 Fit Check，过滤 54 无关条目）
- **本轮性质不同**：第一轮是"场景过滤"，本轮是"使用频率分层"
- **后续可能**：将来如果某个 `_general_library/` 条目突然需要，再 mv 回主目录；或半年一次的例行 spec 审计
