# Playbook：Tiny TTF 中文字体 + Montserrat FontAwesome fallback（LVGL 9.x）

## 上下文签名

- 目标平台：任何 LVGL 9.x 项目（ESP32 / STM32 / 桌面 simulator）
- SDK：LVGL 9.1+（Tiny TTF 在 9.0 引入）
- 字体需求：
  - 中文字符（GBK 常用 3500+ 字）
  - `LV_SYMBOL_*` 图标（FontAwesome 私有区 U+F001 ~ U+F8FF）
  - 英文 / 数字
- 资源约束：不允许巨型全字库（SourceHanSans 全字库 ~12MB）

## 目标

用一份 TTF 承担中文 + 英文，`lv_font_montserrat_*` 承担 FontAwesome 图标，两者通过 LVGL 的 `font->fallback` 链衔接——任何 `lv_label` 上既能显示中文文本又能显示 `LV_SYMBOL_BLUETOOTH` / `LV_SYMBOL_PLAY` 等图标。

成功标准：
- `lv_label_set_text(label, "蓝牙 " LV_SYMBOL_BLUETOOTH " 已连接")` 渲染完整，无豆腐块 `□`
- glyph cache 自动落 PSRAM（不撑爆内部 RAM）
- 新增中文文字无需重新生成字体（除非用到了子集之外的字）

## 不变式（可直接复用）

1. **主字体承担正文 + fallback 兜底图标**：避免给每个 label 手工配两个字体
2. **CJK 字体不含 FontAwesome**：Source Han / 思源黑体等中文字体的 Private Use Area 是空的
3. **fallback 链单向推进**：主字体查不到 → fallback，fallback 查不到 → 豆腐块；不做环形 fallback
4. **glyph cache 大块走 PSRAM**：`CONFIG_LV_USE_CLIB_MALLOC=y` 让 LVGL 用 `heap_caps` 自动选

## 参数清单（必须由当前项目提供）

- **主字体 TTF**：哪份 CJK 字体（本项目用思源黑体 Source Han Sans SC 子集）
- **子集字符集**：从哪里扫描汉字（本项目用 `tools/gen_font_subset.py` 扫 `app/ drivers/ services/ main/`）
- **字号列表**：14px 正文 / 16px 标题 / 48px 大号时间（本项目 `app/app_fonts.c`）
- **fallback 字体**：`lv_font_montserrat_14/20/24`（必须在 sdkconfig 启用对应字号）
- **glyph cache 大小**：常规页面 256 字够用；特殊页面（如只显示 HH:MM）可调到 32

## 前置条件

- `CONFIG_LV_USE_TINY_TTF=y`
- `CONFIG_LV_TINY_TTF_FILE_SUPPORT=n`（从内存/EMBED_FILES 加载，不读文件系统）
- `CONFIG_LV_USE_CLIB_MALLOC=y`（glyph cache 自动落 PSRAM）
- `CONFIG_LV_FONT_MONTSERRAT_14/20/24=y`（按需启用）
- CJK TTF 文件已生成子集并放到源码目录

## 设计边界

- **不做**：用 LVGL 自带的 `LV_FONT_SOURCE_HAN_SANS_SC_*_CJK`（只有 1118 字，覆盖不全）
- **不做**：用 binfont + 文件系统加载（本项目无 LittleFS；binfont 和 Tiny TTF 互斥）
- **不做**：在运行时动态加载新字体
- **先做最小闭环**：单字号 + fallback 通过后，再扩展多字号

## 可替换点（示例映射）

- 字体文件路径（本项目：`app/fonts/srhs_sc_subset.ttf`）
- EMBED_FILES 符号名（`_binary_<文件名替换点为下划线>_start/end`）
- 字号具体取值（本项目 14/16/48，其他项目可能 12/20/36）
- Fallback 字体（Montserrat 只含拉丁 + FontAwesome；也可以用自己的图标字体）

## 分层与职责

- **构建阶段**：`tools/gen_font_subset.py` 扫源码提取汉字 → 输出 `srhs_sc_subset.ttf`
- **编译阶段**：`EMBED_FILES` 让 CMake 把 TTF 嵌入 `factory` 分区的镜像
- **启动阶段**：`app_fonts_init()` 用 `lv_tiny_ttf_create_data_ex` 创建 `lv_font_t*`，挂 fallback
- **运行阶段**：`lv_label_set_text` 遇到 CJK → Tiny TTF 解析 → glyph cache；遇到 `LV_SYMBOL_*` → Tiny TTF miss → fallback 到 Montserrat

