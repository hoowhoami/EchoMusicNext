#pragma once
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>
namespace EchoMusic
{
class EchoUiPartRegistration;
class EchoUiContentFactory;
class EchoUiStructure;
class EchoUiAnchorGroup;
class EchoUiFocusOrder;
class EchoUiPartRegistry final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY preferencesChanged)
public:
    /// Per-engine registry, parented to its engine. A QML_SINGLETON built from
    /// a default constructor goes through `QQmlElement<T>`, which derives from
    /// T and therefore cannot wrap a `final` class; requiring the engine makes
    /// Qt fall back to create() below (the same arrangement as ThemeStore).
    explicit EchoUiPartRegistry(QQmlEngine &engine);
    /// QML singleton entry point. The engine owns the instance, so C++ and
    /// script always see exactly the object forEngine() hands out.
    static EchoUiPartRegistry *create(QQmlEngine *engine, QJSEngine *scriptEngine);
    static EchoUiPartRegistry *forEngine(QQmlEngine &);
    QQmlEngine &engine() const
    {
        return m_engine;
    }
    Q_INVOKABLE void acquireStructure(QObject *consumer, bool inspector = false);
    Q_INVOKABLE void releaseStructure(QObject *consumer);
    void retainStructure(QObject *consumer);
    void scopeChanged(QQuickItem *root);
    void modelContextChanged(QQuickItem *root);
    void modelDataChanged(QQuickItem *view);
    void addressTreeChanged(QQuickItem *root);
    QList<QQuickItem *> presentationRoots() const;
    QString address(const EchoUiPartRegistration *) const;
    QString scope(const EchoUiPartRegistration *) const;
    QString resourceKey(const EchoUiPartRegistration *) const;
    bool hasExplicitTarget(QQuickItem *) const;
    void attach(EchoUiPartRegistration *);
    void requestSessionReset();
    void detach(EchoUiPartRegistration *);
    EchoUiPartRegistration *find(const QString &) const;
    QList<EchoUiPartRegistration *> query(const QVariantMap &) const;
    static bool validSelector(const QVariantMap &);
    static bool matches(const EchoUiPartRegistration &, const QVariantMap &);
    void notifyStructure();
    void nativeStateChanged(EchoUiPartRegistration *);
    static bool normalize(QVariantMap &, QString &error);
    bool putStyle(const QString &token, const QString &owner, const QString &id, int priority,
                  const QVariantMap &selector, const QVariantMap &values);
    void removeStyle(const QString &token);
    void putReplacement(const QString &token, const QString &part, const QString &owner,
                        const QString &id, int priority, const QString &position = "replace");
    QStringList mountOrder(const QString &part, const QString &position) const;
    void removeReplacement(const QString &token);
    void setMountReady(const QString &token, bool ready);
    QString windowId(QQuickWindow *) const;
    bool enabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool);
    bool allowed(const EchoUiPartRegistration *, const QString &owner, const QString &id) const;
    int preferenceRank(const EchoUiPartRegistration *, const QString &owner,
                       const QString &id) const;
    Q_INVOKABLE QStringList preference(const QString &address) const;
    QVariantMap preferences() const;
    void setPreferences(const QVariantMap &);
    bool putMove(const QString &token, const QString &part, const QString &destination,
                 const QString &position, const QString &owner, const QString &id, int priority);
    void removeMove(const QString &token);
    QString moveWinner(const QString &part) const;
    QString parentOf(const EchoUiPartRegistration *) const;
    Q_INVOKABLE QVariantList snapshot() const;
    Q_INVOKABLE QVariantMap describe(const QString &identity) const;
    Q_INVOKABLE QVariantList contributions(const QString &part) const;
    Q_INVOKABLE QVariantMap styleSources(const QString &part) const;
    QVariantMap compositionState(const QString &token) const;
    Q_INVOKABLE void setPreference(const QString &address, const QStringList &order);
    Q_INVOKABLE void resetPreferences();
    bool putComposition(const QString &token, const QString &part, const QString &owner,
                        const QString &id, int priority, const QString &operation,
                        EchoUiContentFactory *factory);
    void removeComposition(const QString &token);
    void reportComposition(const QString &token, const QString &part, const QString &owner,
                           const QString &id, const QString &operation, int priority,
                           const QString &status, const QString &error);
    void setCompositionItem(const QString &token, QQuickItem *, const QString &position,
                            const QString &axis = "horizontal", qreal gap = 0);
    void resolveComposition(EchoUiPartRegistration *);
    void beginPublication();
    void endPublication();
    void maskPublication(const QSet<QString> &, bool masked);
    QString settleInsertions(const QSet<QString> &targets = {});
    bool canPublish(int styles, int compositions, int moves) const;
    int indexedTargetCount() const
    {
        return m_targets.size();
    }
    int structureWatchCount() const;
    int partCount() const
    {
        return m_parts.size();
    }
    int styleCount() const
    {
        return m_styles.size();
    }
