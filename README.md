# Darktable—DiVERE  By ：银盐菲林日记

基于Darkable：https://github.com/darktable-org/darktable
   DiVERE：https://github.com/V7CN/DiVERE

## 改动内容

集中在胶片数字化（负片反减）链路：

- `src/iop/negadoctor.c` + `negadoctor_curves.h`：集成 DiVERE 负片反相管线——密度反相（pivot=0.7）、**分层反差**（PIVOT 折点）、通道 gamma（含**绿通道独立滑块**）、Status M→打印密度矩阵、相纸特性曲线（Kodak 2383/2393/Endura 系列、Ilford MGFB 0–5）
- `src/iop/colorin.c` + `data/color/json/`：IDT / 工作色彩空间 JSON 加载（Kodak2383、KodakEnduraPremier），D60 白点
- `data/negadoctor_ai/`：DiVERE Deep White Balance（`net_awb.onnx`）自动白平衡模型
- macOS 打包脚本：`packaging/macosx/3_make_modified_local.sh`

## 许可与数据来源

- **代码**：**GPL-3.0**（darktable 及其衍生，见 `LICENSE`）
- **diVERE 算法 / 相纸曲线 / 白平衡模型**：**MIT**（<https://github.com/V7CN/DiVERE>，版权声明保留于 `negadoctor_curves.h` / `negadoctor.c` 头部）

> 提示：源码头部版权声明（GPL/MIT）保留不动；`LICENSE` 请勿删除。
