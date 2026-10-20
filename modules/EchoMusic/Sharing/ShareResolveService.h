#pragma once
#include "ApiClient.h"
#include "ShareTarget.h"
#include "SharingCommands.h"
#include "RouteController.h"
#include <QHash>
#include <QJsonDocument>
#include <QTimer>
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace EchoMusic
{
class ShellController;
class WindowController;
class ShareResolveService final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Application-owned incoming shares")
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool spinner READ spinner NOTIFY changed)
    Q_PROPERTY(QString resourceLabel READ resourceLabel NOTIFY changed)
    Q_PROPERTY(QString targetId READ targetId NOTIFY changed)
    Q_PROPERTY(QString heading READ heading NOTIFY changed)
    Q_PROPERTY(QString explanation READ explanation NOTIFY changed)
public:
    /// Explicit UI composition; construction alone does not register pages.
    void registerRoutes(RouteController &router);
    ShareResolveService(ApiClient &, SharingCommands &, RouteController &, ShellController &,
                        WindowController &, bool network = true, QObject *parent = nullptr);
    ~ShareResolveService() override;
    bool openUrl(const QUrl &);
    bool openText(const QString &);
    void setPluginOpener(std::function<bool(const ShareTarget &)> opener);
    void startClipboardMonitoring();
    void inspectClipboard();
    bool loading() const
    {
        return m_loading;
    }
    bool spinner() const
    {
        return m_spinner;
    }
    QString resourceLabel() const;
    QString targetId() const;
    QString heading() const;
    QString explanation() const;
    Q_INVOKABLE void retry();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void goBack();
    void shutdown();
signals:
    void changed();

private:
    friend class ShareResolveServiceTestAccess;
    enum class Kind
    {
        Song,
        Playlist,
        Artist,
        Album,
        RoomState,
        RoomDetail
    };
    struct Pending
    {
        Kind kind;
        quint64 generation, session;
    };
    void begin(const QVariantMap &);
    void cancel();
    void watch(quint64, Kind);
    void accept(quint64, const QByteArray &);
    void reject(quint64, const QByteArray &);
    void fail(const QString &reason);
    void finishRoom();
    void resolved(const QString &route, const QVariantMap &query, const QString &id = {});
    bool active() const;
    ApiClient &m_api;
    SharingCommands &m_commands;
    RouteController &m_router;
    ShellController &m_shell;
    WindowController &m_window;
    std::function<bool(const ShareTarget &)> m_pluginOpener;
    QHash<quint64, Pending> m_pending;
    std::optional<ShareTarget> m_target;
    quint64 m_generation = 0;
    QTimer m_spinnerTimer;
    QString m_reason = "invalid", m_lastClipboardText;
    QJsonObject m_roomDetail;
    int m_roomPending = 0;
    bool m_loading = false, m_spinner = false, m_network, m_closed = false,
         m_clipboardMonitoring = false;
};
} // namespace EchoMusic
