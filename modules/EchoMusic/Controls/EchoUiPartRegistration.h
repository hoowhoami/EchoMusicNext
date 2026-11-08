#pragma once
#include <QFont>
#include "EchoUiContentFactory.h"
#include <QPointer>
#include <QQuickItem>
#include <QQmlParserStatus>
#include <QVariantMap>
#include <QMap>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistry;
// Host declaration only. Public SDK references never expose this object/target.
class EchoUiPartRegistration : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QString parentId READ parentId NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString scope READ scope NOTIFY changed)
    Q_PROPERTY(QString resourceKey READ resourceKey NOTIFY changed)
    Q_PROPERTY(bool persistent READ persistent NOTIFY changed)
    Q_PROPERTY(QString layoutKind READ layoutKind NOTIFY changed)
    Q_PROPERTY(QString layoutEngine READ layoutEngine NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic CONSTANT)
    Q_PROPERTY(QStringList insertionPositions READ insertionPositions NOTIFY changed)
    Q_PROPERTY(
        QVariantList nativeStyleBindings READ nativeStyleBindings NOTIFY nativeStyleBindingsChanged)
    Q_PROPERTY(int contractVersion READ contractVersion CONSTANT)
    Q_PROPERTY(QString owner READ owner WRITE setOwner NOTIFY declarationChanged)
    Q_PROPERTY(EchoMusic::EchoUiContentFactory *nativeFactory READ nativeFactory WRITE
                   setNativeFactory NOTIFY factoryChanged)
    Q_PROPERTY(
        EchoMusic::EchoUiContentFactory *contentFactory READ contentFactory NOTIFY factoryChanged)
    Q_PROPERTY(QVariantMap layoutOverrides READ layoutOverrides NOTIFY overridesChanged)
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY declarationChanged)
    Q_PROPERTY(QString role READ role WRITE setRole NOTIFY declarationChanged)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY declarationChanged)
    Q_PROPERTY(QString surface READ surface WRITE setSurface NOTIFY declarationChanged)
    Q_PROPERTY(QStringList properties READ properties WRITE setProperties NOTIFY declarationChanged)
    Q_PROPERTY(QStringList actions READ actions WRITE setActions NOTIFY declarationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY declarationChanged)
    Q_PROPERTY(bool replaceable READ replaceable WRITE setReplaceable NOTIFY declarationChanged)
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY overridesChanged)
    Q_PROPERTY(bool hasVisualOverrides READ hasVisualOverrides NOTIFY hasVisualOverridesChanged)
    Q_PROPERTY(QString identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(bool observed READ observed NOTIFY observedChanged)
    Q_PROPERTY(QVariantMap state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap baseValues READ baseValues WRITE setBaseValues NOTIFY changed)
    Q_PROPERTY(bool replaced READ replaced NOTIFY replacementChanged)
    Q_PROPERTY(bool contentReplaced READ contentReplaced NOTIFY factoryChanged)
public:
    explicit EchoUiPartRegistration(QObject *parent = nullptr);
    ~EchoUiPartRegistration() override;
    void classBegin() override {}
    void componentComplete() override;
    QQuickItem *target() const
    {
        return m_target;
    }
    void setTarget(QQuickItem *);
    QString parentId() const;
    QString address() const;
    QString scope() const;
    QString resourceKey() const;
    bool persistent() const
    {
        return !address().isEmpty() && !address().startsWith('@');
    }
    QString layoutKind() const;
    QString layoutEngine() const;
    QStringList insertionPositions() const;
    bool automatic() const
    {
        return m_automatic;
    }
    void initializeAutomatic(EchoUiPartRegistry &, QQuickItem *, const QString &role,
                             const QString &key);
    int contractVersion() const
    {
        return 1;
    }
    QString owner() const;
    void setOwner(const QString &);
    EchoUiContentFactory *nativeFactory() const
    {
        return m_nativeFactory;
    }
    EchoUiContentFactory *contentFactory() const
    {
        return m_contentFactory ? m_contentFactory.data() : m_nativeFactory.data();
    }
    void setNativeFactory(EchoUiContentFactory *);
    bool canCompose() const;
    bool ensureNativeFactory();
    void setContentFactory(EchoUiContentFactory *, const QString &signature, bool replaced = false);
    bool contentReplaced() const
    {
        return m_contentReplaced;
    }
    QVariantMap layoutOverrides() const;
    QString compositionSignature() const
    {
        return m_compositionSignature;
    }

    QString role() const
    {
        return m_role;
    }
    void setRole(const QString &);
    QString key() const
    {
        return m_key;
    }
    void setKey(const QString &);
    QString surface() const;
    void setSurface(const QString &);
    QStringList properties() const;
    QVariantList nativeStyleBindings() const
    {
        return m_nativeBindings;
    }
    void setProperties(const QStringList &);
    QStringList actions() const;
    QStringList declaredActions() const
    {
        return m_actions;
    }
    void setActions(const QStringList &);
    bool active() const
    {
        return m_active;
    }
    void setActive(bool);
    bool replaceable() const
    {
        return m_replaceable;
    }
    void setReplaceable(bool);
    QVariantMap overrides() const
    {
        return m_overrides;
    }
    bool hasVisualOverrides() const
    {
        return m_hasVisualOverrides;
    }
    QVariantMap baseValues() const;
    void setBaseValues(const QVariantMap &);
    bool observed() const
    {
        return m_readers > 0;
    }
    void acquireState()
    {
        if (++m_readers == 1)
            emit observedChanged();
    }
    void releaseState()
    {
        if (m_readers > 0 && --m_readers == 0)
            emit observedChanged();
    }
    QVariantMap state() const;
    QVariantMap modelContext() const;
    void setState(const QVariantMap &v);
    bool replaced() const
    {
        return !m_replacement.isEmpty();
    }
    QString replacement() const
    {
        return m_replacement;
    }
    QString replacementCandidate() const
    {
        return m_candidate;
    }
    QString identity() const
    {
        return m_identity;
    }
    QString windowId() const;
    QRectF geometry() const;
    bool presented() const;
    void setStyleObserved(bool);
    void setResolved(const QVariantMap &, const QVariantMap &sources = {});
    QVariantMap visualSources() const
    {
        return m_visualSources;
    }
    void setReplacement(const QString &, const QString &candidate = {});
    Q_INVOKABLE QFont resolveFont(const QFont &base, const QVariantMap &values) const;
    Q_INVOKABLE void publish(const QString &event, const QVariantMap &payload = {});
    bool invoke(const QString &action, const QVariantMap &arguments);
signals:
    void nativeStyleBindingsChanged();
    void factoryChanged();
    void stateChanged();
    void identityChanged();
    void observedChanged();
    void declarationChanged();
    void overridesChanged();
    void hasVisualOverridesChanged();
    void replacementChanged();
    void mountOrderChanged();
    void changed();
    void eventOccurred(const QString &event, const QVariantMap &payload);
    void actionRequested(const QString &action, const QVariantMap &arguments);

private slots:
    void nativeActivated();
    void nativeStateChanged();

private:
    friend class EchoUiPartRegistry;
    void refresh();
    void connectTarget();
    void applyNativeStyles();
    void releaseAdaptationLater();
    QMap<QString, QString> nativeProperties() const;
    QPointer<EchoUiPartRegistry> m_registry;
    QPointer<QQuickItem> m_target;
    QQuickItem *m_indexedTarget = nullptr;
    QString m_owner = "@host", m_compositionSignature, m_indexedKey, m_indexedRole;
    bool m_contentReplaced = false;
    QPointer<EchoUiContentFactory> m_nativeFactory, m_contentFactory;
    QString m_role, m_key, m_surface, m_identity, m_replacement, m_candidate;
    QStringList m_properties, m_actions;
    QVariantMap m_overrides, m_base, m_state, m_visualSources;
    QList<QMetaObject::Connection> m_connections;
    bool m_stateNoticePending = false, m_refreshing = false;
    bool m_visualNoticePending = false, m_hasVisualOverrides = false;
    int m_readers = 0;
    bool m_styleObserved = false;
    bool m_active = true, m_complete = false, m_replaceable = false;
    bool m_automatic = false;
    QVariantList m_nativeBindings;
    QVariantMap m_nativeDefaults;
    QPointer<QObject> m_nativeStyleAdapter;
    quint64 m_nativeStyleGeneration = 0;
};
} // namespace EchoMusic
