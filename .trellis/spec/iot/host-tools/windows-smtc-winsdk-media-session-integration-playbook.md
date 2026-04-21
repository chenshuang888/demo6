# Playbook：Windows SMTC（winsdk）媒体会话集成与兼容性边界

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中“音乐副屏”落地时遇到的关键现实：PC 端脚本读的是 Windows SMTC 的系统媒体卡片，播放器实现差异决定了你能拿到什么、能控制什么；QQ 音乐网页版是典型坑。
>
> 目标：把“媒体信息不对/控制不生效”的问题快速归因到播放器实现，而不是误以为 BLE/脚本/固件链路坏了。

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

**脚本从 SMTC 读取的是系统级媒体卡片数据源；只要 Win+A 的媒体卡片不对，脚本就不可能对。解决路径通常是“换桌面客户端”或“调整浏览器 flag”，而不是改 BLE 代码。**

---

## 数据源本质（对话解释）

`GlobalSystemMediaTransportControlsSessionManager` 提供的是 Windows 系统的“当前播放卡片”（音量弹层/操作中心能看到的那张卡片）。

因此：

- 播放器必须注册到 SMTC 才能被发现
- 播放器必须上报 metadata（title/artist）才读得到
- 播放器必须处理媒体键（VK_MEDIA_*）才能被控制

---

## QQ 音乐网页版的典型问题（对照排查）

| 症状 | 根因 | 我方可行方案 |
|---|---|---|
| 屏上一直 `Nothing playing` | 浏览器/标签页没注册进 SMTC | 开启浏览器 `hardware-media-key-handling` flag |
| title 变成“QQ音乐 - 在线试听...” | 页面没设置 `navigator.mediaSession.metadata`，浏览器用 `document.title` 兜底 | 网页方问题，我方无解 |
| title/artist 对，但 ▶⏸ 不响应 | 媒体键没转发给页面 | 开 flag；或让标签页保持前台 |
| 进度/时长为 -1 | 页面没调 `navigator.mediaSession.setPositionState()` | 网页方问题，我方无解 |

验证方法（对话强调）：按 **Win+A** 打开操作中心，看媒体卡片显示的是否是正确曲目信息。

---

## 推荐的测试组合（对话结论）

- ✅ QQ 音乐桌面客户端：SMTC 实现更完整（title/artist/进度/媒体键）
- ✅ 网易云桌面客户端：通常 OK
- ✅ Spotify 桌面版：参考实现（最容易跑通）
- ⚠️ YouTube Music 网页版：多数 OK，但依赖浏览器 flag
- ❌ QQ 音乐网页版：经常只上报 `document.title`

---

## 事件去重（实际工程必须做）

对话指出：一次切歌可能触发多个回调（media properties / playback info / timeline），如果你每次都 push，会出现“同一条 payload 推 7 遍”。

推荐策略：

- 组装成固定长度 payload 后做 byte-level 比较
- 如果与上一次完全相同，则跳过 push

---

## `winsdk` 版本坑（requirements 不能写 `>=1.0`）

对话中的真实报错：

- `No matching distribution found for winsdk>=1.0`

原因：`winsdk` 长期处于 beta（`1.0.0b*`），没有正式 `1.0`。

建议写法：

- `winsdk==1.0.0b10`（锁定可复现实验环境）
- 或 `winsdk>=1.0.0b1`（允许升级，但要接受偶发 breaking）

