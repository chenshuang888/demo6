# Playbook：把字体/资源从设备文件系统提供给 LVGL（LittleFS + `/res` + LVGL FS stdio）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于“打开 LVGL 文件系统能力”“LVGL stdio 驱动”“分区 label 与挂载点”“font_manager 接到文件系统意味着什么”的一整段解释。
>
> 目标：让资源（尤其字体）不再全部编进固件，而是可从 flash 分区（LittleFS 等）按路径加载；同时保持分层清晰：文件系统挂载归 ESP-IDF，读取接口给 LVGL，资源管理由中间层统一。

---
## 上下文签名（Context Signature，必填）

> 目的：避免“场景相似但背景不同”时照搬实现细节。

- 目标平台：ESP32/ESP32‑S3/STM32/…（按项目填写）
- SDK/版本：ESP-IDF x.y / LVGL 9.x / STM32 HAL / …（按项目填写）
- 外设/链路：UART/BLE/Wi‑Fi/TCP/UDP/屏幕/触摸/外置 Flash/…（按项目填写）
- 资源约束：Flash/RAM/PSRAM，是否允许双缓冲/大缓存（按项目填写）
- 并发模型：回调在哪个线程/任务触发；谁是 owner（UI/协议/存储）（按项目填写）

---

## 不变式（可直接复用）

- 先跑通最小闭环（冒烟）再叠功能/优化，避免不可定位
- 只直接复用“原则与边界”，实现细节必须参数化
- 必须可观测：日志/计数/错误码/抓包等至少一种证据链

---

## 参数清单（必须由当前项目提供/确认）

> 关键参数缺失时必须停手，先做 Fit Check：`spec/iot/guides/spec-reuse-safety-playbook.md`

- 常量/边界：magic、分辨率、slot 大小、最大 payload、buffer 大小等（按项目填写）
- 时序策略：超时/重试/节奏/窗口/幂等（按项目填写）
- 存储语义：写入位置、校验策略、激活/回滚策略（如适用）（按项目填写）

---

## 可替换点（示例映射，不得照搬）

- 本文若出现文件名/目录名/参数示例值：一律视为“示例”，必须先做角色映射再落地
- 角色映射建议：transport/codec/protocol/ui/persist 的 owner 边界先明确

---

## 停手规则（Stop Rules）

- 上下文签名关键项不匹配（平台/版本/外设/资源/并发模型）时，禁止照搬实施步骤
- 关键参数缺失（端序/magic/分辨率/slot/payload_size/超时重试等）时，禁止给最终实现
- 缺少可执行的最小冒烟闭环（无法验收）时，禁止继续叠功能

---


## 一句话结论

**最顺的分工是：ESP-IDF 挂载 LittleFS 到 `/res`，LVGL 通过 stdio（`fopen/fread`）读 `/res/...`，资源/字体由 `asset_manager/font_manager` 统一映射与缓存。**

---

## 关键约定（建议默认就定死）

- **挂载点（base path）固定**：`/res`
- **分区 label 固定**：`resources`
- **LVGL 文件系统驱动**：优先用 `CONFIG_LV_USE_FS_STDIO=y`（LVGL 走 `fopen/fread`）
- **drive letter（可选但建议固定）**：把 `CONFIG_LV_FS_STDIO_LETTER` 和 `CONFIG_LV_FS_DEFAULT_DRIVE_LETTER` 固定成同一个值（例如 `82`，也就是字符 `'R'`）

> drive letter 的目的：统一 LVGL 的“文件路径语法”。是否需要在代码里写 `R:/...` 取决于你调用的 LVGL API 对路径的要求；不要凭感觉，直接用一个最小冒烟去验证 `lv_fs_open()` 能打开你期望的路径即可。

如果你需要“ESP-IDF 怎么集成 LittleFS（依赖名/打包宏/挂载 API）”的完整落地步骤，见：

- `spec/iot/firmware/esp-idf-littlefs-integration-playbook.md`

---

## 模块职责建议：`resources_fs` vs `font_manager` vs UI

对话里的分工建议非常清晰：

- `resources_fs.*`：**资源存储接入层**
  - 只负责：挂载资源分区、暴露 ready 状态、（可选）提供极少量文件系统状态接口
  - 不负责：标题/正文用哪个字体、某页面该用哪个资源文件