## 实施步骤

1. 生成子集 TTF：

   ```bash
   python tools/gen_font_subset.py
   ```

   扫 `app/ drivers/ services/ main/` 下所有字符串里的汉字，输出 `app/fonts/srhs_sc_subset.ttf`（本项目约 3.4MB）。

2. 在 `app/CMakeLists.txt` 用 `EMBED_FILES` 嵌入：

   ```cmake
   idf_component_register(
       SRCS "app_main.c" "app_fonts.c" ...
       INCLUDE_DIRS "."
       EMBED_FILES "fonts/srhs_sc_subset.ttf"
   )
   ```

3. 在 `sdkconfig.defaults`：

   ```
   CONFIG_LV_USE_TINY_TTF=y
   CONFIG_LV_TINY_TTF_FILE_SUPPORT=n
   CONFIG_LV_TINY_TTF_CACHE_GLYPH_CNT=256
   CONFIG_LV_TINY_TTF_CACHE_KERNING_CNT=0
   CONFIG_LV_USE_CLIB_MALLOC=y
   CONFIG_LV_FONT_MONTSERRAT_14=y
   CONFIG_LV_FONT_MONTSERRAT_20=y
   CONFIG_LV_FONT_MONTSERRAT_24=y
   ```

4. `app/app_fonts.c`（本项目完整实现参考）：

   ```c
   extern const uint8_t srhs_ttf_start[] asm("_binary_srhs_sc_subset_ttf_start");
   extern const uint8_t srhs_ttf_end[]   asm("_binary_srhs_sc_subset_ttf_end");

   void app_fonts_init(void)
   {
       const size_t sz = srhs_ttf_end - srhs_ttf_start;
       g_app_font_text  = lv_tiny_ttf_create_data_ex(
           srhs_ttf_start, sz, 14, LV_FONT_KERNING_NONE, 256);
       g_app_font_title = lv_tiny_ttf_create_data_ex(
           srhs_ttf_start, sz, 16, LV_FONT_KERNING_NONE, 256);
       g_app_font_huge  = lv_tiny_ttf_create_data_ex(
           srhs_ttf_start, sz, 48, LV_FONT_KERNING_NONE, 32);

       g_app_font_text->fallback  = &lv_font_montserrat_14;
       g_app_font_title->fallback = &lv_font_montserrat_20;
       g_app_font_huge->fallback  = &lv_font_montserrat_24;
   }
   ```

   关键点：
   - `LV_FONT_KERNING_NONE` 对 CJK 无意义，节省 cache
   - 字号与 fallback Montserrat 字号对齐（14↔14, 16↔20 相近即可）
   - 只显示少量字符的 label 可把 `cache_cnt` 调到 32（如 HH:MM）
   - huge 字号（48）重点缓存数字+冒号，字符集极小

5. 验证渲染：写一个 label 同时包含中文和图标：

   ```c
   lv_label_set_text(lbl, "蓝牙 " LV_SYMBOL_BLUETOOTH " 已连接");
   ```

## 停手规则

- `g_app_font_text` 为 NULL（`lv_tiny_ttf_create_data_ex` 失败）：先查 TTF 是否 EMBED 成功（`srhs_ttf_end - srhs_ttf_start` 应等于文件大小）
- 显示中文正常、图标豆腐块：Montserrat 对应字号未启用
- 显示图标正常、中文豆腐块：TTF 子集未包含相应字符，重新跑 `gen_font_subset.py`
- glyph cache OOM：内部 RAM 太小 / PSRAM 没启用 / `LV_USE_CLIB_MALLOC=n`

## 验证顺序

1. 最小冒烟：单个 label 显示"中文"
2. Fallback 验证：label 显示 "中文" + `LV_SYMBOL_BLUETOOTH`
3. 多字号：14/16/48 三个 label 同屏
4. 压力：所有页面切换一轮，glyph cache 行为稳定（用 `lv_mem_monitor` 观察）

## 常见问题

- 图标位置偏上 / 偏下 → Montserrat 与 Tiny TTF 基线对不齐，尝试 `lv_obj_set_style_text_align`
- 部分汉字缺失 → 子集生成时漏扫某文件，检查 `gen_font_subset.py` 的扫描路径
- 启动后首次切页卡顿 → 第一次渲染填充 glyph cache，可在启动时预热常用字
- fallback 链 `font->fallback->fallback = NULL` 没断：强制 `NULL` 结尾防止野指针
