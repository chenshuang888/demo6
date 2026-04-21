# Playbook：ESP32-S3 N16R8（16MB Flash + 8MB PSRAM）配置体检与迁移落地（含 BLE/LVGL/分区/清缓存）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中为“中文字体 + TinyTTF + BLE 桌面伴侣”扩容时暴露的硬件基线问题：Flash 被配成 2MB、PSRAM 未启用、默认分区表 app 只有 1MB、NimBLE 内存占用挤爆内部 RAM 等。
>
> 目标：把“硬件基线项”一次性迁到正确状态，并给出**必须执行的清理/擦除动作**，避免“defaults 写了但实际没生效”“分区表变了但没擦导致诡异行为”的反复翻车。

---

## 上下文签名（Context Signature，必填）

- 目标平台：ESP32‑S3 + 模组为 **N16R8（16MB Flash + 8MB PSRAM）**
- SDK：ESP‑IDF 5.x（Kconfig/sdkconfig.defaults/自定义分区表）
- 典型压力源：NimBLE + LVGL + 字体/资源（需要把大块内存与部分子系统迁移到 PSRAM）
- 风险等级：高（Flash/PSRAM 频率组合不匹配会 boot loop；分区/配置不生效会出现诡异行为）

---

## 不变式（可直接复用）

- 关键配置改动必须配套“清理/全擦”：否则你看到的是旧配置残留的假象
- **先保证能稳定开机**，再追求内存/性能优化（不要一口气开一堆选项）
- 分区表/PSRAM/Flash 模式属于“硬件基线”，必须有启动日志证据链

---

## 参数清单（必须由当前项目提供/确认）

- 模组真的是 N16R8 吗？（不同模组 Flash/PSRAM 组合不同）
- Flash 模式与频率：QIO？是否允许 Octal Flash？（很多模组只 PSRAM 是 OCT）
- PSRAM 模式/频率：OCT？80MHz？是否能稳定工作
- 分区规划：`factory` app 需要多大？资源/存储区需要多大？是否预留 OTA
- NimBLE/LVGL 的内存策略：哪些必须放 PSRAM，哪些必须留内部 RAM

---

## 停手规则（Stop Rules）

- 不确定模组型号/Flash 类型时，禁止启用 `ESPTOOLPY_OCT_FLASH` 之类高风险选项
- 修改 `sdkconfig.defaults`/`partitions.csv` 后没有做到“删旧 sdkconfig + fullclean + 首次 erase-flash”，禁止继续排查上层问题
- 启动日志里看不到 PSRAM/分区证据链，禁止继续做 LVGL/字体/BLE 的上层联调

---

## 一句话结论

**N16R8 的安全组合是：QIO Flash（16MB）+ OCT PSRAM（8MB，80MHz），并且 `ESPTOOLPY_OCT_FLASH` 必须保持关闭；改完 `sdkconfig.defaults/partitions.csv` 后要删旧 `sdkconfig` + `idf.py fullclean`，首次烧录必须 `idf.py erase-flash`。**

---

## 关键配置清单（对话中的“必须改”）

### 1) Flash

- `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`
- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- `CONFIG_ESPTOOLPY_FLASHSIZE="16MB"`
- **保持 `CONFIG_ESPTOOLPY_OCT_FLASH` 未启用**

> 对话里的关键坑：**N16R8 的 Flash 是 QIO，不是 OCT；只有 PSRAM 是 OCT。** 把 OCT_FLASH 也打开是社区最常见的“直接不开机”原因。

### 2) PSRAM（OCT + 80MHz + malloc 策略）

对话给出的示例（核心含义是：大块内存自动走 PSRAM）：

- `CONFIG_SPIRAM=y`
- `CONFIG_SPIRAM_MODE_OCT=y`
- `CONFIG_SPIRAM_TYPE_AUTO=y`
- `CONFIG_SPIRAM_SPEED_80M=y`
- `CONFIG_SPIRAM_BOOT_INIT=y`
- `CONFIG_SPIRAM_USE_MALLOC=y`
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`（>16KB 分配优先走 PSRAM）

### 3) BLE：把 NimBLE 内存放到 PSRAM

- `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`（并移除 `_INTERNAL`）

对话中的动机：释放内部 RAM 给 UI task 栈/中断/关键路径，避免 BLE + UI + 字体一起挤爆内部堆。

### 4) LVGL：优先用 libc malloc（让 heap_caps 自然把大块分配放到 PSRAM）

对话中的建议：启用 `CONFIG_LV_USE_CLIB_MALLOC=y`，比“硬指定一个 256KB 内部池”更稳。

---

## 分区表：默认 `singleapp`（1MB app）不够用

对话中明确：只要你开始做字体/资源/多 service，默认 `singleapp` 基本必爆。

最小可用示例（按需调整 app 大小；对话从 4MB 扩到 6MB 给 TinyTTF 留余量）：

```csv
# Name,     Type, SubType, Offset,   Size
nvs,        data, nvs,     0x9000,   0x6000
phy_init,   data, phy,     0xf000,   0x1000
factory,    app,  factory, 0x10000,  0x600000   # 6MB（对话中为嵌入 TTF 预留）
storage,    data, spiffs,  ,         0xA00000   # 剩余给资源/文件系统/未来 OTA
```

并在 `sdkconfig(.defaults)` 里启用：

- `CONFIG_PARTITION_TABLE_CUSTOM=y`
- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`

---

## 必须执行的“清缓存/擦除”动作（对话强调：不做就等着怪）

### 1) defaults 改过了：必须让新的 `sdkconfig` 真正生成

对话里的关键事实：

- 新增的 Kconfig 项只有在 **重新生成/重新配置** 时才会进入 `sdkconfig`

可选动作（择一）：

- 删除/移走旧 `sdkconfig` 后再构建（PowerShell：`Remove-Item sdkconfig`；或用 `rm sdkconfig`）
- 或执行一次显式 reconfigure（以你团队工作流为准）

### 2) 分区表改了：build 前全清

- `idf.py fullclean`
- `idf.py build`（顺便观察 `factory` 分区占用比例）

### 3) 分区布局变化：首次烧录必须全擦

- `idf.py erase-flash`
- `idf.py flash monitor`

---

## 验证要点（最少证据）

- 启动日志出现：
  - `esp_psram: Found 8MB PSRAM device`
  - `esp_psram: Speed: 80MHz`
- 分区占用合理（对话里用“factory 占用比例”做快检）
- BLE/NimBLE 迁到 PSRAM 后，内部 RAM 压力明显下降（可用 `idf.py size-components` 对比）

---

## 风险与回退

- boot loop：优先检查是否误开 `ESPTOOLPY_OCT_FLASH`，再检查实际模组是否真是 N16R8
- 回退：回滚 `sdkconfig.defaults/partitions.csv`，删除新生成的 `sdkconfig`，再重新 build
