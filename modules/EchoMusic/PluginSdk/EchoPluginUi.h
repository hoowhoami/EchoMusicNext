#pragma once
#include "EchoPluginOperation.h"
#include "EchoPluginRegistration.h"
#include "EchoPluginTask.h"
#include "EchoPluginUiParts.h"
#include "EchoPluginUiStyles.h"
#include "EchoPluginAppearance.h"
#include <QJSValue>
#include <QUrl>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginTask;
/// Final host registries, not a parallel plugin UI. Definitions are staged
/// during activation and removed before a context is destroyed.
class EchoPluginUi final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(EchoMusic::EchoPluginAppearance *lyricsPage READ lyricsPage CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginUiParts *parts READ parts CONSTANT)
    Q_PROPERTY(EchoMusic::EchoPluginUiStyles *styles READ styles CONSTANT)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QUrl settingsComponent READ settingsComponent NOTIFY settingsChanged)
    QML_ELEMENT
    QML_UNCREATABLE("Owned by EchoPluginContext")
public:
    explicit EchoPluginUi(EchoPluginContext &scope);
    EchoPluginAppearance *lyricsPage() const;
    EchoPluginUiParts *parts() const
    {
        return m_parts;
    }
    EchoPluginUiStyles *styles() const
    {
        return m_styles;
    }
    QUrl settingsComponent() const
    {
        return m_settingsComponent;
    }
    QVariantMap state() const
    {
        return m_scopeState();
    }
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerSidebarItem(const QString &id,
                                                                       const QJSValue &definition);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerShortcutCard(const QString &id,
                                                                        const QJSValue &definition);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerSongMenuItem(const QString &id,
                                                                        const QJSValue &definition);
    Q_INVOKABLE bool setState(const QVariantMap &values);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    registerSurface(const QString &id, const QString &target, const QString &component);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerSettings(const QString &component);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerPage(const QString &id,
                                                                const QString &component);
    Q_INVOKABLE EchoMusic::EchoPluginOperation *navigate(const QString &id,
                                                         const QVariantMap &query = {});
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *registerTitlebarAction(const QString &id,
                                                                          const QString &title,
                                                                          const QString &icon,
                                                                          const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginRegistration *
    registerPlayerbarAction(const QString &id, const QString &title, const QString &icon,
                            const QJSValue &callback);
    Q_INVOKABLE EchoMusic::EchoPluginTask *createTask(const QString &id, const QString &title);

signals:
    void settingsChanged();
    void stateChanged();

private:
    EchoPluginRegistration *sidebarEntry(const QString &, const QJSValue &, bool);
    QVariantMap m_scopeState() const;
    EchoPluginUiParts *m_parts;
    EchoPluginUiStyles *m_styles;
    QUrl m_settingsComponent;
    QVariantMap m_state;
    EchoPluginRegistration *action(bool playerbar, const QString &id, const QString &title,
                                   const QString &icon, const QJSValue &callback);
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
