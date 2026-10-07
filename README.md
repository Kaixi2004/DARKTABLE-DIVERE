# Darktable—DiVERE  By ：银盐菲林日记

基于Darkable：https://github.com/darktable-org/darktable
   DiVERE：https://github.com/V7CN/DiVERE

## 改动内容

集中在胶片数字化（负片反减）链路：

- `src/iop/negadoctor.c` + `negadoctor_curves.h`：集成 DiVERE 负片反相管线——密度反相（pivot=0.7）、**分层反差**（PIVOT 折点）、通道 gamma（含**绿通道独立滑块**）、Status M→打印密度矩阵、相纸特性曲线（Kodak 2383/2393/Endura 系列、Ilford MGFB 0–5）
- `src/iop/colorin.c` + `data/color/json/`：IDT / 工作色彩空间 JSON 加载（Kodak2383、KodakEnduraPremier），D60 白点
- `data/negadoctor_ai/`：DiVERE Deep White Balance（`net_awb.onnx`）自动白平衡模型

## 支持作者
如果这个工具对您的胶片摄影工作有切实帮助，欢迎请作者喝杯饮料或买一卷胶片！您的支持是开源项目持续发展的动力 😊
<img width="554" height="592" alt="image" src="https://github.com/user-attachments/assets/13a80be6-777b-42cc-9c82-da5f9aa196f3" /> <img width="1708" height="2560" alt="image" src="https://github.com/user-attachments/assets/1106a470-c10d-4834-ad91-9878ad7a4c72" />

## 许可与数据来源

- **代码**：**GPL-3.0**（darktable 及其衍生，见 `LICENSE`）
- **diVERE 算法 / 相纸曲线 / 白平衡模型**：**MIT**（<https://github.com/V7CN/DiVERE>，版权声明保留于 `negadoctor_curves.h` / `negadoctor.c` 头部）

> 提示：源码头部版权声明（GPL/MIT）保留不动；`LICENSE` 请勿删除。
