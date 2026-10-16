# Darktable—DiVERE · macOS 构建说明（macOS-ready source package）

本源码包基于 darktable 5.6.1 + DiVERE 负片反减（冲扫）管线，已在 macOS
（Intel x86_64，macOS 14.8，Homebrew /usr/local，AppleClang 16）上完成
**全量编译并实测运行**（详见文末「本机验证记录」）。

## 包内已内置（无需网络、无需 git 子模块操作）

所有构建必需的第三方子模块内容已按 darktable `release-5.6.1` 对应的锁定提交
vendor 进源码树（不含 `.git`）：

| 目录 | 来源 | 用途 |
|---|---|---|
| `src/external/rawspeed` | darktable-org/rawspeed | RAW 解码核心 |
| `src/external/OpenCL` | KhronosGroup/OpenCL-Headers | OpenCL C 头（CL/cl.h 等） |
| `src/external/libxcf` | houz/libxcf | XCF 支持 |
| `src/external/whereami` | gpakosz/whereami | 可执行文件定位 |
| `src/external/LibRaw` | LibRaw/LibRaw | LibRaw 内嵌副本 |
| `src/external/lua-scripts` | darktable-org/lua-scripts | Lua 系统脚本 |
| `src/external/onnxruntime` | ONNX Runtime 1.23.2（osx-x86_64 预编译） | DiVERE Deep White Balance 推理后端 |

## 本次改造清单（均为代码/构建层，不触碰冲扫算法）

1. **macOS 通用 OpenCL 支持**
   - `src/CMakeLists.txt`：OpenCL 头文件解析改为“通用/无本机路径”：
     - 优先使用包内 vendor 的 Khronos 头（`src/external/OpenCL/CL/cl.h`，上游布局）；
     - 若 vendor 头缺失且处于 macOS，自动回退到系统
       `OpenCL.framework` 头：在构建目录生成一个极小的 `CL/cl.h` 转发垫片，
       `#include <CL/cl.h>` 因此可在任何 macOS（Intel/ARM、CLT/Xcode）上解析，
       不依赖任何绝对路径。
   - 运行时侧：`src/common/dlopencl.c` 原本即支持 macOS 的
     `/System/Library/Frameworks/OpenCL.framework/...` 动态加载，保持不变。
2. **macOS OpenMP 探测修复**
   - `CMakeLists.txt`：Homebrew 的 `libomp` 为 keg-only，加入
     `${HOMEBREW_PREFIX}/opt/libomp` 到 `CMAKE_PREFIX_PATH`（与既有
     llvm/icu4c 处理一致），使 `find_package(OpenMP 4.5)` 在 macOS 上可命中。
3. **清除本机绝对路径硬编码 → 相对路径 / 标准 Bundle 检索**
   - `packaging/macosx/darktable.bundle`：前缀改为占位符
     `@DEFAULT_PREFIX@` / `@DT_PREFIX@`，不再写死 `/opt/local`、`/usr/local`。
   - `packaging/macosx/make-app-bundle`：运行时探测 Homebrew 前缀
     （`brew --prefix`，无 brew 时回退 `/usr/local`），自动替换占位符；
     `gtk-mac-bundler` 与 lensfun 数据改为在标准安装布局中动态定位，
     不再硬编码 `~/.local`。
   - 运行时资源定位沿用 darktable 既有机制（相对路径 + macOS Bundle 检索，
     `src/common/file_location.c` / `src/osx/osx.mm`），DiVERE 新增模块
     （negadoctor / colorspaces / AI 模型）均经 `dt_loc_get_datadir()` 加载，
     无本机绝对路径。
4. **ONNX Runtime 配置写入不落源码目录（构建层）**
   - `cmake/modules/FindONNXRuntime.cmake`：vendor 包自带 CMake targets 配置
     含路径修补逻辑；修补写入原指向源码树，被 CMake 禁止。现改为 configure 时
     将 vendored 包镜像到构建目录（`<build>/_deps/onnxruntime`），全部修补
     只作用于构建副本，源码树零写入。

## 冻结保证（冲扫功能、数值、交互零改动）

以下文件在改造前后 SHA256 完全一致（见 `docs/divere_freeze_sha256.txt`）：

- `src/iop/negadoctor.c`、`src/iop/negadoctor_curves.h`（公式、分层反差滑块
  步长、通道 gamma、绿通道独立滑块交互逻辑、默认值）
- `src/iop/colorin.c`、`src/common/colorspaces.c`、`src/common/colorspaces.h`
- `data/color/json/Kodak2383.json`、`data/color/json/KodakEnduraPremier.json`
- `data/negadoctor_ai/config.json`、`data/negadoctor_ai/net_awb.onnx`

