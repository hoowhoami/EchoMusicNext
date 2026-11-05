#pragma once
#include "ApiClient.h"
#include <QElapsedTimer>
#include <QJsonObject>

namespace EchoMusic
{
struct MembershipFacts
{
    bool known = false;
    bool conceptVip = false;
    bool superVip = false;
    bool operator==(const MembershipFacts &) const = default;
};
/// GUI-thread owner of the current account's membership snapshot.
/// Profile presentation and source selection borrow the cache; credentials
/// and entitlement snapshots stay out of QML, queue storage and the audio worker.
class MembershipService final : public QObject
{
    Q_OBJECT
public:
    explicit MembershipService(ApiClient &api, QObject *parent = nullptr);
    ~MembershipService() override;
    void ensure(bool force = false);
    bool loading() const
    {
        return m_request != 0;
    }
    MembershipFacts facts() const
    {
        return m_facts;
    }
    const QJsonObject &domain() const
    {
        return m_domain;
    }
    QString errorMessage() const
    {
        return m_error;
    }
signals:
    void changed();
    // Loading/cache refreshes must not invalidate a source awaiting this query.
    void factsChanged();
    void settled();

private:
    friend class AccountServiceTestAccess;
    friend class MembershipServiceTestAccess;
    void clear();
    void succeed(quint64 id, const QByteArray &bytes);
    void fail(quint64 id, const QByteArray &payload = {});
    void setFacts(MembershipFacts facts);
    ApiClient &m_api;
    quint64 m_request = 0, m_revision = 0;
    QElapsedTimer m_age;
    MembershipFacts m_facts;
    QJsonObject m_domain;
    QString m_error;
};
} // namespace EchoMusic
