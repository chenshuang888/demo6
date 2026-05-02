# notif_pkg

动态 app 复刻原生通知 app 的测试包。

## 上传到设备

通过 BLE 上传整个 `notif_pkg/` 目录（main.js + icon.bin 等）到 LittleFS。
icon.bin 可用项目根目录 tools/ 里的脚本生成（与其它 _pkg 类似）。

## 协议

PC 端推送通知：

```json
{ "to": "notif", "type": "add",
  "body": { "title": "微信", "body": "妈妈：晚饭吃了吗",
            "ts": 1735689600, "cat": "msg" } }
```

`cat` 取值：`msg` / `mail` / `call` / `cal` / `social` / `news` / `alert`。

PC 端清空通知：

```json
{ "to": "notif", "type": "clear" }
```

## 验收点

- [ ] 列表显示 5 条假数据
- [ ] iOS 浅色风格、64px 卡片节奏整齐
- [ ] 类别图标颜色按 cat 区分
- [ ] HH:MM 时间正确（依赖 OS 时间）
- [ ] 点列表项 → 弹模态详情，包含完整时间戳
- [ ] 模态点"删除"→ 列表项消失 + toast
- [ ] 模态点"关闭"或点遮罩 → 关闭模态
- [ ] 长按右上徽章 → 弹"清空所有"模态
- [ ] 屏底 30px 上滑 → toast 提示（受限于无 sys.app.exit）
- [ ] BLE 推 add 消息 → 新通知插入到顶部 + toast
- [ ] sys.app.saveState 退出后下次重进恢复