## 在本机全量编译（已验证的命令）

前置依赖（Homebrew；缺则装，`brew bundle install --file .ci/Brewfile`
会自动补齐大部分；本机 Intel macOS 14 上 JXL / GraphicsMagick /
libsoup / OSMGpsMap 因无 bottle 且源码构建受环境沙箱限制无法安装，
对应的 JXL / 图片导入 / Map 视图 / Web 相关为**可选特性**，缺失时
configure 自动降级为 DISABLED，不影响主程序全量编译）：

配置 + 构建：

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DUSE_AI=ON \
  -DUSE_OPENCL=ON \
  -DUSE_COLORD=OFF \
  -DUSE_KWALLET=OFF \
  -DBUILD_CMSTEST=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

说明：

- **`-DCMAKE_OSX_DEPLOYMENT_TARGET` 必须 ≥ 13.5**：darktable 5.6.1 及其内嵌
  rawspeed 已不再支持低于 13.5 的部署目标（低于会直接报
  `Targeting OSX version ... older than 13.5 is unsupported`）。
- `USE_AI=ON` 为必选：负片模块（negadoctor）的 DiVERE Deep White Balance
  直接调用 AI 后端，关闭会导致功能缺失；ONNX Runtime 由包内 vendored
  1.23.2 提供，configure 自动镜像到构建目录，无需下载、无需系统安装。
- `USE_OPENCL=ON` 为默认，即通用 GPU 加速；GPU 设备在
  首选项 → OpenCL 中管理。负片反减模块本身为 CPU 精确计算（上游设计，
  未改动）。
- 若无需 OpenCL 内核预编译校验（较慢），可追加
  `-DTESTBUILD_OPENCL_PROGRAMS=OFF`（本机 LLVM 缺失时自动关闭）。

macOS App Bundle 打包（构建输出到 `build/macosx` 后）：

```sh
cd packaging/macosx
./3_make_modified_local.sh      # 生成 darktable.app（Homebrew 依赖动态收集）
```

## 本机验证记录（2026-10-13，macOS 14.8.9 / x86_64）

- `cmake --build` 全量完成：`BUILD_RC=0`，[100%] 所有目标构建成功。
- `darktable-cltest` 编译特性：`OpenCL ENABLED`、`OpenMP ENABLED`、
  `AI ENABLED`、`Lua ENABLED`、`gPhoto2 ENABLED`、`GMIC ENABLED`。
- `libdarktable.dylib`：含 65 个 `dt_opencl*` 符号；链接
  `@rpath/libonnxruntime.1.23.2.dylib`。
- OpenCL 运行时：`[opencl_init] found 1 platform`（Apple），2 个设备
  （`AMD Ryzen 7 5800H with Radeon Graphics` iGPU 与
  `AMD Radeon Pro RX6600M Compute Engine` dGPU），`OpenCL successfully
  initialized`；内核目录经相对路径定位
  `<build>/share/darktable/kernels`。
- 渲染管线实测：`darktable-cli` 以独立 XDG 目录完成 PPM → JPEG 导出，
  输出为合法 JPEG（48×48、3 通道、baseline）。
- 冻结清单：9 项文件改造后 SHA256 与 `docs/divere_freeze_sha256.txt`
  逐字节一致。

## Ilford 相纸曲线修复记录（2026-10-13）

**现象**：负片处理 → 相纸 页中 Ilford MGFB 0/00/1/2/3/4/5 一使用画面即纯白。

**根因**：`src/iop/negadoctor_curves.h` 中 7 条 Ilford rgb 曲线的 x 坐标为
**递减**序列（约 0.81→0.33），而曲线应用 `np.interp`（`_apply_curve` →
`_interp_linear`）要求 xp 严格递增。递减数组导致绝大多数像素的归一化密度
命中 `x <= pts[0].x` 分支，恒返回首点 y≈0.995 → 输出密度被压到 ≈0 →
`exp(-density·ln10)` ≈ 0.95 → 全白（高光区全白、暗部全黑撕裂）。

**对照结论**（DiVERE 上游源码 `divere/core/math_ops.py` 与
`config/curves/Ilford MGFB *.json`）：
- 公式完全一致（`np.interp(normalized, x_points, y_points)`，加载不做反转）；
- 数据逐点一致（7 条曲线 20 个点与上游 JSON 完全吻合）；
- 即该缺陷为 **DiVERE 上游数据本身方向写反**，原版软件同样受影响，非本
  整合引入。

