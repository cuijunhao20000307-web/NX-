# NX标题工坊

Switch 本机游戏名称 / 厂商 / 版本 / 图标覆盖工具。

## 0.2.2

- 应用名称：NX标题工坊
- 作者名：LINKO
- A：Switch 本机修改游戏名称、作者、显示版本
- Y：手机网页编辑 / 上传图标
- 没有修改的字段保持原样

## 工作方式

本项目 **不修改已安装游戏的 NSP/NCA**，只在 SD 卡写入 sys-ticon 使用的覆盖文件：

`sdmc:/atmosphere/contents/<TITLE_ID>/`

会生成：

- `config.ini`：名称、厂商、显示版本
- `icon.jpg`：256×256 baseline JPG，目标不超过 100 KiB
- `icon174.jpg`：174×174 baseline JPG，目标不超过 64 KiB

原游戏内容保持不变。首次修改前会把已有的 sys-ticon 覆盖备份到 `sdmc:/switch/NXTitleStudio/backups/<TITLE_ID>/`，X 键或手机网页恢复时会还原旧覆盖。
