#include "AppIconService.h"
#include "AppIconShortcuts_p.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QWindow>
#include <QPixmap>
#include <QImageReader>
#include <QBuffer>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
namespace EchoMusic
{
#ifdef Q_OS_WIN
namespace
{
// 运行中任务栏按钮的实时图标：QWindow::setIcon 的 WM_SETICON 不总能让
// AUMID 固定按钮刷新，与 Electron 的 BrowserWindow.setIcon 一致地显式
// 发送两种尺寸。HICON 由本表持有，替换时销毁上一份。
QHash<QWindow *, QPair<HICON, HICON>> g_runtimeWindowIcons;

HICON alphaHIconFromImage(const QImage &source, int size)
{
    const QImage scaled = source.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    BITMAPV5HEADER header = {};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = scaled.width();
    header.bV5Height = scaled.height();
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    void *bits = nullptr;
    const auto screenDc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screenDc, reinterpret_cast<const BITMAPINFO *>(&header),
                                     DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (!color || !bits) {
        if (color)
            DeleteObject(color);
        return nullptr;
    }
    // DIB 自下而上，QImage 自上而下；ARGB32_Premultiplied 的内存布局即
    // 小端 BGRA，与上面的掩码一致。
    for (int y = 0; y < scaled.height(); ++y)
        memcpy(static_cast<char *>(bits) + y * scaled.width() * 4,
               scaled.constScanLine(scaled.height() - 1 - y), size_t(scaled.width()) * 4);
    // 1bpp AND 掩码每行按 WORD 对齐；DPI 缩放的图标尺寸（如 125% 下的
    // SM_CXSMICON=20）不是 16 的倍数，按整图字节数分配会越界读。
    const DWORD maskStride = ((DWORD(scaled.width()) + 15) / 16) * 2;
    const QByteArray maskBytes(QByteArray(size_t(maskStride) * size_t(scaled.height()), char(0)));
    HBITMAP mask = CreateBitmap(scaled.width(), scaled.height(), 1, 1, maskBytes.constData());
    ICONINFO info = {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void sendRuntimeWindowIcon(QWindow *window, const QImage &image)
{
    if (!window->handle())
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    auto &slot = g_runtimeWindowIcons[window];
    HICON hBig = alphaHIconFromImage(image, GetSystemMetrics(SM_CXICON));
    HICON hSmall = alphaHIconFromImage(image, GetSystemMetrics(SM_CXSMICON));
    SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSmall));
    if (slot.first)
        DeleteObject(slot.first);
    if (slot.second)
        DeleteObject(slot.second);
    slot = {hBig, hSmall};
}

void releaseRuntimeWindowIcons(QWindow *window)
{
    // 仅释放句柄：窗口销毁回调里不得再触碰 QWindow 本体。
    const auto slot = g_runtimeWindowIcons.take(window);
    if (slot.first)
        DeleteObject(slot.first);
    if (slot.second)
        DeleteObject(slot.second);
}

void clearRuntimeWindowIcon(QWindow *window, const QIcon &fallback)
{
    window->setIcon(fallback);
    releaseRuntimeWindowIcons(window);
}
} // namespace
#endif
struct AppIconService::ShortcutState
{
    AppIconShortcuts store;
};
AppIconService::AppIconService(QObject *parent)
    : QObject(parent), m_default(QGuiApplication::windowIcon()),
      m_shortcuts(std::make_shared<ShortcutState>())
{
    m_worker.setMaxThreadCount(1);
    m_worker.setExpiryTimeout(1000);
}
AppIconService::~AppIconService()
{
    m_worker.waitForDone();
}
bool AppIconService::supported(const QString &target)
{
    if (target == "window" || target == "tray")
        return true;
#ifdef Q_OS_WIN
    return target == "desktop" || target == "taskbar";
#elif defined(Q_OS_LINUX)
    return target == "desktop";
#else
    return false;
#endif
}
void AppIconService::prepare(const QHash<QString, QString> &paths,
                             std::function<void(PreparedAppIcons)> done,
                             const QHash<QString, QByteArray> &data)
{
    if (m_pending >= 16) {
        PreparedAppIcons result;
        result.error = "Too many icon operations";
        done(result);
        return;
    }
    ++m_pending;
    emit drainedChanged();
    auto *watch = new QFutureWatcher<PreparedAppIcons>(this);
    connect(watch, &QFutureWatcher<PreparedAppIcons>::finished, this, [this, watch, done] {
        const auto result = watch->result();
        watch->deleteLater();
        --m_pending;
        done(result);
        emit drainedChanged();
    });
    QSet<QString> keep;
    for (const auto &entry : m_entries)
        for (const auto &path : entry.icons.paths)
            keep.insert(path);
    for (const auto &path : m_desiredShortcuts)
        keep.insert(path);
    for (const auto &path : m_submittedShortcuts)
        keep.insert(path);
    const auto cache =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/plugin-icons";
    watch->setFuture(QtConcurrent::run(&m_worker, [paths, data, cache, keep] {
        PreparedAppIcons result;
        auto keys = paths.keys();
        for (const auto &key : data.keys())
            if (!keys.contains(key))
                keys.append(key);
        for (const auto &key : keys) {
            if (!AppIconService::supported(key)) {
                result.error = "Unsupported icon target";
                break;
            }
            auto bytes = data.value(key);
            if (paths.contains(key)) {
                const QFileInfo info(paths.value(key));
                if (!info.isFile() || info.size() <= 0 || info.size() > 4 * 1024 * 1024) {
                    result.error = "Icon must be an image up to 4 MiB";
                    break;
                }
                QFile input(paths.value(key));
                if (!input.open(QIODevice::ReadOnly)) {
                    result.error = "Cannot read icon";
                    break;
                }
                bytes = input.readAll();
            }
            if (bytes.isEmpty() || bytes.size() > 4 * 1024 * 1024) {
                result.error = "Icon bytes exceed image limit";
                break;
            }
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            const auto size = reader.size();
            if (!size.isValid() || size.width() > 2048 || size.height() > 2048) {
                result.error = "Icon dimensions exceed 2048";
                break;
            }
            reader.setScaledSize(size.scaled(256, 256, Qt::KeepAspectRatio));
            auto image = reader.read();
            if (image.isNull()) {
                result.error = "Cannot decode icon image";
                break;
            }
            result.images[key] = image;
            if (key == "desktop" || key == "taskbar") {
#ifdef Q_OS_WIN
                const QByteArray format = "ico";
#else
                const QByteArray format = "png";
#endif
                const auto name = QString::fromLatin1(
                    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
                const auto path = cache + '/' + name + '.' + QString::fromLatin1(format);
                QDir().mkpath(cache);
                QSaveFile output(path);
                if (!output.open(QIODevice::WriteOnly) ||
                    !image.save(&output, format.constData()) || !output.commit()) {
                    result.error = "Cannot cache shortcut icon";
                    break;
                }
                result.paths[key] = path;
            }
        }
        // Rebuildable image cache, bounded independently of source packages.
        // Keep current/pending shortcuts and a small newest preparation window.
        const auto entries = QDir(cache).entryInfoList({"*.png", "*.ico"},
                                                       QDir::Files | QDir::NoSymLinks, QDir::Time);
        qint64 bytes = 0;
        int count = entries.size();
        for (const auto &file : entries)
            bytes += file.size();
        for (qsizetype index = entries.size(); index-- > 64;) {
            const auto &file = entries[index];
            if (bytes <= 64 * 1024 * 1024 && count <= 256)
                break;
            if (keep.contains(file.absoluteFilePath()) ||
                result.paths.values().contains(file.absoluteFilePath()))
                continue;
            if (!QRegularExpression("^[0-9a-f]{64}\\.(png|ico)$").match(file.fileName()).hasMatch())
                continue;
            if (QFile::remove(file.absoluteFilePath())) {
                bytes -= file.size();
                --count;
            }
        }
        return result;
    }));
}
bool AppIconService::put(const QString &token, PreparedAppIcons icons, int priority,
                         QVariantMap owner)
{
    if (!m_entries.contains(token) && m_entries.size() >= 128)
        return false;
    qint64 bytes = 0;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        if (it.key() != token)
            for (const auto &image : it->icons.images)
                bytes += image.sizeInBytes();
    for (const auto &image : icons.images)
        bytes += image.sizeInBytes();
    if (bytes > 32 * 1024 * 1024)
        return false;
    m_entries.insert(token, {std::move(icons), std::move(owner), priority, ++m_order});
    apply();
    return true;
}
void AppIconService::remove(const QString &token)
{
    if (m_entries.remove(token))
        apply();
}
void AppIconService::removeTarget(const QString &token, const QString &target)
{
    auto it = m_entries.find(token);
    if (it == m_entries.end())
        return;
    it->icons.images.remove(target);
    it->icons.paths.remove(target);
    if (it->icons.images.isEmpty())
        m_entries.erase(it);
    apply();
}
bool AppIconService::hasTarget(const QString &target) const
{
    for (const auto &entry : m_entries)
        if (entry.icons.images.contains(target))
            return true;
    return false;
}
void AppIconService::attach(QWindow *window)
{
    if (!window || m_windows.contains(window))
        return;
    m_windows[window] = window->icon().cacheKey() == QGuiApplication::windowIcon().cacheKey()
                            ? m_default
                            : window->icon();
    connect(window, &QObject::destroyed, this, [this, window] {
#ifdef Q_OS_WIN
        // 窗口没了：只释放我们持有的 HICON，不得触碰 QWindow 本体。
        releaseRuntimeWindowIcons(window);
#endif
        m_windows.remove(window);
    });
    const Entry *winner = nullptr;
    for (const auto &entry : m_entries)
        if (entry.icons.images.contains("window") &&
            (!winner || entry.priority > winner->priority ||
             (entry.priority == winner->priority && entry.order > winner->order)))
            winner = &entry;
    if (winner)
        window->setIcon(QIcon(QPixmap::fromImage(winner->icons.images.value("window"))));
}
QIcon AppIconService::trayIcon(const QIcon &fallback) const
{
    const Entry *winner = nullptr;
    for (const auto &entry : m_entries)
        if (entry.icons.images.contains("tray") &&
            (!winner || entry.priority > winner->priority ||
             (entry.priority == winner->priority && entry.order > winner->order)))
            winner = &entry;
    if (!winner)
        return fallback;
    QIcon icon(QPixmap::fromImage(winner->icons.images.value("tray")));
#ifdef Q_OS_MACOS
    icon.setIsMask(true);
#endif
    return icon;
}
void AppIconService::apply()
{
    const auto generation = ++m_applyRevision;
    QHash<QString, Entry> winners;
    for (const auto &entry : m_entries)
        for (const auto &target : entry.icons.images.keys()) {
            const auto other = winners.constFind(target);
            if (other == winners.constEnd() || entry.priority > other->priority ||
                (entry.priority == other->priority && entry.order > other->order))
                winners[target] = entry;
        }
    const auto icon = winners.contains("window")
                          ? QIcon(QPixmap::fromImage(winners["window"].icons.images["window"]))
                          : m_default;
    QGuiApplication::setWindowIcon(icon);
    if (generation != m_applyRevision)
        return;
    const auto windows = m_windows;
    const auto windowImage =
        winners.contains("window") ? winners["window"].icons.images.value("window") : QImage();
    for (auto it = windows.begin(); it != windows.end(); ++it) {
        if (!m_windows.contains(it.key()))
            continue;
        it.key()->setIcon(winners.contains("window") ? icon : it.value());
#ifdef Q_OS_WIN
        // 运行中的任务栏按钮实时跟随窗口图标（与 Electron 一致）。
        if (winners.contains("window") && !windowImage.isNull())
            sendRuntimeWindowIcon(it.key(), windowImage);
        else
            clearRuntimeWindowIcon(it.key(), it.value());
#endif
        if (generation != m_applyRevision)
            return;
    }
    QHash<QString, QString> paths;
    for (const auto &target : {QStringLiteral("desktop"), QStringLiteral("taskbar")})
        if (winners.contains(target))
            paths[target] = winners[target].icons.paths.value(target);
    if (paths != m_desiredShortcuts) {
        m_desiredShortcuts = paths;
        ++m_revision;
        scheduleShortcuts();
    }
    scheduleShortcuts();
    emit changed();
}
void AppIconService::scheduleShortcuts()
{
    if (m_shortcutBusy || (!m_forceShortcuts && m_desiredShortcuts == m_submittedShortcuts))
        return;
    m_forceShortcuts = false;
    const auto paths = m_desiredShortcuts;
    const auto revision = m_revision;
    m_submittedShortcuts = paths;
    m_shortcutBusy = true;
    ++m_pending;
    emit drainedChanged();
    auto *watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, this, [this, watch, revision] {
        const auto result = watch->result();
        watch->deleteLater();
        --m_pending;
        m_completedRevision = qMax(m_completedRevision, revision);
        m_shortcutBusy = false;
        if (revision == m_revision) {
            m_error = result.value("error").toString();
            emit changed();
        }
        if (!m_error.isEmpty() && revision == m_revision) {
            const auto target = result.value("target").toString();
            QString token;
            const Entry *winner = nullptr;
            for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
                if (it->icons.paths.contains(target) &&
                    (!winner || it->priority > winner->priority ||
                     (it->priority == winner->priority && it->order > winner->order))) {
                    token = it.key();
                    winner = &it.value();
                }
            if (!token.isEmpty())
                emit contributionFailed(token, m_error);
        }
        scheduleShortcuts();
        emit drainedChanged();
    });
    watch->setFuture(QtConcurrent::run(
        &m_worker, [worker = m_shortcuts, paths] { return worker->store.apply(paths); }));
}
void AppIconService::refresh()
{
    ++m_revision;
    m_forceShortcuts = true;
    apply();
}
void AppIconService::restoreShortcut(const QString &target, std::function<void(QVariantMap)> done,
                                     std::shared_ptr<std::atomic_bool> canceled)
{
    if (m_pending >= 16) {
        done({{"error", "Too many icon operations"}});
        return;
    }
    ++m_pending;
    emit drainedChanged();
    auto *watch = new QFutureWatcher<QVariantMap>(this);
    connect(watch, &QFutureWatcher<QVariantMap>::finished, this, [this, watch, done] {
        const auto result = watch->result();
        watch->deleteLater();
        --m_pending;
        done(result);
        emit drainedChanged();
    });
    watch->setFuture(QtConcurrent::run(&m_worker, [worker = m_shortcuts, target, canceled] {
        return worker->store.restoreDefault(target, canceled);
    }));
}
QVariantMap AppIconService::snapshot() const
{
    QVariantList contributions;
    for (const auto &entry : m_entries) {
        auto row = entry.owner;
        row["priority"] = entry.priority;
        row["targets"] = entry.icons.images.keys();
        contributions.append(row);
    }
    return {{"contributions", contributions},
            {"busy", !drained()},
            {"error", m_error},
            {"window", supported("window")},
            {"tray", supported("tray")},
            {"desktop", supported("desktop")},
            {"taskbar", supported("taskbar")}};
}
} // namespace EchoMusic
