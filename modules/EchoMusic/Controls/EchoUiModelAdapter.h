#pragma once
#include "EchoUiExtensionModel.h"
#include "EchoUiValueModel.h"
#include "EchoUiModelCell.h"
#include "EchoUiPropertyRelay.h"
#include "EchoUiSelectionModel.h"
#include <QJSValue>
#include <memory>
#include <QQmlEngine>
#include <QQuickItem>

namespace EchoMusic
{
class EchoUiModelAdapter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QVariantList displayEntries READ displayEntries NOTIFY changed)
    Q_PROPERTY(QVariantList columnEntries READ columnEntries NOTIFY changed)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(QVariant modelInput READ modelInput WRITE setModelInput NOTIFY inputChanged)
    Q_PROPERTY(
        QQmlComponent *delegateInput READ delegateInput WRITE setDelegateInput NOTIFY inputChanged)
    Q_PROPERTY(
        QJSValue rowHeightInput READ rowHeightInput WRITE setRowHeightInput NOTIFY inputChanged)
    Q_PROPERTY(QJSValue columnWidthInput READ columnWidthInput WRITE setColumnWidthInput NOTIFY
                   inputChanged)
    Q_PROPERTY(qreal baseHeight READ baseHeight WRITE setBaseHeight NOTIFY inputChanged)
    Q_PROPERTY(qreal baseWidth READ baseWidth WRITE setBaseWidth NOTIFY inputChanged)
    Q_PROPERTY(int directionInput READ directionInput WRITE setDirectionInput NOTIFY inputChanged)
    Q_PROPERTY(int nativeRowCount READ nativeRowCount NOTIFY changed)
    Q_PROPERTY(int nativeColumnCount READ nativeColumnCount NOTIFY changed)
    Q_PROPERTY(
        QObject *selectionInput READ selectionInput WRITE setSelectionInput NOTIFY inputChanged)
public:
    QVariantList entries() const
    {
        return m_semanticEntries;
    }
    QVariantList columnEntries() const;
    QVariantList displayEntries() const;
    Q_INVOKABLE QVariantMap orderEntries(const QVariantList &nativeKeys,
                                         const QVariantList &entries) const;
    Q_INVOKABLE QVariantMap projectEntries(const QVariantList &nativeKeys) const;
    EchoUiModelRecipe *recipeFor(const QString &identity) const;
    QVariantMap entryFor(const QString &identity) const;
    QString selectedKey() const
    {
        return m_selectedKey;
    }
    Q_INVOKABLE bool selectEntry(int index);
    Q_INVOKABLE void clearSelection();
    QVariant modelInput() const
    {
        return m_modelWasObject && !m_nativeModel ? QVariant::fromValue<QObject *>(nullptr)
                                                  : m_originalModel;
    }
    QQmlComponent *delegateInput() const
    {
        return m_nativeDelegate;
    }
    QJSValue rowHeightInput() const
    {
        return m_rowHeight;
    }
    QJSValue columnWidthInput() const
    {
        return m_columnWidth;
    }
    qreal baseHeight() const
    {
        return m_baseHeight;
    }
    qreal baseWidth() const
    {
        return m_baseWidth;
    }
    int directionInput() const
    {
        return m_direction;
    }
    int nativeRowCount() const
    {
        return m_projection.nativeRows();
    }
    int nativeColumnCount() const
    {
        return m_projection.nativeColumns();
    }
    void setModelInput(const QVariant &);
    void setDelegateInput(QQmlComponent *);
    void setRowHeightInput(const QJSValue &);
    void setColumnWidthInput(const QJSValue &);
    void setBaseHeight(qreal);
    void setBaseWidth(qreal);
    void setDirectionInput(int);
    QObject *selectionInput() const
    {
        return m_nativeSelection;
    }
    void setSelectionInput(QObject *);
    Q_INVOKABLE bool armEntry(const QString &token, const QString &key);
    Q_INVOKABLE bool invokeEntry(const QString &token, const QString &key, bool checked);
    Q_INVOKABLE int nativeSection(int index, bool column) const
    {
        return m_projection.nativeSection(index, column);
    }
    Q_INVOKABLE qreal extensionSize(int index, bool column) const;

    struct Record
    {
        QString token, owner, id, kind;
        int priority = 0, rank = -1;
        QList<EchoUiExtensionModel::Entry> entries;
        QPointer<EchoUiModelRecipe> recipe;
        bool operator==(const Record &) const = default;
    };
    static EchoUiModelAdapter *forView(QQuickItem *, QQmlEngine &, bool create);
    ~EchoUiModelAdapter() override;
    void beginPublication(const QSet<QString> &masked = {});
    void endPublication(bool commit);
    bool canPut(const Record &, QString &error);
    Record record(const QString &token) const
    {
        return m_records.value(token);
    }
    bool put(const Record &, QString &error);
    QVariantMap contextData() const;
    void remove(const QString &token);
    QString errorMessage() const
    {
        return m_error;
    }
    QString kind() const;
    QVariantMap diagnostics() const;
    void setStatus(const QString &token, const QVariantMap &);
    static QVariantMap inspect(QQuickItem *, QQmlEngine &, const Record &);
    static QVariantMap inspectRecords(QQuickItem *, QQmlEngine &, const QList<Record> &);