signals:
    void modelDiagnosticsChanged();
    void structureChanged();
    void compositionChanged(const QString &part);
    void treeChanged();
    void preferencesChanged();
    void added(EchoMusic::EchoUiPartRegistration *part);
    void removed(const QString &identity);
    void partChanged(EchoMusic::EchoUiPartRegistration *part);

private:
    // setProperties/setActions re-resolve a declaration in place (the
    // setPreference idiom) and reach the private resolve() for it.
    friend class EchoUiPartRegistration;
    int m_publicationDepth = 0;
    QSet<QString> m_maskedPublications;
    void forgetAddress(const QString &identity);
    void refreshStyleDependencies();
    QList<EchoUiPartRegistration *> declarationsWithin(QQuickItem *, bool visual) const;
    QSet<QString> m_styleDependencySources;
    QHash<QString, QSet<QString>> m_styleTargets, m_partStyleTokens;
    void forgetPartStyles(const QString &);
    void rebuildAddresses() const;
    QSet<EchoUiPartRegistration *> m_declarations;
    QHash<EchoUiPartRegistration *, QQuickItem *> m_declarationTargets;
    QHash<QQuickItem *, QSet<EchoUiPartRegistration *>> m_targetDeclarations;
    QSet<QQuickItem *> m_presentationRoots;
    QList<QPointer<EchoUiPartRegistration>> m_scopePending;
    bool m_scopeScheduled = false;
    mutable QHash<QString, QString> m_addresses, m_rawAddresses, m_scopes, m_resources;
    mutable QHash<QString, QSet<QString>> m_addressMembers, m_scopeMembers, m_resourceMembers;
    mutable QSet<QString> m_dirtyAddresses;
    struct Composition
    {
        QString token, part, owner, id, operation;
        int priority;
        QPointer<EchoUiContentFactory> factory;
        QPointer<QQuickItem> item;
        QString position;
        QString status, error;
        QString axis = "horizontal";
        qreal gap = 0;
    };
    struct MoveState
    {
        QPointer<QQuickItem> origin, next;
        QString token;
    };
    void resolveMoves();
    void resolveInsertions();
    void suspendFocusOrders();
    QHash<QString, MoveState> m_moveStates;
    QHash<QString, QVariantMap> m_moves;
    bool m_moving = false, m_enabled = true;
    void notifyTree();
    QHash<QQuickItem *, QList<QPointer<EchoUiPartRegistration>>> m_targets;
    QHash<QString, Composition> m_compositions;
    QHash<QString, QStringList> m_preferences;
    QHash<QString, QPointer<EchoUiAnchorGroup>> m_anchorGroups;
    QHash<QString, QPointer<EchoUiFocusOrder>> m_focusOrders;
    QList<QPointer<EchoUiFocusOrder>> m_appliedFocusOrders;
    QHash<QString, QList<QPointer<EchoUiContentFactory>>> m_chains;
    struct CachedChain
    {
        QPointer<EchoUiContentFactory> root;
        QList<QPointer<EchoUiContentFactory>> layers;
    };
    QHash<QString, CachedChain> m_chainCache;
    bool m_treePending = false, m_structurePending = false;
    struct Style
    {
        QString token, owner, id;
        int priority = 0;
        QVariantMap selector, values;
    };
    QString styleBucket(const QVariantMap &) const;
    QList<Style> matchingStyles(EchoUiPartRegistration *, bool updateObservation = false) const;
    void resolve(EchoUiPartRegistration *);
    void resolveReplacement(EchoUiPartRegistration *);
    QHash<QString, QPointer<EchoUiPartRegistration>> m_parts;
    QHash<QString, Style> m_styles;
    QHash<QString, QSet<QString>> m_styleBuckets, m_keyIndex, m_roleIndex;
    QHash<QString, Style> m_replacements;
    mutable QHash<QQuickWindow *, QString> m_windows;
    mutable quint64 m_windowSequence = 0;
    quint64 m_sequence = 0;
    QSet<QString> m_sessionResetParts;
    bool m_sessionResetPending = false;
    QSet<EchoUiPartRegistration *> m_statePending;
    QQmlEngine &m_engine;
    EchoUiStructure *m_structure = nullptr;
};
} // namespace EchoMusic