**修复**：仅将 7 条 Ilford rgb 数组的点对顺序反转（每点 (x,y) 原样保留，
使 x 递增）。不修改任何公式/插值/滑块/交互；Kodak 系列 8 条曲线与其余
冻结文件零改动。修复后哈希见 `docs/divere_freeze_sha256.txt` 第二节。
修复后重编译验证：`BUILD_RC=0`，`libnegadoctor.so` 重新链接成功。

## OpenCL 欠曝指示器修复记录（2026-10-13）

**现象**：开启 OpenCL 后，仅开启"欠曝指示"（直方图左侧蓝色三角）时，
预览中本应只显示蓝色欠曝区域，却额外出现红色过曝干扰。

**根因**：`src/iop/overexposed.c` 的 CPU 版 `process()` 在判定过曝（红色）
时要求 `dev->overexposed.enabled` 为真、判定欠曝（蓝色）时要求
`show_underexposed` 为真；而 OpenCL 版 `process_cl()` 调用
`data/kernels/basic.cl` 的 `overexposed` 内核时**未传入这两个开关**，
内核无条件按阈值染色，导致仅开启欠曝指示时过曝区域也被错误染红。
另：GAMUT/LUMINANCE/SATURATION 分支的内核用原始输入像素 `pixel`
计算 luminance，而 CPU 版用色彩空间转换后的像素 `img_tmp`，判定依据
不一致。

**修复**（仅两处，其余零改动）：
- `data/kernels/basic.cl`：`overexposed` 内核新增 `enabled`、
  `show_underexposed` 参数；ANYRGB/GAMUT 分支染色条件与 CPU 完全对齐
  （红需 `enabled`、蓝需 `show_underexposed`）；GAMUT/LUMINANCE/
  SATURATION 分支 luminance 改用转换后像素 `pixel_tmp`（对齐 CPU 的
  `img_tmp`）。
- `src/iop/overexposed.c`：`process_cl()` 传入 `enabled`、
  `show_underexposed` 两个参数。

重编译 `BUILD_RC=0`，`liboverexposed.so` 重新链接；新内核已同步至
构建目录 `share/darktable/kernels/basic.cl`；GUI 冒烟无致命错误。

## 版本号乱码 + 启动 Logo 缺失修复记录（2026-10-13）

**现象一：版本号显示乱码**（界面显示 `archive-cdf9f2ceb…`）。
根因：源码包（无 .git 的 zip 分发）configure 时未指定 PROJECT_VERSION，
CMake 将版本回退为 zip 文件名中的 archive 占位符并写进 `version_gen.c`。
修复：configure 增加 `-DPROJECT_VERSION=5.6.1`（真实发布版本），
`bin/version_gen.c` 即生成 `darktable_package_version = "5.6.1"`。
**此修复不改任何源码文件。**

**现象二：启动界面 Logo 消失 + GTK 主题图标警告
（`Could not load a pixbuf from …check-symbolic.svg`）。**
根因：gdk-pixbuf 的 `loaders.cache` 不含 SVG loader 注册项，
所有 .svg 资源（splash 的 `pixmaps/idbutton.svg`、Adwaita 主题图标）
加载失败（librsvg 本身已安装，但 loader 未注册）。
修复（系统级，非源码改动）：以 `gdk-pixbuf-query-loaders` 重建
`/usr/local/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache`，SVG loader 已注册。
GUI 冒烟验证：pixbuf 加载警告计数为 0。

**推荐 configure 完整参数（含版本号）：**
```
cmake -DUSE_AI=ON -DUSE_OPENCL=ON -DUSE_COLORD=OFF -DUSE_KWALLET=OFF \
      -DBUILD_CMSTEST=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
      -DPROJECT_VERSION=5.6.1 <源码树>
```

## 版本号定制（2026-10-13）

用户自定义版本显示为 **`5.6.1-银盐菲林日记`**。
CMake 的 `project(VERSION ...)` 只接受纯数字版本，因此：
- 根 `CMakeLists.txt` 版本 override 分支新增 `DT_VERSION_STRING` 变量
  （定义时优先用于 `version_gen.c`，`PROJECT_VERSION` 保持数值给 project()）；
- configure 传入 `-DPROJECT_VERSION=5.6.1 "-DDT_VERSION_STRING=5.6.1-银盐菲林日记"`。

验证：`bin/darktable-cli -v` 输出 `darktable 5.6.1-银盐菲林日记`；
GUI 冒烟无 pixbuf 警告。

**推荐 configure 完整参数（含版本定制）：**
```
cmake -DUSE_AI=ON -DUSE_OPENCL=ON -DUSE_COLORD=OFF -DUSE_KWALLET=OFF \
      -DBUILD_CMSTEST=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
      -DPROJECT_VERSION=5.6.1 "-DDT_VERSION_STRING=5.6.1-银盐菲林日记" <源码树>
```
