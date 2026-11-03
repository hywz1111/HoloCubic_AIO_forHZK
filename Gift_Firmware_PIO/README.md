# 晗歌 / はづき · 静音礼物固件

独立于原 AIO 菜单的 ESP32-PICO-D4 固件。接电后直接轮播 TF 卡中的静音 MV；无卡、无视频或遇到无法播放的文件时显示内置的「晗歌 / はづき」橙色画面。无需联网、账号、陀螺仪校准或操作菜单。

当前交付重点为播放器、手机管理和硬件适配。Pingu 的朴素形象及开机动画后续制作，当前没有内置 Pingu 动画、照片或正式 MV 素材。

## 给制作方

### 硬件

按 [HARDWARE.md](HARDWARE.md) 选择配套的原版 HoloCubic 主板、屏幕板和外壳。静音方案无需增加功放或扬声器。屏幕为 ST7789 240×240，TF 卡使用 FAT32。已有完整机器可先检查引脚兼容性，不必重新做板。

### 编译与首次烧录

安装 VS Code、PlatformIO，克隆整个仓库后打开本目录。需要相邻的 `../AIO_Firmware_PIO/lib`，不要只下载本目录。

```sh
pio run -d Gift_Firmware_PIO
pio run -d Gift_Firmware_PIO -t upload --upload-port COM7
pio device monitor -b 115200 --port COM7
```

以上命令在仓库根目录执行；在本目录执行时去掉 `-d Gift_Firmware_PIO`。COM7 替换为实际串口。已锁定 ESP32 平台 3.5.0，使用仓库的 TFT_eSPI（含棱镜镜像修改）、TJpg_Decoder、FastLED、LittleFS 等依赖。不要更新成不兼容的新版本。

本固件使用独立分区表和 NVS 命名空间，不自动擦除或格式化文件系统。由旧 AIO 切换过来首次需要连同分区和启动文件完整烧录，优先使用 PlatformIO Upload；只替换 firmware.bin 不够。旧 AIO 的 SPIFFS 设置不会迁移；恢复旧 AIO 时也需重新烧录它的配套分区。TF 卡内容不会由烧录流程主动删除，但制作前仍应备份。

GitHub Actions 会编译固件、执行媒体核心测试，并提供 `hazuki-gift-flash` 烧录包，内含各二进制文件和地址说明。该编译验证不能代替实机验收。

### 视频准备

制作方需要 Python 3.8+ 和 FFmpeg。原始 MP4/MOV **不能直接放进机器播放**；本版本接收连续 baseline JPEG 帧的 `.mjpeg` 文件，不支持 AVI、H.264、HEVC、音轨或在线 MV 链接。

```sh
python Gift_Firmware_PIO/tools/convert_mv.py input.mp4 01_stage.mjpeg
python Gift_Firmware_PIO/tools/convert_mv.py input.mp4 02_stage.mjpeg --fit crop --start 60 --duration 60
python Gift_Firmware_PIO/tools/convert_mv.py 01_stage.mjpeg --check
```

- 默认 240×240、12 fps、静音，保留完整画面并补黑边；`--fit crop` 居中裁剪填满画面，需检查是否裁掉脸部。
- 每帧最多 48 KiB，每段最多 128 MiB，最多读取 32 段；超限时工具拒绝生成最终文件。长视频用 `--start` / `--duration` 分段。
- 文件名使用英文字母、数字、下划线或短横线，主体 1～42 字符，后缀小写 `.mjpeg`。建议 `01_stage.mjpeg`、`02_stage.mjpeg`，按文件名排序轮播。
- 播放帧率通过手机页设置；原始 MJPEG 不含时间戳，须与转换时 fps 一致。12 fps 是起始目标，实机若达不到会变慢；验证后可改为 8/10 fps 并重新转换素材。
- 关机后把文件复制到 TF 卡的 `/mv/` 目录。也可在手机管理页面上传已经转换好的文件；手机网页不转码 MP4。
- 上传先写临时文件，通过尺寸、帧边界检查后才加入播放列表；不能覆盖同名素材。异常帧在播放时再次解码检查，连续异常的文件跳过。掉电不会替换已有的视频，但 FAT32 本身没有断电事务保障，写入中仍应避免断电。

### 自检与验证

```sh
g++ -std=c++11 -Wall -Wextra -Werror -I Gift_Firmware_PIO/include Gift_Firmware_PIO/test/media_core_test.cpp -o /tmp/hazuki-media-test
/tmp/hazuki-media-test
```

交付前按 [HARDWARE.md 的验收清单](HARDWARE.md#实机验收) 完成显示、断电、异常素材、手机上传和长时间温升测试。当前源码只验证编译和可在主机执行的逻辑，不宣称已完成实机验收。

## 给收礼人

接上电源即可欣赏，播放时没有声音。拿起或移动设备不会切换功能。

想调亮度、暂停、切换或添加随礼提供的视频：

1. 手机连接 `Hazuki-XXXX` 开头的 Wi-Fi，默认密码为 `hazuki-orange`（交付时以随礼小卡为准）。
2. 手机提示「没有互联网」时选择保持连接。
3. 浏览器打开 **http://192.168.4.1**，按页面操作。

不需要连接家里的路由器。亮度设置和手动选中的视频会保存；重启后从所选视频的开头开始，不保存精确播放秒数。暂停状态在重启后恢复为播放。变更安装方向会重新开始显示，播放速度需与素材一致。

上传时请保持连接，不要拔电。取出或更换内存卡前请断电。没有卡也能展示内置名字和橙色光点。

## 实现范围

- 手机管理在播放循环中持续服务；每次限制读卡量，视频帧缓冲固定 48 KiB，不依赖 PSRAM。
- 默认只开放本机热点，不连接外部网络，无遥测、云端服务、自动更新或远程上传功能。
- 同一时间上传一个文件，上传时暂停视频，显示备用画面，完成后重建播放列表。
- 设置限幅、文件名白名单、请求标记和临时文件避免误操作；热点密码可在制作时通过 `HAZUKI_AP_PASSWORD` 编译宏替换（8～63 个 ASCII 字符）。默认密码公开于本仓库，送出前宜换成独立密码并印在小卡上。
- 备用文字已生成为小尺寸位图，无需外置中文字库；字体来源为 Noto Sans CJK，见 [FONT_LICENSE.txt](FONT_LICENSE.txt)。应用代码沿用仓库 GPL-3.0，驱动保留原作者许可与署名。复用了稚晖君原始 HoloCubic 的硬件方案以及 ClimbSnail / forHZK 项目的驱动配置。

后续再制作 Pingu 开场与正式主题素材，不影响当前硬件和播放器结构。
