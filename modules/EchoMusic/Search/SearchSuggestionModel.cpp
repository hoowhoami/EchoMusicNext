#include "SearchSuggestionModel.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>
namespace EchoMusic
{
SearchSuggestionModel::SearchSuggestionModel(ApiClient &api, QObject *parent)
    : QAbstractListModel(parent), m_api(api)
{
    m_debounce.setInterval(280);
    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        if (!m_active || m_closing || m_query.isEmpty())
            return;
        m_requestedQuery = m_query;
        m_request = m_api.searchSuggest(m_query, {AuthenticationMode::CurrentAccount, false});
    });
    connect(&api, &ApiClient::succeeded, this, &SearchSuggestionModel::succeeded);
    connect(&api, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &) { failed(id); });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        if (m_closing)
            return;
        cancelSuggestion();
        const auto id = std::exchange(m_defaultRequest, 0);
        if (id)
            m_api.cancel(id);
        m_defaultKeyword.clear();
        m_promotions.clear();
        replaceRows();
        if (m_started) {
            m_started = false;
            initialize();
        }
        if (m_active && !m_query.isEmpty())
            setQuery(m_query);
        emit changed();
    });
}
int SearchSuggestionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}
QVariant SearchSuggestionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto &row = m_rows.at(index.row());
    if (role == SuggestionText)
        return row.text;
    if (role == GroupTitle)
        return row.group;
    if (role == GroupStart)
        return row.first;
    return {};
}
QHash<int, QByteArray> SearchSuggestionModel::roleNames() const
{
    return {
        {SuggestionText, "suggestionText"}, {GroupTitle, "groupTitle"}, {GroupStart, "groupStart"}};
}
QString SearchSuggestionModel::textAt(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows.at(index).text : QString{};
}
void SearchSuggestionModel::initialize()
{
    if (m_started || m_closing)
        return;
    m_started = true;
    if (m_defaultEnabled)
        m_defaultRequest = m_api.searchDefault({AuthenticationMode::CurrentAccount, false});
}
void SearchSuggestionModel::setDefaultEnabled(bool enabled)
{
    if (m_defaultEnabled == enabled || m_closing)
        return;
    m_defaultEnabled = enabled;
    const auto pending = std::exchange(m_defaultRequest, 0);
    if (pending)
        m_api.cancel(pending);
    m_defaultKeyword.clear();
    m_promotions.clear();
    if (enabled && m_started)
        m_defaultRequest = m_api.searchDefault({AuthenticationMode::CurrentAccount, false});
    emit changed();
}
void SearchSuggestionModel::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    cancelSuggestion();
    const auto id = std::exchange(m_defaultRequest, 0);
    if (id)
        m_api.cancel(id);
}
void SearchSuggestionModel::cancelSuggestion()
{
    m_debounce.stop();
    const auto id = std::exchange(m_request, 0);
    if (id)
        m_api.cancel(id);
    m_loading = false;
}
void SearchSuggestionModel::replaceRows()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}
void SearchSuggestionModel::setActive(bool active)
{
    if (m_active == active || m_closing)
        return;
    m_active = active;
    if (active)
        setQuery(m_query);
    else {
        cancelSuggestion();
        emit changed();
    }
}
void SearchSuggestionModel::setQuery(const QString &query)
{
    if (m_closing)
        return;
    cancelSuggestion();
    m_query = query.trimmed();
    replaceRows();
    if (m_active && !m_query.isEmpty()) {
        m_loading = true;
        m_debounce.start();
    }
    emit changed();
}
void SearchSuggestionModel::succeeded(quint64 id, const QByteArray &payload)
{
    if (m_closing || !id || (id != m_defaultRequest && id != m_request))
        return;
    const auto document = QJsonDocument::fromJson(payload);
    if (id == m_defaultRequest && id) {
        m_defaultRequest = 0;
        const auto promotions = document.object().value("promotions").toArray();
        if (!promotions.isEmpty())
            m_defaultKeyword =
                promotions.first().toObject().value("main_title").toString().trimmed();
        m_promotions.clear();
        for (const auto &value : promotions) {
            const auto item = value.toObject();
            const auto title = item.value("subtitle").toString().trimmed();
            const auto label =
                title.isEmpty() ? item.value("main_title").toString().trimmed() : title;
            if (!label.isEmpty())
                m_promotions.append(label);
        }
        emit changed();
        return;
    }
    if (!id || id != m_request || !m_active || m_requestedQuery != m_query)
        return;
    m_request = 0;
    m_loading = false;
    QList<Row> rows;
    if (document.isObject())
        for (const auto &value : document.object().value("groups").toArray()) {
            const auto group = value.toObject();
            const auto title = group.value("title").toString();
            // EchoMusic's titlebar deliberately excludes MV suggestions while the
            // search page itself still exposes the MV category.
            if (title == QStringLiteral("MV"))
                continue;
            bool first = true;
            for (const auto &entry : group.value("items").toArray()) {
                const auto text = entry.toObject().value("text").toString();
                if (text.isEmpty())
                    continue;
                rows.append({text, title, first});
                first = false;
            }
        }
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit changed();
}
void SearchSuggestionModel::failed(quint64 id)
{
    if (m_closing)
        return;
    if (id && id == m_defaultRequest) {
        m_defaultRequest = 0;
        emit changed();
    }
    if (id && id == m_request) {
        m_request = 0;
        m_loading = false;
        replaceRows();
        emit changed();
    }
}
} // namespace EchoMusic
