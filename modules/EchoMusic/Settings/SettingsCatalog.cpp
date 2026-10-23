#include "SettingsCatalog.h"
#include <QRegularExpression>
namespace EchoMusic
{
namespace
{
QString normalized(QString value)
{
    return value.toCaseFolded().remove(QRegularExpression("\\s+"));
}
} // namespace
SettingsCatalog::SettingsCatalog(QObject *parent) : QAbstractListModel(parent)
{
    // Sort settings by explicit section order; lyric subsections share one category.
    auto interfaceKeywords =
        tr("深浅色模式 跟随系统 浅色 深色 界面缩放 放大 缩小 搜索框默认推荐词 专辑动态封面");
#if !defined(Q_OS_MACOS)
    interfaceKeywords += tr(" 全屏按钮 标题栏");
#endif
    m_entries = {
        {"interface", tr("界面与外观"), "eye", interfaceKeywords, "InterfaceSettingsSection"},
        {"window", tr("窗口与系统"), "settings",
         tr("窗口行为 记住窗口大小 窗口位置 关闭行为 最小化 托盘 开机自启动"),
         "WindowSettingsSection"},
        {"font", tr("字体设置"), "typography",
         tr("全局字体 播放页歌词字体 桌面歌词字体 Mini 歌词字体 任务栏歌词字体 系统默认 跟随全局"),
         "FontSettingsSection"},
        {"playback", tr("播放体验"), "player-play",
         tr("音质 播放 音量 倍速 歌曲 收藏 过渡 淡入 音量均衡 LUFS 自动跳过错误 失败后切换延迟 "
            "最大自动切换次数 防止系统休眠 智能兼容模式 MV 音频兜底"),
         "PlaybackSettingsSection"},
        {"player", tr("播放引擎"), "cpu",
         tr("普通预读时长 网络缓存时长上限 网络缓存模式 缓存耗尽时暂停 缓存恢复等待 前向缓存上限 "
            "回退缓存上限 音频设备缓冲 音频采样率 音频声道 输出采样格式 无缝播放 播放恢复超时 "
            "播放卡死自动恢复 最大自动恢复次数"),
         "PlayerSettingsSection"},
        {"spatialAudio", tr("音效管理"), "adjustments", tr("蝰蛇 音效 DSP 卷积 IRS WAV"),
         "AudioEffectSettingsSection"},
        {"lyric", tr("歌词设置"), "music-share",
         tr("播放页 桌面歌词 Mini 歌词 翻译 音译 校准 字体 颜色"), "LyricSettingsSection"},
        {"shortcuts", tr("快捷键"), "keyboard", tr("全局快捷键 播放 暂停 上一首 下一首 恢复默认"),
         "ShortcutSettingsSection"},
        {"audioDevice", tr("音频设备"), "device-speaker", tr("输入 输出设备 麦克风 系统默认 独占"),
         "AudioDeviceSettingsSection"},
        {"cast", tr("投放"), "cast", tr("启用投放 当前输出 DLNA AirPlay 可用设备 清除设备记录"),
         "CastSettingsSection"},
        {"plugins", tr("插件"), "puzzle",
         tr("插件管理 管理面板 在线插件源 已安装插件 插件设置 插件文档"), "PluginSettingsSection"},
        {"data", tr("数据与安全"), "shield",
         tr("备份与恢复 创建备份 恢复备份 插件与插件数据 查看运行日志 清除应用数据 缓存 安全"),
         "DataSettingsSection"},
        {"network", tr("网络设置"), "cloud",
         tr("全局网络代理 系统代理 手动代理 自动检测 WPAD PAC 脚本 proxyRules SOCKS "
            "代理认证 代理用户名 代理密码 不代理地址 bypass GitHub 加速站 GitHub 加速地址 "
            "在线插件源 插件下载 酷狗 API 超时 播放器网络超时 应用网络设置 代理 超时"),
         "NetworkSettingsSection"},
        {"experimental", tr("实验性功能"), "flask",
         tr("页面缓存 最大缓存页面数 日志级别 API 响应体日志 临时诊断日志 诊断并重启 开启启动诊断 "
            "用户信息 鉴权头 用户 ID token t1 guid mid dfid uuid dev mac 重置设备身份 GPU DPI"),
         "ExperimentalSettingsSection"},
        {"about", tr("关于"), "info-circle",
         tr("自动检查更新 检查预发布版本 静默安装 当前版本 更新日志 免责声明 项目源码"),
         "AboutSettingsSection"}};
    for (int i = 0; i < m_entries.size(); ++i)
        m_visible.append(i);
}
int SettingsCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QVariant SettingsCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto &entry = m_entries.at(m_visible.at(index.row()));
    switch (role) {
    case Key:
        return entry.key;
    case Label:
        return entry.label;
    case Glyph:
        return entry.glyph;
    case Available:
        return available(entry);
    default:
        return {};
    }
}
QHash<int, QByteArray> SettingsCatalog::roleNames() const
{
    return {{Key, "sectionKey"},
            {Label, "sectionLabel"},
            {Glyph, "sectionGlyph"},
            {Available, "sectionAvailable"}};
}
QUrl SettingsCatalog::source() const
{
    for (const auto &entry : m_entries)
        if (entry.key == m_current && available(entry))
            return QUrl("qrc:/qt/qml/EchoMusic/Settings/" + entry.component + ".qml");
    return {};
}
bool SettingsCatalog::available(const Entry &entry) const
{
    const bool playbackSection = entry.key == "lyric" || entry.key == "audioDevice" ||
                                 entry.key == "spatialAudio" || entry.key == "playback" ||
                                 entry.key == "player" || entry.key == "cast";
    return !entry.component.isEmpty() && (!playbackSection || m_playbackAvailable);
}
void SettingsCatalog::setPlaybackAvailable(bool value)
{
    if (m_playbackAvailable == value)
        return;
    m_playbackAvailable = value;
    const auto filter = m_filter;
    m_filter = QStringLiteral("\x01");
    setFilter(filter);
}
void SettingsCatalog::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    beginResetModel();
    m_filter = filter;
    m_visible.clear();
    const auto query = normalized(filter);
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &entry = m_entries.at(i);
        if ((query.isEmpty() || available(entry)) &&
            normalized(entry.key + entry.label + entry.keywords).contains(query))
            m_visible.append(i);
    }
    m_current.clear();
    for (int index : m_visible) {
        if (available(m_entries.at(index))) {
            m_current = m_entries.at(index).key;
            break;
        }
    }
    endResetModel();
    emit changed();
}
void SettingsCatalog::select(const QString &key)
{
    for (int index : m_visible) {
        const auto &entry = m_entries.at(index);
        if (entry.key == key && available(entry) && m_current != key) {
            m_current = key;
            emit changed();
            return;
        }
    }
}
void SettingsCatalog::reset()
{
    setFilter({});
    select("interface");
}
} // namespace EchoMusic
