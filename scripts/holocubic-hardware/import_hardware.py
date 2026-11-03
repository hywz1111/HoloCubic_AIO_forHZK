#!/usr/bin/env python3
"""Import pinned original HoloCubic hardware into a local forHZK checkout."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request

SOURCE = "hywz1111/HoloCubic"
REVISION = "5e00a57bd6afbf6e22fc25291095082656eaf3a4"
PREFIXES = ("1.Hardware/", "4.3D Model/", "5.Docs/")
DESTINATION = "Hardware/Original_HoloCubic"

GUIDE = '''# 原始 HoloCubic 硬件资料

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
'''


def build_files(archive):
    files = {}
    manifest = []
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for member in tar.getmembers():
            if not member.isfile():
                continue
            parts = member.name.split("/", 1)
            if len(parts) != 2:
                continue
            path = parts[1]
            if not (path.startswith(PREFIXES) or path in ("README.md", "LICENSE")):
                continue
            if any(part in ("..", "") for part in path.split("/")):
                raise ValueError("Unsafe archive path")
            data = tar.extractfile(member).read()
            digest = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            manifest.append({"path": path, "size": len(data), "git_blob_sha1": digest})
            if path == "README.md":
                path = "README.original.md"
                data = data.replace(b"(/5.Docs/", b"(./5.Docs/")
            files[path] = data
    if not all(any(p.startswith(prefix) for p in files) for prefix in PREFIXES):
        raise ValueError("Archive missing expected hardware directories")
    expected_path = Path(__file__).with_name("source-manifest.json")
    if not expected_path.is_file():
        raise ValueError("Missing source-manifest.json; keep it next to this script")
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    if expected["commit"] != REVISION or expected["repository"] != SOURCE:
        raise ValueError("Source manifest does not match pinned revision")
    actual_map = {item["path"]: item for item in manifest}
    expected_map = {item["path"]: item for item in expected["files"]}
    if actual_map != expected_map:
        raise ValueError("Source archive does not match verified GitHub file hashes")
    files["README.md"] = GUIDE.encode()
    files["manifest.json"] = (json.dumps({"repository": SOURCE, "commit": REVISION,
        "files": sorted(manifest, key=lambda x: x["path"])}, ensure_ascii=False, indent=2) + "\n").encode()
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path, help="Local HoloCubic_AIO_forHZK checkout")
    parser.add_argument("--archive", type=Path, help="Optional previously downloaded source tar.gz")
    args = parser.parse_args()
    root = args.repository.resolve()
    if not (root / "AIO_Firmware_PIO/platformio.ini").is_file():
        parser.error("Target does not appear to be a forHZK checkout")
    if subprocess.check_output(["git", "-C", str(root), "status", "--porcelain"]):
        parser.error("Target checkout must be clean to avoid mixing existing changes")
    destination = root / DESTINATION
    if destination.exists():
        parser.error("Destination already exists; refusing to overwrite")
    if args.archive:
        archive = args.archive.read_bytes()
    else:
        request = urllib.request.Request(
            f"https://codeload.github.com/{SOURCE}/tar.gz/{REVISION}",
            headers={"User-Agent": "HoloCubic-hardware-import"})
        with urllib.request.urlopen(request, timeout=60) as response:
            archive = response.read()
    files = build_files(archive)
    readme = root / "README.md"
    text = readme.read_text(encoding="utf-8")
    section = ("\n## 硬件设计与制作资料\n\n"
               "原始 HoloCubic 的原理图、PCB、外壳模型及芯片手册已整合到 "
               "[硬件资料目录](Hardware/Original_HoloCubic/README.md)。"
               "请先查看其中的版本适配说明，再选用设计文件制作。\n")
    for path, data in files.items():
        target = destination / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        if target.read_bytes() != data:
            raise IOError(f"Verification failed: {path}")
    readme.write_text(text.rstrip() + "\n" + section, encoding="utf-8")
    print(f"Imported {len(files)} files into {destination}")
    print("Files verified. Review with git diff and git status before committing.")


if __name__ == "__main__":
    main()
