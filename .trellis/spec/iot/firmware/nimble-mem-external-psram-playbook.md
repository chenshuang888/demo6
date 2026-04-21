# Playbook：NimBLE 堆走外部 PSRAM（释放内部 RAM）

## 上下文签名

- 目标平台：ESP32-S3 **带 PSRAM**（8MB Octal 或 2MB Quad）
- SDK：ESP-IDF v5.x + NimBLE（内置）
- 链路：BLE 4.2（`CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n`）或 5.0
- 资源约束：项目同时跑 LVGL + 嵌入大资源（字体/图像），内部 RAM 紧张

## 目标

让 NimBLE 协议栈的 connection buffer / ACL buffer / GATT state 走 PSRAM，把内部 RAM 让给 DMA / ISR / LVGL 双缓冲等必须内部 RAM 的场景。

成功标准：
- `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` 生效
- 连接/收发 notify 稳定（至少 10 分钟持续广播 + 连接 + 推送）
- `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` 比切换前增加（典型释放 10-30KB）

## 不变式（可直接复用）

1. **BLE 回调不阻塞 UI 线程**：push 入队后立即返回，UI 任务消费（与 `../guides/nimble-ui-thread-communication-contract.md` 一致）
2. **PSRAM 访问有延迟**：ISR / DMA 回调里的 BLE 数据处理必须迁出回调外
3. **大块分配走 PSRAM，小块走内部**：通过 `SPIRAM_MALLOC_ALWAYSINTERNAL` 阈值控制
4. **堆 caps 方向单向**：一旦 NimBLE 选 EXTERNAL，它的所有分配都带 `MALLOC_CAP_SPIRAM`；不能有些走内部有些走 PSRAM

## 参数清单（必须由当前项目提供）

- `CONFIG_SPIRAM_MODE`：OCT / QUAD（看板型，本项目 OCT）
- `CONFIG_SPIRAM_SPEED`：80M / 40M（和 Flash 频率组合决定是否受支持）
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`：小于该阈值走内部 RAM（本项目用 `16384` = 16KB）
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`：保留给 DMA/ISR 等场景（本项目 `32768` = 32KB）
- NimBLE 连接数上限：本项目默认 1（不改）

## 前置条件

- 板型有 PSRAM 且在 `sdkconfig.defaults` 启用（`CONFIG_SPIRAM=y` + `_MODE_OCT=y` 或 `_MODE_QUAD=y`）
- PSRAM 初始化通过（启动日志 `SPIRAM: Found ... Octal ...`）
- BLE 启用：`CONFIG_BT_ENABLED=y` + `CONFIG_BT_NIMBLE_ENABLED=y`

## 设计边界

- **不做**：改 NimBLE 内部结构、自己重写 mbuf 池
- **不做**：把 ISR 处理逻辑塞进 NimBLE 回调里（回调上下文可能在 PSRAM 栈上）
- **先做最小闭环**：只改 `MEM_ALLOC_MODE`，跑一遍基线验收，再调其他 NimBLE 内存项

## 可替换点（示例映射）

- `ALWAYSINTERNAL` / `RESERVE_INTERNAL` 的具体数值来自本项目调参经验，其他项目需按自己的 DMA / LVGL / cache 需求重新算
- `SPIRAM_SPEED_80M` 取决于 MSPI timing 表是否支持你的板型组合，见 `./esp32s3-mspi-timing-unsupported-flash-psram-freq-combo-pitfall.md`

## 分层与职责

- **硬件层**：PSRAM 初始化（boot 阶段）
- **SDK 层**：`heap_caps` 按 `MALLOC_CAP_SPIRAM` 自动选择分配位置
- **NimBLE 层**：`MEM_ALLOC_MODE=EXTERNAL` 告诉 NimBLE 所有动态分配都带 `MALLOC_CAP_SPIRAM`
- **应用层**：无需改动，只享受收益

## 实施步骤

1. 确认 PSRAM 在 `sdkconfig.defaults` 已启用且模式正确：

   ```
   CONFIG_SPIRAM=y
   CONFIG_SPIRAM_MODE_OCT=y       # 本项目 N16R8 是 Octal
   CONFIG_SPIRAM_TYPE_AUTO=y
   CONFIG_SPIRAM_SPEED_80M=y
   CONFIG_SPIRAM_USE_MALLOC=y
   CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
   CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
   ```

2. 启用 NimBLE 外部内存：

   ```
   CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
   ```

3. **删 sdkconfig 重新 build**（见 `./sdkconfig-defaults-regen-pitfall.md`）：

   ```bash
   rm sdkconfig
   python -m idf.py build
   ```

4. 最小冒烟：烧录后 monitor，观察：
   - `NimBLE host task: nimble_host` 正常启动
   - 通过 `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` 对比上一次数值

5. 功能验证：PC 连接 + 订阅 notify + 持续推送 10 分钟，无断连/崩溃

## 停手规则

- PSRAM 未成功初始化（启动日志无 `SPIRAM: Found`）：**禁止**启用 `MEM_ALLOC_MODE_EXTERNAL`，NimBLE 会 malloc 返回 NULL 崩溃
- MSPI 频率组合不支持：先回到 80/80 基线再开外部内存
- 连接压力测试失败：先回滚 `MEM_ALLOC_MODE_EXTERNAL=n`，确认问题不在 PSRAM 延迟

## 验证顺序

1. 最小冒烟：编译通过 → 烧录 → BLE 广播可见
2. 功能验证：PC 连接 + 各 service write/notify 往返
3. 压力/边界：10 分钟持续 notify + 重连 5 次 + 断电恢复

## 常见问题

- BLE host task crash 在 `os_msys_get_pkthdr` → PSRAM 分配失败 → 检查 `SPIRAM_USE_MALLOC=y`
- notify 延迟偏高 → 正常：PSRAM 访问慢，若需要极低延迟考虑把 ACL buffer 留内部
- `nimble_host` 启动卡住 → `CONFIG_BT_NIMBLE_TASK_STACK_SIZE` 可能不够（PSRAM 上下文栈开销大），调到 5120
- 启用后某些库链接错 `esp_wifi` 相关符号 → 检查是否混启了 WiFi（本项目不启用）
