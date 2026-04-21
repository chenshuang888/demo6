# Pitfall：ESP32-S3 N16R8 的 Flash 是 QIO 不是 Octal（启用 OCT_FLASH 会 boot loop）

## 上下文签名

- 目标平台：ESP32-S3 **N16R8**（16MB Flash + 8MB PSRAM）
- SDK：ESP-IDF v5.4+
- Flash：16MB **QIO**（Quad-I/O），80MHz
- PSRAM：8MB **Octal**，80MHz
- 典型板：YD-ESP32-S3 N16R8 / ESP32-S3-WROOM-1U-N16R8

> 注意：芯片命名里 **R8** 指 8MB Octal PSRAM，**N16** 指 16MB Flash。Flash 封装走 Quad SPI（QIO 模式），**不是** Octal SPI。容易把 "R8 = Octal PSRAM" 误推到 "N16 Flash 也是 Octal"。

## 证据最小集

### 复现步骤（3 步触发 boot loop）

1. 在 `sdkconfig.defaults` 把 Flash 配成 Octal：`CONFIG_ESPTOOLPY_OCT_FLASH=y`
2. 删除 `sdkconfig` 后重新 `idf.py build` 让 defaults 生效
3. `idf.py -p COM3 flash monitor` 后观察启动日志

### 关键日志（典型表现）

```
rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
SPIWP:0xee
mode:QIO, clock div:2
load:0x3fcXXXXX,len:xxx
invalid header: 0xffffffff   ← 读不到 image，因为 MSPI 时序被 OCT 打乱
ets_main.c 371 fatal error
```

或在 `esptool` 下载阶段就报 `Invalid head of packet` / `MD5 of file does not match`。

### 关键配置

- `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`（必需）
- `CONFIG_ESPTOOLPY_OCT_FLASH=n`（**必须保持 OFF**）
- `CONFIG_SPIRAM_MODE_OCT=y`（PSRAM 是 OCT）
- `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y`
- `CONFIG_SPIRAM_SPEED_80M=y`

### 边界条件

- 只在 N16R8 / N16R8V 这类"Flash QIO + PSRAM OCT"混合封装上触发
- 纯 N16（无 PSRAM）或 N8R2（Quad PSRAM）不会有这组冲突
- 若 PSRAM 改成 Quad（罕见），`OCT_FLASH` 才允许开

## 停手规则

- 不确定板卡是 N16R8 / N16R8V 还是 N16R2 / N16 之前盲改 `OCT_FLASH`：先读芯片丝印 / 板卡规格书
- 任何让 Flash 和 PSRAM 同时改 OCT/QUAD/freq 的组合：先最小冒烟，别一次整合多个 high-risk 改动

## 一句话结论

**N16R8 的 Flash 是 Quad，不是 Octal。`CONFIG_ESPTOOLPY_OCT_FLASH` 必须保持 OFF，否则 MSPI 时序与 OCT PSRAM 冲突，表现为启动反复重启或下载阶段 MD5 mismatch。**

## 触发条件

- 使用 N16R8（或 N16R8V）板型
- `sdkconfig` 里 `CONFIG_ESPTOOLPY_OCT_FLASH=y` 被误开（常见原因：从"N32R8V"或 "通用 S3 OCT" 模板复制配置）

## 现象

- 启动反复重启：`rst:0x1` → `invalid header` → `rst:0x3` 循环
- 或 `idf.py flash` 报 `MD5 of file does not match` / `Invalid head of packet`
- 有时能偶尔启动一次，`esp_flash_get_chip_size()` 读出来是错的

## 根因

S3 的 MSPI 控制器只有**一套** IO 时钟输出给 Flash 和 PSRAM：
- N16R8 板型里 Flash 被焊成 Quad 封装，PSRAM 是 Octal 封装
- 告诉 bootloader "Flash 也是 OCT"，它会按 8-wire 去驱动 4-wire 的 Flash，时序错乱
- 因为和 OCT PSRAM 共享 MSPI 总线，错序信号会串扰，导致偶发读失败

## 修复（最小改动优先）

在 `sdkconfig.defaults`：

```
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
# 必须保持未启用：
# CONFIG_ESPTOOLPY_OCT_FLASH is not set

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
```

改完必须删掉 `sdkconfig` 再 build（见 `./sdkconfig-defaults-regen-pitfall.md`），否则 defaults 不会被重新解析。

## 预防

- **防呆**：在 `sdkconfig.defaults` 里明确注释板型和 FLASH/PSRAM 模式映射关系（本项目 `sdkconfig.defaults` line 52-76 正是这样）
- **CI 卡点**：有 CI 的话在构建前 grep 确保 `CONFIG_ESPTOOLPY_OCT_FLASH` 未启用
- **文档契约**：README 放板型表，任何更换芯片型号必须同步

## 验收标准

- 启动日志出现 `SPIWP:0xee` + `mode:QIO, clock div:2`
- `esp_flash_get_chip_size()` 返回 `0x01000000`（16MB）
- `esp_psram_get_size()` 返回 `0x00800000`（8MB）
- 下载/回读稳定，100 次 reset 无 boot loop
- 定位路径：若再出现启动循环，先看 monitor 前 10 行 `mode:` 字段是否是 `QIO`
