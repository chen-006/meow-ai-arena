# 第三方依赖 / Third-party notices

渲染器不打包下面任何一项，使用者自己安装，按各自的许可使用。
The renderer bundles none of the following; install them yourself and use them under their own licenses.

| 名称 / Name | 测试过的版本 / Tested version | 用途 / Used for | 许可 / License | 出处 / Source |
|---|---|---|---|---|
| Blender | 5.2.4 LTS | 渲染（EEVEE），以独立程序方式调用 / rendering (EEVEE), run as a separate program | GPL-2.0-or-later | https://www.blender.org/ |
| ffmpeg | 7.x | 合成 mp4 / encoding the mp4 | LGPL-2.1-or-later / GPL（视构建而定 / depending on the build） | https://ffmpeg.org/ |
| NumPy | 随 Blender 和本机 Python / bundled with Blender and the local Python | 回放读取、几何 / replay parsing, geometry | BSD-3-Clause | https://numpy.org/ |
| SciPy | 本机 Python 装的版本 / local Python | 地图预处理（距离场、平滑）/ map preprocessing | BSD-3-Clause | https://scipy.org/ |
| scikit-image | 本机 Python 装的版本 / local Python | 地形网格（marching cubes）/ terrain mesh | BSD-3-Clause | https://scikit-image.org/ |
| Pillow | 本机 Python 装的版本 / local Python | 信息栏、地面贴图 / HUD, ground textures | MIT-CMU (HPND) | https://python-pillow.org/ |
| Noto Sans SC（可选 / optional） | 系统字体 / system font | 信息栏文字 / HUD text | SIL OFL 1.1 | https://fonts.google.com/noto |
