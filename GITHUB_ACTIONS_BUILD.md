# DiVERE GitHub Actions 自动编译

## 产物

工作流 `.github/workflows/release-divere.yml` 会生成三个安装包：

1. `darktable-DiVERE-5.6.1-win64.exe`：Windows x64 安装程序
2. `darktable-DiVERE-5.6.1-x86_64.dmg`：Intel 芯片 macOS DMG
3. `darktable-DiVERE-5.6.1-arm64.dmg`：Apple Silicon / M 系列芯片 macOS DMG

DiVERE 自己的打包脚本位于 `packaging/divere/`，没有调用原版的 `packaging/macosx/*hb*.sh` 或 `packaging/windows/darktable.iss.in`。

## 手动编译

1. 把整个源码目录上传到 GitHub 仓库，保留目录结构。
2. 打开仓库的 **Actions** 页面。
3. 选择 **Build DiVERE releases**。
4. 点击 **Run workflow**。
5. 三个构建任务完成后，在该次运行页面底部的 **Artifacts** 下载安装包。

## 发布 Release

推送形如 `v5.6.1` 或 `release-5.6.1` 的标签时，工作流除了生成 Artifacts，还会自动创建一个 **Draft Release（草稿 Release）**，并把三个安装包附加进去。检查无误后手动发布即可。

## 功能检查

构建脚本显式启用并在 CMake 阶段强制检查：

- AI / ONNX Runtime
- OpenCL
- GraphicsMagick、G'MIC、RSVG
- 相机支持与 libgphoto2
- Lua、地图、SDL2、PortMIDI
- JPEG XL、AVIF、HEIF、OpenEXR、OpenJPEG、WebP、XCF
- curve / noise tools
- OpenCL kernels、AI 模型、Lua、图标和主题资源

Windows 包包含 ONNX Runtime、DirectML、相机库、GTK/GDK-Pixbuf/GIO/GPhoto 运行时模块和缓存。macOS 包会递归收集 Homebrew 动态库和运行时模块，并改写为 `@loader_path` / `@rpath` 相对路径，不把 GitHub Runner 或 Homebrew 的绝对路径留在 App 内。

## 签名提示

当前配置使用 macOS 临时签名（ad-hoc），没有使用 Apple Developer 证书；Windows 也没有购买代码签名证书。因此客户首次打开时可能需要右键打开 DMG 里的 App，Windows 可能出现 SmartScreen 提示。若后续有 Apple Developer ID 和 Windows 代码签名证书，可在此工作流中继续接入正式签名和公证。
