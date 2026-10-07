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
<img width="283" height="284" alt="aef76dac9c058220f1e283da54cc2e32" src="https://github.com/user-attachments/assets/c64e6382-15ce-4f97-a0e7-9629c4b9ce21" /><img width="283" height="283" alt="微信图片_20261007193323_261_121" src="https://github.com/user-attachments/assets/298c6fb4-295a-4fb4-b22e-11368e07626c" />

## 许可与数据来源

- **代码**：**GPL-3.0**（darktable 及其衍生，见 `LICENSE`）
- **DiVERE 算法 / 相纸曲线 / 白平衡模型**：**MIT**（<https://github.com/V7CN/DiVERE>，版权声明保留于 `negadoctor_curves.h` / `negadoctor.c` 头部）

