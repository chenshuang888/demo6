# Contract：LVGL 页面路由最小契约（create/destroy/update + Screen 切换）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo6.md`）中对“第二个页面怎么做”“router 框架怎么设计”“show/hide 是否多余”“update 是否必要”的收敛结论。
>
> 目标：让多页面从一开始就走“可扩展但不复杂”的最优解，避免把页面逻辑全部堆进一个 `app_ui.c`。

---
## 上下文签名（Context Signature）

> 这是“契约（Contract）”，但仍必须做适配检查：字段/端序/上限/版本/可靠性策略可能不同。
> 如果你无法明确回答本节问题：**禁止**直接输出“最终实现/最终参数”，只能先补齐信息 + 给最小验收闭环。

- 适用范围：设备↔主机 / MCU↔MCU / 模块内
- 编码形态：二进制帧 / JSON / CBOR / 自定义
- 版本策略：是否兼容旧版本？如何协商？
- 端序：LE / BE（字段级别是否混合端序？）
- 可靠性：是否 ACK/seq？是否重传/超时？是否幂等？
- 校验：CRC/Hash/签名？谁生成、谁校验？

---

## 不变式（可直接复用）

- 分帧/组包必须明确：`magic + len + read_exact`（或等价机制）。
- 字段语义要“可观测”：任意一端都能打印/抓包验证关键字段。
- 协议状态机要单向推进：避免“双向都能任意跳转”的隐藏分支。

---

## 参数清单（必须由当前项目提供/确认）

- `magic`：
- `version`：
- `endianness`：
- `mtu` / `payload_max`：
- `timeout_ms` / `retry`：
- `crc/hash` 算法与覆盖范围：
- `seq` 是否回绕？窗口大小？是否允许乱序？
- 兼容策略：旧版本字段缺失/新增字段如何处理？

---

## 停手规则（Stop Rules）

- 端序/`magic`/长度上限/兼容策略任何一项不明确：不要写实现，只给“需要补齐的问题 + 最小抓包/日志验证步骤”。
- 字段语义存在歧义：先补一份可复现的样例（hex dump / JSON 示例）与解析结果，再动代码。
- 牵涉写 flash/bootloader/加密签名：先给最小冒烟闭环与回滚路径，再进入实现细节。

---


## 一句话结论

**对“切页即销毁重建”的场景，页面生命周期只保留 3 个回调即可：`create`（必须）/`destroy`（可选）/`update`（可选）。**

`show/hide` 只有在“页面缓存不销毁，只是隐藏/显示”时才需要。

---

## 分层与目录建议（framework 与 app 分离）

对话中的建议结构（语义）：

```
demo6/
  framework/
    page_router.c/h
  app/
    pages/
      page_time.c/h
      page_menu.c/h
    app_main.c/h
  drivers/
  main/
```

原则：

- `framework/`：只管“页面注册/切换/销毁/轮询 update”，不写业务 UI
- `app/pages/`：每页一个模块，内聚页面私有状态

---

## 页面回调契约（最小化）

```c
typedef struct {
    lv_obj_t *(*create)(void);  // 必须：创建页面 screen/root，并返回根对象
    void (*destroy)(void);      // 可选：释放页面私有资源（不提供则只删除对象）
    void (*update)(void);       // 可选：UI 线程内主动轮询（时间/传感器/动画）
} page_callbacks_t;
```

语义：

- `create()`：返回一个 **screen/root**（通常 `lv_obj_create(NULL)`）
- `destroy()`：释放页面私有资源，并清空页面内部静态指针（防止悬挂引用）
- `update()`：页面主动逻辑入口；触摸回调是被动的，`update()` 是主动的

---

## 路由器行为契约

### 注册

- `page_router_register(id, callbacks)`：注册页面回调表
- 重复注册策略要明确（拒绝/覆盖二选一，建议拒绝并报错）

### 切换

`page_router_switch(id)` 的最小行为：

1. 若存在当前页：
   - 调用当前页 `destroy()`（如果提供）
   - 删除当前 screen/root（如果 destroy 未做或你选择框架统一删除）
2. 调用新页 `create()` 创建新 screen/root
3. `lv_scr_load(new_screen)` 切换到新页面

> 关键约束：切换过程必须保证旧页面对象都被释放（否则会积累内存）。

---

## `update()` 为什么要保留（对话收敛点）

对话中的核心理由：

- 触摸回调属于被动触发（用户点才执行）
- `update()` 是 UI 线程里“唯一稳定的主动轮询入口”

典型场景：

- 时间页：周期刷新时间显示
- 传感器页：定期刷新温湿度
- 动画：逐帧推进

---

## UI 线程建议

把所有 LVGL 对象操作放进 UI 任务（同一线程），避免跨线程锁复杂度。

示意：

```c
while (1) {
    page_router_update();      // 调当前页 update（若有）
    lvgl_port_task_handler();  // 驱动 LVGL tick/flush
    vTaskDelay(pdMS_TO_TICKS(XX));
}
```

---

## 验收标准

- 新增第 2/3/… 页时无需改旧页面文件，只需新增模块并注册
- 页面切换不会越跑越卡（无对象泄漏/内存持续增长）
- 动态内容只写在 `update()` 或触摸回调里（UI 修改集中、线程安全）

