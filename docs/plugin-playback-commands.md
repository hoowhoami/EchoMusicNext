# 插件共享播放动作

所有动作通过公开 `EchoPluginViewContext.command(action, arguments)` 或 `ctx.player.control` 返回真实 `EchoPluginOperation`。命令在 GUI 队列执行前校验作用域、账号世代；携带 `revision` 的命令还校验曲目世代。取消、隐藏、退休后没有排队写入。`locateTrack` 自动绑定当前曲目世代。

| 动作               | 参数                                                                 | 语义                                                                                                     |
| ------------------ | -------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `shuffleQueue`     | 无                                                                   | 当前条目置首，其余条目 Fisher–Yates；清空“下一首播放”优先序；保留当前音源、进度和播放模式                |
| `volumePreview`    | `value`: 有限数，0..150，百分比                                      | 当前 scope 的单一输出预览租约，改变真实本地引擎音量和只读 live snapshot，不写偏好                        |
| `volumeCommit`     | `value`: 同上                                                        | 仅持有租约的 scope 可提交；即使等于最后预览值也写一次偏好并释放租约                                      |
| `volumeCancel`     | 无                                                                   | 释放本 scope 的预览，恢复开始时的音量和取消静音回退值；没有本 scope 租约时无副作用                       |
| `sleepTimerStart`  | `value`: 整数 1..59999 分钟；`action`: `pause` / `quit` / `shutdown` | 重启共享一次性睡眠截止时间；乘法及截止时间使用 qint64，拒绝越界/溢出                                     |
| `sleepTimerCancel` | 无                                                                   | 取消尚未执行的共享睡眠定时；实际动作已经执行时返回 `Unavailable`                                         |
| `songWall`         | 无                                                                   | 打开宿主共享真实队列墙，虚拟 GridView 不镜像一份队列；当前条目点击切换暂停，其他条目播放真实 index       |
| `locateTrack`      | 可选 `revision`                                                      | 精确解析当前队列 `queue:playlist:<id>` / `queue:album:<id>`，打开来源歌曲页并居中该 ID；来源不存在则失败 |

源由私人 FM、刷歌或一起听持有时，`shuffleQueue` 返回 `Unavailable`。歌曲墙拒绝私人 FM 或空队列。打乱不切换随机播放模式，不重启当前音源；下一首 URL 预读取新顺序。

音量预览只接受一个 QObject owner。并行 owner、远端投放输出不接受预览。scope 退出、owner 销毁或账号变化恢复未提交值；原生/系统 `setVolume` 接管使旧租约失效，旧 `commit` 失败且不能覆盖新值。公开 `volume` 命令仍是立即持久提交。预览时不能把 snapshot 值另存为设置。

`player.snapshot` 新增 `canShuffleQueue`、`canShowSongWall`、`canLocateTrack`，以及 `sleepTimer: {active, durationMinutes, remainingSeconds, action, executing, errorMessage}`。队列投影跟随实际 active source model，并安全转发 reset/insert/remove/move/dataChanged；旧来源被释放后不保留悬空引用。

来源定位的 `locateTrack`、`locateToken` 是 route 的非缓存键 query，重复请求复用同一个 mounted 页面。`CatalogSongList` 等真实行到达后清除隐藏目标的本地搜索并居中；请求只消费一次，离开页面或换歌撤销。完整来源列表不含目标、读取失败分别显示可见消息；不会改为普通打开队列的成功结果。暂不推断历史、云盘、本地文件等未知来源的集合归属。

睡眠仍沿用宿主现有一次性计时及 action 执行者；不持久化已启用状态。自定义原生面板范围也扩展到 59999 分钟。Folia 编辑器以 0..999 小时和 0..59 分钟显示同一真实计时器，零时长不允许开启。

## 验证

`tests/qt/PluginViewCommandsTest.cpp` 使用真实 Host、scope、账号 epoch、队列和 QSettings。新增行为先在旧正式库上得到 4 项命令缺失红灯；队列跨 source 投影与导航另有独立红灯。私有复制 archive 重编变更对象及新 MOC 后，通过当前 15 项回归；另外对 null/map/list/bytes/bool/string 非数值入参先复现旧实现误提交零音量，再统一 page/control 校验为真实数值 metatype。没有修改正式库。正式目标构建和最终 app 集成由主线统一执行。

插件 `tests/native/qml/tst_PlayerCapsuleActions.qml` 使用按需真实 Host、公开 page、200 条真实 PCM 队列、SongCatalogModel/Actions，检查鼠标预览/提交/退休、999h59m、虚拟墙、加载后中心定位和读取失败。Cocoa/Metal 的 DPR1、DPR2 各 11/11；零时长排队取消、隐藏控件条回滚另有独立红灯。正式包验证结果见插件 `docs/folia/capsule-reference.md`。
