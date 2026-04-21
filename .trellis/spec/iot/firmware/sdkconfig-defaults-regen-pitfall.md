# Pitfall：改了 `sdkconfig.defaults` 但没删 `sdkconfig`，配置从未生效

## 上下文签名

- 目标平台：任何 ESP-IDF 项目（ESP32 / S3 / C3 / ...）
- SDK：ESP-IDF v4.x / v5.x（所有版本同此坑）
- 触发：换板型 / 改 Flash 尺寸 / 改分区 / 切字体策略 / 改内存策略后重新 build

## 证据最小集

### 复现步骤

1. 在 `sdkconfig.defaults` 改一个关键项，例如：
   ```
   # 原来：CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
   CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
   ```
2. 直接 `idf.py build`（**不删 sdkconfig**）
3. 构建通过，但 `sdkconfig` 里 `CONFIG_ESPTOOLPY_FLASHSIZE` 仍是 4MB——新 defaults 没被采纳

### 关键日志

构建阶段没有警告，只有烧录后才会暴露：
- 分区越界：`Partition table offset + size exceeds flash size`
- 运行时 `esp_flash_get_chip_size()` 返回旧值
- `idf.py flash` 把 16MB 固件按 4MB 写，中间截断

### 关键配置

任何在 `sdkconfig.defaults` 里新增或修改的行都受此规则影响：
- FlashSize / FlashMode / FlashFreq
- PSRAM 配置项
- BLE / NimBLE 选项
- LVGL 配置项（字体、内存分配器、tick 源）
- 分区表路径

## 停手规则

- 改过 `sdkconfig.defaults` 后直接 `build`：**停**，先 `rm sdkconfig`
- 不确定本次 build 是否用了新 defaults：`diff sdkconfig sdkconfig.defaults | grep <关心的项>`

## 一句话结论

**ESP-IDF 只在 `sdkconfig` 不存在时才从 `sdkconfig.defaults` 生成。改完 defaults 必须删掉 `sdkconfig` 再 build，否则修改不会生效。**

## 触发条件

- 修改 `sdkconfig.defaults`（无论改什么项）
- 保留现有的 `sdkconfig`
- 执行 `idf.py build` 或 `idf.py menuconfig`

## 现象

- 构建成功但行为和 defaults 不一致
- menuconfig 显示的值是旧的
- 烧录后分区 / Flash 大小 / 特性和预期对不上
- "感觉没改"，反复改多次

## 根因

ESP-IDF 的配置生成规则：

1. 如果 `sdkconfig` **存在**：以 sdkconfig 为准。`defaults` 只在 sdkconfig 中"没提到某项"时才作为默认值填空
2. 如果 `sdkconfig` **不存在**：基于 `sdkconfig.defaults` + `Kconfig` 的默认值生成新 sdkconfig

所以已经有 sdkconfig 的情况下，改 defaults 的大部分"新值"会被老 sdkconfig 的既有值覆盖。menuconfig 交互式修改 defaults 里已改的选项也看不到变化。

## 修复

改 `sdkconfig.defaults` 后，执行：

```bash
# Windows（bash / git bash），用 python 而非 python3
rm sdkconfig
python -m idf.py build

# 或完全清理（更稳）
python -m idf.py fullclean
rm sdkconfig
python -m idf.py build
```

对迁移板型（如换到 N16R8）、改分区表、改 Flash 尺寸这种**大改**，一律用 `fullclean` + `rm sdkconfig` 组合。

## 预防

- **版本化**：`.gitignore` 排除 `sdkconfig`（让每个开发者本地生成），`sdkconfig.defaults` **提交到仓库**
- **文档契约**：README 明文写"修改 sdkconfig.defaults 后必须删 sdkconfig 重新 build"（本项目 README line 153 已写）
- **防呆脚本**（可选）：`tools/` 写一个 pre-build 检查，对比 defaults 新增/改的行和 sdkconfig 是否匹配
- **AI 协作约定**：AI 修改 defaults 时必须在 commit 说明里提醒"需要删 sdkconfig"

## 验收标准

- 改完 defaults + 删 sdkconfig + build 成功
- 新 sdkconfig 里相关项和 defaults 一致（`grep <改动项> sdkconfig`）
- 定位路径：下次怀疑 defaults 没生效时，先 `diff sdkconfig sdkconfig.defaults | grep <关键项>`
