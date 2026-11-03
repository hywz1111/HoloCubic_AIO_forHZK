# 原始 HoloCubic 硬件资料

本目录整合稚晖君 HoloCubic 项目的硬件资料，供 HoloCubic_AIO_forHZK 制作和参考。

来源仓库：https://github.com/hywz1111/HoloCubic
原作者仓库：https://github.com/peng-zhihui/HoloCubic
来源提交：`5e00a57bd6afbf6e22fc25291095082656eaf3a4`

## 文件导航

| 内容 | 位置 | 工具 |
| --- | --- | --- |
| 原理图、PCB、多板装配工程 | [1.Hardware](1.Hardware/) | Altium Designer |
| 外壳与装配模型 | [4.3D Model](4.3D%20Model/) | 支持 STEP/STL 的 CAD；3DM 使用 Rhino |
| 芯片手册、项目图片 | [5.Docs](5.Docs/) | PDF 阅读器、图片查看器 |
| 原项目说明 | [README.original.md](README.original.md) | 含制作、棱镜和固件说明 |
| 来源许可证 | [LICENSE](LICENSE) | 按原项目 GPL-3.0 许可证保留 |
| 文件完整性清单 | [manifest.json](manifest.json) | 原始路径、字节数及 Git blob SHA-1 |

## 版本适配

- Naive PCB 对应原始 Naive、Bilibili、Metal 外壳方案；Ironman PCB 对应 Ironman 外壳。
- 这些是原项目资料，不是 AIO 后续改版的主板 v2.0、屏幕板 v2.1 或 DCDC 设计。
- AIO 改版硬件请对照仓库已有的[硬件版本说明](../../硬件版本说明.md)，不要把其版本号套用到本目录。
- 下单前核对主板、屏幕板、外壳尺寸和连接方式；AIO 的联网及视频负载还需核对供电能力。
- 屏幕为 ST7789 240×240，原方案主控为 ESP32-PICO-D4，姿态输入为 MPU6050。
- 当前 forHZK 的 SD 初始化已显式指定 GPIO26；原说明中修改全局 SPI 库的旧步骤不应直接照搬。

导入保留原始目录及设计文件，不修改 forHZK 固件。README.original.md 仅把原说明中的图片路径改为相对路径，方便在本目录浏览。
