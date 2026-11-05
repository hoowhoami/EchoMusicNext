#include "ListeningPreferencesService.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <utility>
namespace EchoMusic
{
namespace
{
using Choice = QPair<QString, QString>;
const QList<Choice> recommendation{
    {"L1", "国语"},   {"L2", "英语"}, {"L5", "粤语"}, {"L4", "韩语"}, {"L3", "日语"},
    {"L6", "纯音乐"}, {"S9", "DJ"},   {"T1", "翻唱"}, {"N2", "现场"}, {"S28", "AI 歌曲"}};
const QList<Choice> languages{
    {"1", "国语"},    {"2", "粤语"},  {"4", "英语"},  {"5", "日语"},     {"6", "韩语"},
    {"19", "闽南语"}, {"7", "德语"},  {"8", "法语"},  {"9", "意大利语"}, {"11", "葡萄牙语"},
    {"13", "俄语"},   {"20", "藏语"}, {"21", "泰语"}, {"22", "越南语"},  {"18", "小语种"}};
const QList<Choice> styles{{"1", "流行"},   {"2", "摇滚"}, {"3", "电子"},     {"10", "民谣"},
                           {"6", "嘻哈"},   {"14", "R&B"}, {"21", "国风"},    {"9", "纯音乐"},
                           {"4", "古典乐"}, {"7", "爵士"}, {"5", "舞曲"},     {"12", "乡村"},
                           {"15", "DJ"},    {"16", "ACG"}, {"23", "传统民歌"}};
const QList<Choice> genders{{"", "不设置"}, {"1", "女"}, {"2", "男"}};
const QList<Choice> ages{{"", "不设置"}, {"6", "10后"}, {"7", "05后"},  {"1", "00后"},
                         {"8", "95后"},  {"2", "90后"}, {"9", "85后"},  {"3", "80后"},
                         {"10", "75后"}, {"4", "70后"}, {"11", "65后"}, {"12", "60后"},
                         {"0", "其它"}};
QVariantList choices(const QList<Choice> &list, const QString &current)
{
    QVariantList result;
    bool known = false;
    if (current.isNull())
        result.append(QVariantMap{{"value", QVariant()}, {"label", QStringLiteral("未提供")}});
    for (const auto &c : list) {
        result.append(QVariantMap{{"value", c.first}, {"label", c.second}});
        known |= c.first == current;
    }
    if (!known)
        result.append(
            QVariantMap{{"value", current}, {"label", QStringLiteral("当前设置（其他版本）")}});
    return result;
}
int selection(const QVariantList &list, const QString &value)
{
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toMap().value("value").toString() == value)
            return i;
    return 0;
}
bool hasPatch(const ApiClient::PreferencePatch &p)
{
    return p.mode || p.gender || p.age || p.languages || p.styles || p.recommendation;
}
} // namespace
ListeningPreferencesService::ListeningPreferencesService(ApiClient &api, AccountService &account,
                                                         QObject *parent)
    : QObject(parent), m_strengths(this), m_api(api), m_account(account),
      m_revision(api.sessionRevision())
{
    m_strengths.update(strengthRows());
    connect(this, &ListeningPreferencesService::changed, this,
            [this] { m_strengths.update(strengthRows()); });
    connect(&api, &ApiClient::succeeded, this, &ListeningPreferencesService::succeeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &payload) { failed(id, payload); });
    connect(&api, &ApiClient::sessionChanged, this, [this](quint64 revision) {
        clear();
        m_revision = revision;
        emit changed();
    });
    connect(&account, &AccountService::sessionAdopted, this, [this] {
        clear();
        m_revision = m_api.sessionRevision();
        emit changed();
    });
}
ListeningPreferencesService::~ListeningPreferencesService()
{
    shutdown();
}
bool ListeningPreferencesService::current() const
{
    return !m_closing && m_account.userId() > 0 &&
           m_account.userId() == m_api.currentAccountUserId() &&
           m_revision == m_api.sessionRevision();
}
void ListeningPreferencesService::clear()
{
    auto read = std::exchange(m_read, 0), write = std::exchange(m_save, 0);
    m_loaded = false;
    m_baseline = {};
    m_draft = {};
    m_submitted = {};
    m_error.clear();
    if (read)
        m_api.cancel(read);
    if (write)
        m_api.cancel(write);
}
void ListeningPreferencesService::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    clear();
}
void ListeningPreferencesService::load()
{
    if (!current() || loading() || saving() || dirty())
        return;
    m_error.clear();
    m_read = m_api.listeningPreferences();
    emit changed();
}
ApiClient::PreferencePatch ListeningPreferencesService::changes(const Snapshot &before,
                                                                const Snapshot &after)
{
    ApiClient::PreferencePatch p;
    if (before.mode.isNull() != after.mode.isNull() ||
        (before.mode.isEmpty() ? QString("0") : before.mode) !=
            (after.mode.isEmpty() ? QString("0") : after.mode))
        p.mode = after.mode;
    if (before.gender.isNull() != after.gender.isNull() || before.gender != after.gender)
        p.gender = after.gender;
    if (before.age.isNull() != after.age.isNull() || before.age != after.age)
        p.age = after.age;
    const std::array<std::optional<QString> *, 3> fields{&p.recommendation, &p.languages,
                                                         &p.styles};
    for (int i = 0; i < 3; ++i) {
        const auto &a = before.weights[i], &b = after.weights[i];
        bool equal = a.encoded == b.encoded;
        if (!equal && a.values && b.values) {
            if (i) {
                equal = *a.values == *b.values;
            } else {
                QSet<QString> keys;
                for (auto it = a.values->begin(); it != a.values->end(); ++it)
                    keys.insert(it.key());
                for (auto it = b.values->begin(); it != b.values->end(); ++it)
                    keys.insert(it.key());
                equal = true;
                for (const auto &key : keys)
                    equal &= a.values->value(key).toDouble(50) == b.values->value(key).toDouble(50);
            }
        }
        if (!equal)
            *fields[i] = b.encoded;
    }
    return p;
}
bool ListeningPreferencesService::dirty() const
{
    return m_loaded && hasPatch(changes(m_baseline, m_draft));
}
ListeningPreferencesService::Snapshot ListeningPreferencesService::reset(Snapshot value)
{
    value.mode = "0";
    auto &w = value.weights[0];
    if (w.values) {
        for (const auto &c : recommendation)
            w.values->insert(c.first, 50);
        w.encoded = QString::fromUtf8(QJsonDocument(*w.values).toJson(QJsonDocument::Compact));
    }
    return value;
}
bool ListeningPreferencesService::canReset() const
{
    return m_loaded && hasPatch(changes(m_draft, reset(m_draft)));
}
int ListeningPreferencesService::modeIndex() const
{
    if (m_draft.mode.isNull())
        return -1;
    const auto mode = m_draft.mode.isEmpty() ? QString("0") : m_draft.mode;
    return QStringList{"1", "0", "2"}.indexOf(mode);
}
QString ListeningPreferencesService::modeDescription() const
{
    switch (modeIndex()) {
    case 0:
        return tr("多听熟悉的音乐");
    case 1:
        return tr("在熟悉与新鲜之间平衡");
    case 2:
        return tr("发现更多新鲜音乐");
    default:
        return tr("当前为其他版本的推荐模式，可选择新的模式。");
    }
}
QVariantList ListeningPreferencesService::strengthRows() const
{
    QVariantList result;
    const auto &w = m_draft.weights[0];
    for (const auto &c : recommendation) {
        const int value =
            qRound(qBound(0., w.values ? w.values->value(c.first).toDouble(50) : 50., 100.));
        const QString status = value == 0    ? tr("已屏蔽")
                               : value == 50 ? tr("默认强度")
                               : value < 50  ? tr("减少推荐")
                                             : tr("加大推荐");
        result.append(QVariantMap{{"id", c.first},
                                  {"label", c.second},
                                  {"value", value},
                                  {"status", status},
                                  {"editable", w.values.has_value()}});
    }
    return result;
}
QVariantList ListeningPreferencesService::options(int field) const
{
    QVariantList result;
    const auto &w = m_draft.weights[field];
    for (const auto &c : field == 1 ? languages : styles)
        result.append(
            QVariantMap{{"id", c.first},
                        {"label", c.second},
                        {"selected", w.values && w.values->value(c.first).toDouble() > 50},
                        {"editable", w.values.has_value()}});
    return result;
}
QVariantList ListeningPreferencesService::languageOptions() const
{
    return options(1);
}
QVariantList ListeningPreferencesService::styleOptions() const
{
    return options(2);
}
QVariantList ListeningPreferencesService::genderOptions() const
{
    return choices(genders, m_draft.gender);
}
QVariantList ListeningPreferencesService::ageOptions() const
{
    return choices(ages, m_draft.age);
}
int ListeningPreferencesService::genderIndex() const
{
    return selection(genderOptions(), m_draft.gender);
}
int ListeningPreferencesService::ageIndex() const
{
    return selection(ageOptions(), m_draft.age);
}
QString ListeningPreferencesService::hint(int field) const
{
    const auto &w = m_draft.weights[field];
    if (!w.values)
        return tr("这组偏好暂时无法解析，已保留原值；其他偏好仍可编辑。");
    int count = 0, unknown = w.values->size();
    for (const auto &c : field == 1 ? languages : styles) {
        if (w.values->contains(c.first))
            --unknown;
        if (w.values->value(c.first).toDouble() > 50)
            ++count;
    }
    return unknown ? tr("已选择 %1 项，另保留 %2 个其他版本标签").arg(count).arg(unknown)
                   : tr("已选择 %1 项").arg(count);
}
QString ListeningPreferencesService::languagesHint() const
{
    return hint(1);
}
QString ListeningPreferencesService::stylesHint() const
{
    return hint(2);
}
void ListeningPreferencesService::setMode(int index)
{
    if (!editable() || index < 0 || index > 2)
        return;
    m_draft.mode = QStringList{"1", "0", "2"}[index];
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::setStrength(const QString &id, double value)
{
    if (!editable() || !std::isfinite(value) || !m_draft.weights[0].values)
        return;
    if (std::none_of(recommendation.begin(), recommendation.end(),
                     [&](const auto &c) { return c.first == id; }))
        return;
    auto &w = m_draft.weights[0];
    w.values->insert(id, qRound(qBound(0., value, 100.)));
    w.encoded = QString::fromUtf8(QJsonDocument(*w.values).toJson(QJsonDocument::Compact));
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::toggle(int field, const QString &id)
{
    if (!editable() || !m_draft.weights[field].values)
        return;
    const auto &list = field == 1 ? languages : styles;
    if (std::none_of(list.begin(), list.end(), [&](const auto &c) { return c.first == id; }))
        return;
    auto &w = m_draft.weights[field];
    if (w.values->value(id).toDouble() > 50)
        w.values->remove(id);
    else
        w.values->insert(id, 100);
    w.encoded = w.values->isEmpty()
                    ? QString()
                    : QString::fromUtf8(QJsonDocument(*w.values).toJson(QJsonDocument::Compact));
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::toggleLanguage(const QString &id)
{
    toggle(1, id);
}
void ListeningPreferencesService::toggleStyle(const QString &id)
{
    toggle(2, id);
}
void ListeningPreferencesService::setGender(int index)
{
    const auto list = genderOptions();
    if (!editable() || index < 0 || index >= list.size())
        return;
    m_draft.gender = list[index].toMap()["value"].toString();
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::setAge(int index)
{
    const auto list = ageOptions();
    if (!editable() || index < 0 || index >= list.size())
        return;
    m_draft.age = list[index].toMap()["value"].toString();
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::resetRecommendation()
{
    if (!editable())
        return;
    m_draft = reset(m_draft);
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::discard()
{
    if (saving())
        return;
    m_draft = m_baseline;
    m_error.clear();
    emit changed();
}
void ListeningPreferencesService::save()
{
    if (!editable() || !dirty())
        return;
    const auto patch = changes(m_baseline, m_draft);
    if (patch.recommendation && m_draft.weights[0].values) {
        const auto &w = *m_draft.weights[0].values;
        bool allBlocked = true;
        for (const auto &id : QStringList{"L1", "L2", "L5", "L4", "L3", "L6"})
            allBlocked &= w.value(id).toDouble(50) == 0;
        if (allBlocked)
            m_error = tr("语种不能全部屏蔽，请至少保留一种。");
        else if (w.value("S9").toDouble(50) > 50 && w.value("T1").toDouble(50) == 0)
            m_error = tr("加大 DJ 推荐强度时，请保留翻唱推荐；许多 DJ 歌曲属于翻唱。");
        else
            m_error.clear();
        if (!m_error.isEmpty()) {
            emit changed();
            return;
        }
    }
    m_submitted = m_draft;
    m_error.clear();
    m_save = m_api.updateListeningPreferences(patch);
    if (!m_save)
        m_error = tr("保存失败，已保留你的选择，请重试");
    emit changed();
}
void ListeningPreferencesService::succeeded(quint64 id, const QByteArray &bytes)
{
    if (!id || !current())
        return;
    const auto d = QJsonDocument::fromJson(bytes).object();
    if (id == m_read) {
        m_read = 0;
        Snapshot value;
        value.mode = d.value("recommendation_mode").toString();
        value.gender = d.value("gender").toString();
        value.age = d.value("age").toString();
        const QStringList names{"recommendation", "languages", "styles"};
        for (int i = 0; i < 3; ++i) {
            const auto w = d.value(names[i]).toObject();
            value.weights[i].encoded = w.value("encoded").toString();
            if (w.value("weights").isObject())
                value.weights[i].values = w.value("weights").toObject();
        }
        m_baseline = value;
        m_draft = value;
        m_loaded = true;
        m_error.clear();
        emit changed();
    } else if (id == m_save) {
        m_save = 0;
        m_baseline = m_submitted;
        m_draft = m_submitted;
        m_error.clear();
        const auto revision = m_revision;
        emit changed();
        if (current() && revision == m_revision)
            emit saved();
    }
}
void ListeningPreferencesService::failed(quint64 id, const QByteArray &payload)
{
    if (!id || !current())
        return;
    if (id == m_read) {
        m_read = 0;
        m_error = ApiClient::errorMessage(payload, tr("偏好加载失败，请稍后重试"));
    } else if (id == m_save) {
        m_save = 0;
        m_error = ApiClient::errorMessage(payload, tr("保存失败，已保留你的选择，请重试"));
    } else
        return;
    emit changed();
}
} // namespace EchoMusic