signals:
    void projectionErrorChanged(const QString &);
    void changed();
    void inputChanged();
private slots:
    void nativeModelChanged();
    void nativeDelegateChanged();
    void nativeRowHeightChanged();
    void nativeColumnWidthChanged();
    void nativeHeightChanged();
    void nativeWidthChanged();
    void nativeDirectionChanged();
    void nativeSelectionChanged();
    void nativeSemanticChanged();

private:
    EchoUiModelAdapter(QQuickItem *, QQmlEngine &);
    void refresh();
    void restore();
    void write(const char *property, const QVariant &);
    void captureInputs();
    void applyMetrics();
    void refreshMenu();
    void refreshSemantic();
    void refreshColumns();
    QVariantList m_semanticEntries;
    QString m_selectedKey, m_selectedToken;
    QPointer<EchoUiModelRecipe> m_selectedRecipe;
    QPointer<QObject> m_menuBridge;
    QPointer<QQuickItem> m_view;
    QQmlEngine &m_engine;
    EchoUiExtensionModel m_projection;
    EchoUiValueModel m_valuesModel;
    QPointer<QQmlComponent> m_nativeDelegate, m_delegate;
    QPointer<QObject> m_nativeModel;
    QHash<QString, Record> m_records, m_heldRecords;
    int m_publicationDepth = 0;
    QList<QMetaObject::Connection> m_connections;
    QVariant m_originalModel;
    std::unique_ptr<EchoUiPropertyRelay> m_modelRelay, m_delegateRelay, m_rowRelay, m_columnRelay,
        m_heightRelay, m_widthRelay, m_directionRelay, m_selectionRelay;
    QPointer<QItemSelectionModel> m_nativeSelection;
    EchoUiSelectionModel *m_selectionBridge = nullptr;
    QJSValue m_rowHeight, m_columnWidth, m_metricWrapper, m_wrappedRow, m_wrappedColumn;
    qreal m_baseHeight = 0, m_baseWidth = 0;
    int m_direction = 0;
    QList<qreal> m_extraHeights, m_extraWidths;
    bool m_capturing = false, m_modelWasObject = false;
    QMetaObject::Connection m_sourceDestroyed;

    QString m_error;
    QHash<QString, QVariantMap> m_status;
    QList<QMetaObject::Connection> m_selectionContextConnections, m_modelContextConnections;
    QPersistentModelIndex m_savedCurrentSource;
    int m_savedIndex = -1;
    QPointF m_savedPosition;
    bool m_writing = false, m_restored = true, m_pending = false;
};
} // namespace EchoMusic
