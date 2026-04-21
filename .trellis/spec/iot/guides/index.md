# 指南（蒸馏方法 / 防复发 / 验收）

> 本目录用于：把"如何使用知识库"写清楚，保证蒸馏后的条目真正可用。

## 文档

- `./spec-reuse-safety-playbook.md`：**复用安全规则**（适配检查/Fit Check、停手规则、不变式/参数/示例分层），用于避免"场景相似但背景不同"时照搬实现细节。
- `./anti-regression-acceptance-checklist.md`：如何验收"同类场景不再重复出错"。
- `./host-based-contract-tests-to-break-flash-debug-loop-playbook.md`：用主机侧契约测试 + C 行为模拟，把"反复编译烧录"变成"10 秒跑测试"。
- `./iot-host-simulation-test-coverage-ladder-checklist.md`：主机侧模拟测试覆盖阶梯：哪些能模拟、哪些必须真机，以及从 85% 到 95% 的升级路径。
- `./esp-idf-project-structure-app-drivers-main-playbook.md`：从 LVGL demo 迁移到"main + drivers + app"三层工程结构（入口切换 + 主链路冒烟）。
- `./local-build-ownership-contract.md`：协作约定：本地编译/烧录/monitor 默认由用户执行，AI 只给最小验证步骤。

### 设备 UI（非 LVGL 也适用）

- `./embedded-ui-page-stack-contract.md`：页面栈 + 生命周期契约（push/pop/home + init/enter/update/draw/key/exit）。

### 线程与并发（UI/Logic）

- `./lvgl-ui-thread-callback-and-background-task-contract.md`：LVGL 的 UI 线程/回调/后台任务边界（UI 不阻塞，IO 不进回调）。
- `./nimble-ui-thread-communication-contract.md`：NimBLE（BLE host task）与 UI 线程通信边界（队列化写入 + UI 线程应用 + `volatile` 决策框架）。

### 文档与维护

- `./handoff-docs-playbook.md`：维护 `docs/handoff/` 的交接文档套路（入口 + 总说明 + logs）。
- `./legacy-files-removal-playbook.md`：重构后删除旧文件/旧模块的安全流程（先证据，再确认，再删除）。
- `./driver-folder-semantics-contract.md`：`driver/` 目录语义边界（什么能进 driver，什么不该进）。
- `./codebase-to-readme-evidence-driven-playbook.md`：从代码库证据驱动地写 README（入口证据→主线→模块职责→可执行验证，避免编造）。
- `./readme-architecture-card-checklist.md`：README 架构卡片清单（IoT/嵌入式优先，30 秒读懂链路与约束）。
- `./ble-service-boundary-control-vs-media-decision-record.md`：决策记录：`control_service` 与 `media_service` 是否合并（语义边界与折中方案）。
