#pragma once
#include "EchoPluginRegistration.h"
#include "AppearanceCatalog.h"
#include <QJSValue>
#include <QFont>
namespace EchoMusic
{
class EchoPluginContext;
class EchoPluginAppearanceHandle final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Returned by appearance registration")
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(EchoMusic::AppearanceConfiguration *configuration READ configuration CONSTANT)
public:
    EchoPluginAppearanceHandle(EchoPluginRegistration &, AppearanceEntry &, QObject *parent);
    QString key() const
    {
        return m_key;
    }
    bool active() const;
    AppearanceConfiguration *configuration() const
    {
        return m_configuration;
    }
    Q_INVOKABLE void dispose();
signals:
    void changed();

private:
    QString m_key;
    QPointer<EchoPluginRegistration> m_lease;
    QPointer<AppearanceConfiguration> m_configuration;
};
class EchoPluginAppearance final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit EchoPluginAppearance(EchoPluginContext &);
    Q_INVOKABLE EchoMusic::EchoPluginAppearanceHandle *registerTheme(const QJSValue &definition);
    Q_INVOKABLE EchoMusic::EchoPluginAppearanceHandle *registerSkin(const QJSValue &definition);
    Q_INVOKABLE bool useTheme(const QString &id);
    Q_INVOKABLE bool useSkin(const QString &id);
    Q_INVOKABLE bool previewTheme(const QString &id);
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE void openThemes();
    Q_INVOKABLE void openSkins();
    QVariantMap snapshot() const;
    QString errorMessage() const
    {
        return m_error;
    }
    bool drained() const;
    QVariantMap presentation(const QString &surface, QObject *owner, AppearanceConfiguration *,
                             AppearanceEntry *entry);
    static bool validateVisuals(const QVariantMap &, QString &error);
signals:
    void changed();

private:
    EchoPluginAppearanceHandle *add(const QJSValue &, const QString &kind);
    QString key(const QString &id, const QString &kind) const;
    EchoPluginContext &m_scope;
    QString m_error;
    QList<QPointer<AppearanceConfiguration>> m_configurations;
};
class EchoPluginFonts final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by plugin context")
    Q_PROPERTY(QStringList families READ families CONSTANT)
public:
    explicit EchoPluginFonts(EchoPluginContext &);
    QStringList families() const;
    Q_INVOKABLE QFont build(const QStringList &families, int pixelSize = 14, int weight = 400,
                            qreal letterSpacing = 0) const;

private:
    EchoPluginContext &m_scope;
};
} // namespace EchoMusic
