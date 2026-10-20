#pragma once
#include "NativeBridge.h"
#include <QAbstractListModel>
#include <QThread>
#include <QUrl>

namespace EchoMusic
{
class DspProviderStore;

/// Application-owned trusted native DSP catalog; this is separate from QML/JS
/// plugins. ABI inspection happens on Rust's non-real-time actor, never the GUI.
class DspProviderRegistry final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    DspProviderRegistry(NativeBridge &native, const QString &directory, QObject *parent = nullptr);
    ~DspProviderRegistry() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const
    {
        return m_rows.size();
    }
    bool busy() const
    {
        return m_busy;
    }
    QString errorMessage() const
    {
        return m_error;
    }
    QVariantMap record(const QString &id) const;
    Q_INVOKABLE void importFile(const QUrl &url);
    void remove(const QString &id);
signals:
    void changed();
    void restored();
    void imported(const QString &id);

private:
    void fail(const QString &message);
    NativeBridge &m_native;
    QThread m_thread;
    DspProviderStore *m_store;
    QVariantList m_rows;
    QVariantMap m_candidate;
    QString m_error, m_importing;
    quint64 m_probe = 0;
    bool m_busy = true, m_restored = false;
};
} // namespace EchoMusic
