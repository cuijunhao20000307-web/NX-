# NXTitleStudio

Switch 本机游戏名称 / 厂商 / 版本 / 图标覆盖工具。

## 工作方式

本项目 **不修改已安装游戏的 NSP/NCA**，只在 SD 卡写入 sys-ticon 使用的覆盖文件：

`sdmc:/atmosphere/contents/<TITLE_ID>/`

会生成：

- `config.ini`：名称、厂商、显示版本
- `icon.jpg`：256×256 baseline JPG，目标不超过 100 KiB
- `icon174.jpg`：174×174 baseline JPG，目标不超过 64 KiB

原游戏内容保持不变。首次修改前会把已有的 sys-ticon 覆盖备份到 `sdmc:/switch/NXTitleStudio/backups/<TITLE_ID>/`，X 键或手机网页恢复时会还原旧覆盖。

## Switch 操作

1. 安装 Atmosphère。
2. 安装并启用 `sys-ticon`。
3. 把 `NXTitleStudio.nro` 放到 `sdmc:/switch/NXTitleStudio/NXTitleStudio.nro`。
4. Switch 和手机连接同一个 Wi‑Fi。
5. 打开 HBMenu → NXTitleStudio。
6. 方向键选游戏。
7. 按 **A**，Switch 屏幕显示二维码。
8. 手机扫码打开局域网页。
9. 输入名称/厂商/版本，选择 PNG/JPG 图片，点 Apply。
10. 重启 Switch 后 HOME 菜单缓存会刷新。

## 恢复

- 在游戏列表按 **X**；或
- 手机页面点 `Restore original`。

这只删除本工具/sys-ticon 的覆盖，不删除游戏。

## 编译

需要 devkitPro / devkitA64 / libnx。

```bash
./fetch_deps.sh
make
```

输出：`NXTitleStudio.nro`

## 依赖

- libnx
- Nayuki QR Code generator
- stb_image / stb_image_write
