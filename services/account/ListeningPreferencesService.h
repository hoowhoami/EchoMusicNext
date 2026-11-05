#pragma once
#include "AccountService.h"
#include "PreferenceStrengthModel.h"
#include <QJsonObject>
#include <QVariantList>
#include <array>
#include <optional>
namespace EchoMusic
{
/// Account-bound preference baseline/draft, differential writes and validation.
/// Canonical weight maps come from Rust; Qt never decodes upstream JSON strings.
class ListeningPreferencesService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canReset READ canReset NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int modeIndex READ modeIndex NOTIFY changed)
    Q_PROPERTY(QString modeDescription READ modeDescription NOTIFY changed)
    Q_PROPERTY(EchoMusic::PreferenceStrengthModel *strengths READ strengths CONSTANT)
    Q_PROPERTY(QVariantList languageOptions READ languageOptions NOTIFY changed)
    Q_PROPERTY(QVariantList styleOptions READ styleOptions NOTIFY changed)
    Q_PROPERTY(QVariantList genderOptions READ genderOptions NOTIFY changed)
    Q_PROPERTY(QVariantList ageOptions READ ageOptions NOTIFY changed)
    Q_PROPERTY(int genderIndex READ genderIndex NOTIFY changed)
    Q_PROPERTY(int ageIndex READ ageIndex NOTIFY changed)
    Q_PROPERTY(QString languagesHint READ languagesHint NOTIFY changed)
    Q_PROPERTY(QString stylesHint READ stylesHint NOTIFY changed)
    Q_PROPERTY(bool recommendationEditable READ recommendationEditable NOTIFY changed)
public:
    explicit ListeningPreferencesService(ApiClient &api, AccountService &account, QObject *parent);
    ~ListeningPreferencesService() override;
    bool loaded() const
    {
        return m_loaded;
    }
    bool loading() const
    {
        return m_read != 0;
    }
    bool saving() const
    {
        return m_save != 0;
    }
    bool dirty() const;
    bool canReset() const;
    QString error() const
    {
        return m_error;
    }
    int modeIndex() const;
    QString modeDescription() const;
    PreferenceStrengthModel *strengths()
    {
        return &m_strengths;
    }
    QVariantList languageOptions() const;
    QVariantList styleOptions() const;
    QVariantList genderOptions() const;
    QVariantList ageOptions() const;
    int genderIndex() const;
    int ageIndex() const;
    QString languagesHint() const;
    QString stylesHint() const;
    bool recommendationEditable() const
    {
        return m_draft.weights[0].values.has_value();
    }
    Q_INVOKABLE void load();
    Q_INVOKABLE void setMode(int index);
    Q_INVOKABLE void setStrength(const QString &id, double value);
    Q_INVOKABLE void toggleLanguage(const QString &id);
    Q_INVOKABLE void toggleStyle(const QString &id);
    Q_INVOKABLE void setGender(int index);
    Q_INVOKABLE void setAge(int index);
    Q_INVOKABLE void resetRecommendation();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void save();
    void shutdown();
signals:
    void changed();
    void saved();

private:
    friend class ListeningPreferencesTestAccess;
    struct Weight
    {
        QString encoded;
        std::optional<QJsonObject> values;
    };
    struct Snapshot
    {
        QString mode, gender, age;
        std::array<Weight, 3> weights;
    };
    bool current() const;
    bool editable() const
    {
        return current() && m_loaded && !loading() && !saving();
    }
    void clear();
    void succeeded(quint64 id, const QByteArray &bytes);
    void failed(quint64 id, const QByteArray &payload = {});
    void toggle(int field, const QString &id);
    QVariantList options(int field) const;
    QString hint(int field) const;
    static Snapshot reset(Snapshot value);
    static ApiClient::PreferencePatch changes(const Snapshot &before, const Snapshot &after);
    QVariantList strengthRows() const;
    PreferenceStrengthModel m_strengths;
    ApiClient &m_api;
    AccountService &m_account;
    quint64 m_revision = 0, m_read = 0, m_save = 0;
    bool m_loaded = false, m_closing = false;
    QString m_error;
    Snapshot m_baseline, m_draft, m_submitted;
};
} // namespace EchoMusic
