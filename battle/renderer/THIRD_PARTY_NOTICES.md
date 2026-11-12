# 第三方依赖 / Third-party notices

渲染器不打包下面任何一项，使用者自己安装，按各自的许可使用。
The renderer bundles none of the following; install them yourself and use them under their own licenses.

| 名称 / Name | 测试过的版本 / Tested version | 用途 / Used for | 许可 / License | 出处 / Source |
|---|---|---|---|---|
| Blender | 5.2.4 LTS | 渲染（EEVEE），以独立程序方式调用 / rendering (EEVEE), run as a separate program | GPL-2.0-or-later | https://www.blender.org/ |
| ffmpeg | 7.1 | 合成 mp4 / encoding the mp4 | LGPL-2.1-or-later / GPL（视构建而定 / depending on the build） | https://ffmpeg.org/ |
| NumPy | 随 Blender 和本机 Python / bundled with Blender and the local Python | 回放读取、机位计算 / replay parsing, camera maths | BSD-3-Clause | https://numpy.org/ |
| Pillow | 本机 Python 装的版本 / whatever the local Python has | 信息栏 / HUD drawing | MIT-CMU (HPND) | https://python-pillow.org/ |
| Noto Sans SC（可选 / optional） | 系统字体 / system font | 信息栏文字 / HUD text | SIL OFL 1.1 | https://fonts.google.com/noto |

模拟器依赖的 Jolt Physics（MIT）见 [engine/README.md](../engine/README.md)。
The simulator's dependency Jolt Physics (MIT) is described in [engine/README_en.md](../engine/README_en.md).