- `font_manager.*`：**字体语义与对象管理层**
  - 负责：定义 role、决定 role→资源文件的映射、管理 `lv_font_t*` 生命周期与 fallback
  - 不负责：文件系统挂载细节（这部分留给 `resources_fs`）
- UI 层：只按语义 role 取字体（`font_manager_get(role)`），不要碰路径

---

## 概念澄清：LVGL “文件系统能力”到底是什么

对话里强调：这不是说 LVGL 自己实现了一个文件系统。

更准确是：

- LVGL 具备“通过统一接口去读文件”的能力
- 但它本身不负责：
  - 挂载 flash
  - 管理 LittleFS/SPIFFS
  - 管理 SD 卡

---

## “LVGL stdio 驱动”是什么意思

这里的 `stdio` 指 C 标准库文件接口：

- `fopen`
- `fread`
- `fseek`
- `fclose`

因此启用类似 `CONFIG_LV_USE_FS_STDIO=y` 的含义是：

- 让 LVGL 走标准文件读写接口
- 前提是：底层文件系统已经由系统（ESP-IDF）挂载好，并且路径真实存在

---

## LVGL 能不能直接从文件系统读资源？

能，但必须满足链路完整：

1. flash 里有一个资源分区（例如 LittleFS）
2. ESP-IDF 把分区挂载到某个路径（例如 `/res`）
3. `/res/fonts/xxx.bin` 真实存在
4. LVGL 通过 `fopen/fread` 去读取该路径

一句话：**LVGL 能读，是因为“下面已经准备好了”。**

---

## 为什么选择“LVGL stdio”，而不是“LVGL 自己的 littlefs 驱动”

对话里给出的最自然分工是：

- **LittleFS 挂载**：ESP-IDF 负责
- **文件读取接口**：LVGL 走 stdio
- **字体/资源解析**：LVGL 或中间层负责

如果走 `LV_USE_FS_LITTLEFS`，很多时候反而会变成“LVGL 去直接理解/管理底层文件系统”，在 ESP-IDF 工程里不如“系统挂载 + LVGL 读取”来得顺。

---

## 基础术语：分区 label vs 挂载点

对话里用非常清晰的方式区分：

- **partition label（分区 label）**：flash 里那一块的“内部名字”（在 `partitions.csv` 里定义）
- **mount point（挂载点）**：系统路径前缀（例如 `/res`），表示访问哪个路径时映射到哪块分区

类比：

- label 像“储物柜编号”
- 挂载点像“门口贴的路径标识”

---

## 什么叫“文件系统没有挂载成功”

对话里的定义非常实用：**挂载就是把“真实存在的一块存储区域（分区）”和“程序可访问的路径入口”建立关联**。

因此“挂载失败”意味着：

- 路径入口（例如 `/res`）没有成功建立映射
- 程序再去打开 `/res/fonts/ui_sans_16.bin` 就会失败
- 从系统视角看：这条路径背后没有一个有效文件系统

### 常见原因（按优先级排查）

1. **分区表里没有这个分区**
   - 例如你写 `partition_label="resources"`，但 `partitions.csv` 里没有 `resources`
2. **分区 type/subtype 不匹配**
   - LittleFS 需要对应的 `data,littlefs`（或组件要求的组合）；写错 subtype 可能直接挂不上
3. **分区镜像没打包/没烧进去**
   - 分区存在，但里面没有有效 LittleFS 镜像（或者刷写步骤没生效）
4. **文件系统损坏**
5. **挂载参数不匹配**
   - label/base path 写错、配置结构体字段不对等

> 提醒：挂载成功也不代表文件一定存在；“挂载成功但文件缺失”与“挂载失败”是两类问题，日志上要区分开。

---

## “font_manager 已经接到文件系统了”意味着什么

对话里的准确表述是：

**`font_manager` 具备“按固定路径尝试加载字体文件”的能力。**

这通常意味着：

- 初始化时会尝试访问一组固定路径（例如 `/res/fonts/ui_sans_16.bin` 等）
- 如果存在就加载，不存在则走 fallback（内置字体/默认字体/报错提示）

> 关键：UI 层不应该直接 `fopen("/res/...")`；应该由 `font_manager/asset_manager` 做路径与策略管理。

---

## 验收标准

- 设备启动后能成功挂载资源分区到 `/res`
- `font_manager` 能从 `/res/fonts/` 加载至少 1~2 个字体资源并在 UI 中生效
- UI 层不出现硬编码路径；只通过 Resource ID / manager API 获取字体/图片
