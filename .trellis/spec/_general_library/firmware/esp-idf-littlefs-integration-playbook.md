# Playbook：ESP-IDF 集成 LittleFS（依赖声明 + 分区镜像打包 + 挂载 API）

> 来源：对话蒸馏（`C--Users-ChenShuang-Desktop-esp32-demo5.md`）中关于 LittleFS 的三类关键信息：`idf_component.yml` 依赖写法、`littlefs_create_partition_image(...)` 打包宏、以及 `esp_vfs_littlefs_register(...)` 的挂载 API。
>
> 目标：把“能跑起来”的最小接法写成清晰步骤，并把最容易写错的组件名/挂载概念一次讲透。

---

## 上下文签名（Context Signature，必填）

- 目标平台：ESP‑IDF 5.x（CMake 构建）
- 目标：构建期把资源目录打进固件分区；运行期挂载到固定路径（例如 `/res`）
- 风险等级：中（分区/label 不一致会导致挂载失败；误格式化会丢资源）
- 依赖方式：IDF Component Manager（manifest）+ 组件 `REQUIRES`（CMake）

---

## 不变式（可直接复用）

- manifest 的依赖坐标 ≠ `REQUIRES` 的组件名：两套命名体系必须分清
- 资源分区一般不应 `format_if_mount_failed=true`（上线时避免把资源格式化掉）
- 分区 label 必须“打包宏/分区表/挂载配置”三者一致

---

## 参数清单（必须由当前项目提供/确认）

- 资源分区 label（例如 `resources`）与 mount `base_path`（例如 `/res`）
- 分区表：是否存在 `data,littlefs` 分区、大小是否足够
- 资源目录：仓库内资源路径（例如 `resources/`）与打包策略
- format 策略：Bring‑up 阶段是否允许临时 format；上线阶段是否强制关闭
- 依赖锁定：`joltwallet/littlefs` 版本是否需要锁定（避免漂移）

---

## 停手规则（Stop Rules）

- 你还没确定“分区 label/分区表/挂载路径”三者一致：禁止开始上层依赖（例如直接让 LVGL 从 `/res` 读字体）
- 你无法确认 `littlefs_create_partition_image(...)` 是否真的执行：禁止判断“资源读取失败”是上层 bug
- 准备上线却还开着 `format_if_mount_failed=true`：必须先停手改回 `false`

---

## 一句话结论

在 ESP-IDF 里接 LittleFS，建议按这条最稳链路走：

1. 用 **IDF Component Manager** 引入 `joltwallet/littlefs`（这是依赖坐标）
2. 用 `littlefs_create_partition_image(partition_label, folder, FLASH_IN_PROJECT)` 在构建期把本地目录打包进分区
3. 运行期用 `esp_vfs_littlefs_register()` 把该分区挂载到一个固定路径（例如 `/res`）

并且要记住一个关键坑：**manifest 里的依赖坐标 ≠ CMake `REQUIRES` 里的组件名**。

---

## 1) 依赖声明：`idf_component.yml` 怎么写

对话里给出的稳定做法是：**按组件注册表示例做精确版本锁定**（避免“今天能编、明天就崩”）。

示例（版本号仅作示意，实际以你项目锁定为准）：

```yaml
dependencies:
  joltwallet/littlefs: "==1.20.4"
```

要点：

- `joltwallet/littlefs` 是“依赖坐标”（registry package），用于拉取组件
- 真正在 `idf_component_register(... REQUIRES ...)` 里写的，通常是组件目录/组件名（见下方 pitfall）

---

## 2) 构建期打包：把 `resources/` 变成 LittleFS 分区镜像

对话里确认的宏名是：

- `littlefs_create_partition_image(partition_label folder FLASH_IN_PROJECT)`

典型用法（示例）：

```cmake
# 把仓库内 resources/ 目录打进名为 resources 的 LittleFS 分区镜像
littlefs_create_partition_image(resources resources FLASH_IN_PROJECT)
```

配套要求：

- 分区表里必须存在一个 `data,littlefs` 分区，且 **label 与上面的 `partition_label` 对得上**

> 分区大小怎么定属于工程约束（flash 容量/OTA/资源量）。第一版建议先给足够空间，并保留 fallback（见资源链路 playbook）。

---

## 3) 运行期挂载：把分区挂到 `/res`

运行期关键点：

- 头文件：`#include "esp_littlefs.h"`
- 配置结构体：`esp_vfs_littlefs_conf_t`
- 挂载函数：`esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf)`
- 卸载函数：`esp_vfs_littlefs_unregister(const char *partition_label)`

最小挂载示例（路径/label 按你的约定修改）：

```c
#include "esp_littlefs.h"

esp_vfs_littlefs_conf_t conf = {
    .base_path = "/res",
    .partition_label = "resources",
    .format_if_mount_failed = false,
};

esp_vfs_littlefs_register(&conf);
```

关于 `format_if_mount_failed`：

- **资源分区通常不希望被格式化**（它的内容来自构建期镜像，格式化意味着资源丢失）
- 只有在“Bring-up 调试、分区内容未初始化”的场景，才考虑临时设为 `true`；上线前建议回到 `false`

---

## 4) 关键坑：`REQUIRES` 组件名写错（依赖坐标 vs 组件名）

对话里特意强调过的坑点：

- 你在 `idf_component.yml` 里写的是：`joltwallet/littlefs`（依赖坐标）
- 但你在 `idf_component_register(... REQUIRES ...)` 里大概率应该写的是：`littlefs`

原因：

- `REQUIRES/PRIV_REQUIRES` 用的是 **组件名/组件目录名**（CMake 视角）
- 依赖坐标是 **组件注册表的坐标**（Component Manager 视角）

因此出现下面几种“看起来像对、其实会错”的写法时，要优先怀疑组件名搞混了：

- 把 `joltwallet/littlefs` 写进 `REQUIRES`
- 把 `esp_littlefs`（头文件名/仓库名的影子）当成 `REQUIRES` 组件名

验收方式（最直接）：

- 编译期：包含 `#include "esp_littlefs.h"` 不报找不到头
- 链接期：`esp_vfs_littlefs_register` 符号可解析

---

## 5) 最小验收清单

- [ ] 构建期：`littlefs_create_partition_image(...)` 有执行（产物被打进 flash）
- [ ] 运行期：`esp_vfs_littlefs_register(...)` 返回成功
- [ ] 路径可访问：`fopen("/res/...")` 能打开一个实际存在的资源文件

### 运行时日志验收点（对话中的典型例子）

- **分区表生效**：启动日志的 `boot: Partition Table:` 里能看到 `resources` 分区条目  
  - 例如出现类似：`boot:  3 resources ...`  
  - 其中 `Unknown data` 对 data 分区并不罕见，重点是“分区名/偏移/大小被识别到了”
- **挂载成功**：你自己的挂载模块日志能确认把 label=`resources` 挂到了 `/res`  
  - 例如：`resources_fs: mounted resources at /res`
- **上层行为符合预期**：如果 `/res/fonts/` 里还没有真实字体文件，上层出现 fallback warning（例如 `use fallback font for /res/fonts/...`）通常是**完全符合预期**，不应当被当作 bug
