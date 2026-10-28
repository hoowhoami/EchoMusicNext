#include "CommentsController.h"
#include <QJsonDocument>
#include <algorithm>
namespace EchoMusic
{
void CommentsModel::clearProjection()
{
    if (m_rows.isEmpty())
        return;
    // Row removal lets ListView retire pending asynchronous delegates while
    // their indices are still known. A model reset clears DelegateModel first
    // and can leave the view trying to cancel an already-removed index.
    beginRemoveRows({}, 0, int(m_rows.size() - 1));
    m_rows.clear();
    endRemoveRows();
}
QVariant CommentsModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() < 0 || i.row() >= m_rows.size())
        return {};
    const auto &row = m_rows[i.row()];
    switch (role) {
    case Entry: {
        const auto value = row.value("entry");
        return value.metaType() == QMetaType::fromType<QJsonObject>()
                   ? value.value<QJsonObject>().toVariantMap()
                   : value;
    }
    case Kind:
        return row.value("kind");
    case RootId:
        return row.value("rootId");
    default:
        return {};
    }
}
QVariantMap CommentsModel::geometryEntry(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    const auto value = m_rows[row].value("entry");
    const bool json = value.metaType() == QMetaType::fromType<QJsonObject>();
    const auto object = json ? value.value<QJsonObject>() : QJsonObject{};
    const auto map = json ? QVariantMap{} : value.toMap();
    QVariantMap result;
    for (const auto *name :
         {"id", "body", "content", "content_expanded", "quote_name", "quote_content", "user_id",
          "thread_expanded", "reply_open", "reply_first", "reply_nested", "loading", "error",
          "hasMore", "empty"}) {
        if (json && object.contains(name))
            result.insert(name, object.value(name).toVariant());
        else if (!json && map.contains(name))
            result.insert(name, map.value(name));
    }
    return result;
}
void CommentsModel::replace(QList<QVariantMap> rows)
{
    auto identity = [](const QVariantMap &row) {
        return row.value("kind").toString() + "/" + row.value("rootId").toString() + "/" +
               (row.value("entry").metaType() == QMetaType::fromType<QJsonObject>()
                    ? row.value("entry").value<QJsonObject>().value("id").toString()
                    : row.value("entry").toMap().value("id").toString());
    };
    bool same = rows.size() == m_rows.size();
    if (same)
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (identity(rows[i]) != identity(m_rows[i])) {
                same = false;
                break;
            }
    if (same) {
        for (qsizetype i = 0; i < rows.size(); ++i)
            if (rows[i] != m_rows[i]) {
                m_rows[i] = std::move(rows[i]);
                emit dataChanged(index(int(i)), index(int(i)));
            }
        return;
    }
    qsizetype prefix = 0, suffix = 0;
    // Payload changes do not change a row's identity. Preserve the native
    // delegate/scroll anchor when metadata arrives together with inserted rows.
    while (prefix < rows.size() && prefix < m_rows.size() &&
           identity(rows[prefix]) == identity(m_rows[prefix]))
        ++prefix;
    while (suffix < rows.size() - prefix && suffix < m_rows.size() - prefix &&
           identity(rows[rows.size() - 1 - suffix]) == identity(m_rows[m_rows.size() - 1 - suffix]))
        ++suffix;
    const auto removed = m_rows.size() - prefix - suffix;
    if (removed) {
        beginRemoveRows({}, int(prefix), int(prefix + removed - 1));
        m_rows.remove(prefix, removed);
        endRemoveRows();
    }
    const auto inserted = rows.size() - prefix - suffix;
    if (inserted) {
        beginInsertRows({}, int(prefix), int(prefix + inserted - 1));
        for (qsizetype i = 0; i < inserted; ++i)
            m_rows.insert(prefix + i, std::move(rows[prefix + i]));
        endInsertRows();
    }
    const auto update = [&](qsizetype i) {
        if (m_rows[i] != rows[i]) {
            m_rows[i] = std::move(rows[i]);
            emit dataChanged(index(int(i)), index(int(i)));
        }
    };
    for (qsizetype i = 0; i < prefix; ++i)
        update(i);
    for (qsizetype i = rows.size() - suffix; i < rows.size(); ++i)
        update(i);
}
CommentsController::CommentsController(ApiClient &a, RouteContext &r, bool network, QObject *p,
                                       CommentAuthorService *authors)
    : CommentsController(a, &r, network, p, authors)
{
}
CommentsController::CommentsController(ApiClient &a, bool network, QObject *p,
                                       CommentAuthorService *authors)
    : CommentsController(a, nullptr, network, p, authors)
{
    m_presented = false;
}
CommentsController::CommentsController(ApiClient &a, RouteContext *r, bool network, QObject *p,
                                       CommentAuthorService *authors)
    : QObject(p), m_authors(authors), m_api(a), m_route(r), m_model(this), m_network(network)
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    if (m_authors)
        connect(m_authors, &CommentAuthorService::changed, this, [this] {
            rebuild();
            finishInitialIfReady();
            emit changed();
        });
    connect(&a, &ApiClient::succeeded, this, &CommentsController::accept);
    connect(&a, &ApiClient::failed, this,
            [this](quint64 id, int, const QByteArray &b) { fail(id, b); });
    connect(&a, &ApiClient::sessionChanged, this, [this] { clear(); });
    connect(&a, &ApiClient::closed, this, &CommentsController::shutdown);
}
CommentsController::~CommentsController()
{
    shutdown();
}
CommentsController::Stream &CommentsController::stream(const QString &key)
{
    auto &s = m_streams[key];
    if (!s)
        s = std::make_shared<Stream>();
    return *s;
}
QString CommentsController::currentKey() const
{
    return m_tab == 1 ? "category:" + m_category : m_tab == 2 ? "word:" + m_word : "all";
}
const CommentsController::Stream *CommentsController::current() const
{
    return m_streams.value(currentKey()).get();
}
QString CommentsController::selection() const
{
    return m_tab == 1 ? m_category : m_tab == 2 ? m_word : QString();
}
bool CommentsController::loading() const
{
    return (currentKey() == "all" && m_waitingAuthors) || fetching();
}
bool CommentsController::hasComments() const
{
    const auto *s = current();
    return s && !s->rows.isEmpty();
}
bool CommentsController::fetching() const
{
    const auto k = currentKey();
    for (const auto &p : m_pending)
        if (p.operation == Operation::List && p.stream == k)
            return true;
    return false;
}
bool CommentsController::hasMore() const
{
    auto *s = current();
    return s && s->more;
}
bool CommentsController::busy() const
{
    for (const auto &p : m_pending)
        if (p.operation != Operation::List)
            return true;
    return false;
}
bool CommentsController::signedIn() const
{
    return m_api.currentAccountUserId() != 0;
}
QString CommentsController::errorMessage() const
{
    auto *s = current();
    return s ? s->error : QString();
}
QString CommentsController::totalText() const
{
    auto *s = current();
    return s && !s->total.isNull() && !s->total.isUndefined() ? s->total.toVariant().toString()
                                                              : QStringLiteral("—");
}
void CommentsController::setDraft(const QString &v)
{
    if (m_draft == v)
        return;
    m_draft = v;
    emit changed();
}
void CommentsController::configure(ApiClient::CommentResource type, const QString &id,
                                   const QString &name, quint64 mix)
{
    if (type != m_resource || id != m_id || mix != m_mix) {
        clear();
        m_resource = type;
        m_id = id;
        m_mix = mix;
    }
    m_name = name;
}
void CommentsController::cancel()
{
    ++m_epoch;
    const auto ids = m_pending.keys();
    m_pending.clear();
    for (auto id : ids)
        m_api.cancel(id);
}
void CommentsController::suspend()
{
    cancel();
    m_started = false;
    emit changed();
}
void CommentsController::clear()
{
    cancel();
    m_model.clearProjection();
    m_initialSettled = m_initialCompleted = m_waitingAuthors = false;
    m_streams.clear();
    m_threadTargets.clear();
    m_expandedContents.clear();
    emit scopeReset();
    m_categories = {};
    m_words = {};
    m_category.clear();
    m_word.clear();
    m_draft.clear();
    m_replyTarget.clear();
    m_replyRoot.clear();
    m_replyDraft.clear();
    ++m_replyRevision;
    m_started = false;
    rebuild();
    emit changed();
}
void CommentsController::shutdown()
{
    if (m_closed)
        return;
    m_closed = true;
    cancel();
    disconnect(&m_api, nullptr, this, nullptr);
}
void CommentsController::setPresented(bool value)
{
    m_presented = value;
    if (value)
        start();
    else
        suspend();
}
void CommentsController::start()
{
    if (m_closed || m_started || !active() || m_id.isEmpty())
        return;
    m_started = true;
    if (stream("all").loaded)
        finishInitialIfReady();
    if (!stream("all").loaded)
        request("all", true);
    if (m_tab && stream("all").loaded && !stream(currentKey()).loaded)
        request(currentKey(), true);
    emit changed();
}
void CommentsController::setTab(int t)
{
    if (t < 0 || t > 2 || m_tab == t)
        return;
    m_model.clearProjection();
    m_tab = t;
    if (m_started && !stream(currentKey()).loaded)
        request(currentKey(), true);
    rebuild();
    emit changed();
}
void CommentsController::select(const QString &value)
{
    if (!m_tab || selection() == value)
        return;
    m_model.clearProjection();
    if (m_tab == 1)
        m_category = value;
    else
        m_word = value;
    if (m_started && !stream(currentKey()).loaded)
        request(currentKey(), true);
    rebuild();
    emit changed();
}
void CommentsController::reload()
{
    request(currentKey(), true);
}
void CommentsController::loadMore()
{
    request(currentKey(), false);
}
void CommentsController::request(const QString &key, bool reset)
{
    if (m_closed || !m_network || !m_started || !active() || m_id.isEmpty())
        return;
    if ((key.startsWith("category:") && key.mid(9).isEmpty()) ||
        (key.startsWith("word:") && key.mid(5).isEmpty()))
        return;
    auto &s = stream(key);
    if (reset) {
        if (key == currentKey())
            m_model.clearProjection();
        ++s.revision;
        const auto ids = m_pending.keys();
        for (auto id : ids)
            if (m_pending[id].stream == key && m_pending[id].operation == Operation::List) {
                m_pending.remove(id);
                m_api.cancel(id);
            }
        s.rows = {};
        s.singerRows = {};
        s.next = 1;
        s.more = true;
        s.total = {};
        s.loaded = false;
    } else {
        if (!s.more)
            return;
        for (const auto &p : m_pending)
            if (p.stream == key && p.operation == Operation::List)
                return;
    }
    s.error.clear();
    ApiClient::CommentQuery q;
    q.resource = m_resource;
    q.id = m_id;
    q.page = s.next;
    q.mixSongId = m_mix;
    if (key.startsWith("category:")) {
        q.section = ApiClient::CommentSection::Category;
        q.categoryId = key.mid(9).toULongLong();
    } else if (key.startsWith("word:")) {
        q.section = ApiClient::CommentSection::HotWord;
        q.hotWord = key.mid(5);
    } else if (key.startsWith("thread:")) {
        const auto root = m_threadTargets.value(key);
        q.section = ApiClient::CommentSection::Floor;
        q.code = root.value("code").toString();
        q.id = root.value("resource_id").toString();
        q.threadId = root.value("thread_id").toString();
        if (q.threadId.isEmpty() || q.threadId == "0")
            q.threadId = root.value("parent_id").toString().toULongLong() > 0
                             ? root.value("parent_id").toString()
                             : root.value("id").toString();
    }
    const auto id = m_api.comments(q);
    if (id)
        m_pending.insert(
            id, {Operation::List, key, {}, m_epoch, s.revision, m_api.sessionRevision(), s.next});
    else
        s.error = tr("评论请求未能启动");
    rebuild();
    emit changed();
}
void CommentsController::accept(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    const auto p = m_pending.take(id);
    if (m_closed || p.epoch != m_epoch || p.session != m_api.sessionRevision() || !active())
        return;
    if (p.operation != Operation::List) {
        if (p.operation == Operation::Send && m_draft == p.draft)
            m_draft.clear();
        emit notice(p.operation == Operation::Delete  ? tr("评论已删除")
                    : p.operation == Operation::Reply ? tr("回复已提交，展示结果以平台审核为准")
                                                      : tr("已提交，展示结果以平台审核为准"),
                    false);
        if (p.operation == Operation::Reply && p.revision == m_replyRevision) {
            m_replyTarget.clear();
            m_replyRoot.clear();
            if (m_replyDraft == p.draft)
                m_replyDraft.clear();
            loadThread(p.stream, true);
        } else if (p.operation == Operation::Delete && !p.stream.isEmpty() &&
                   m_streams.contains(threadKey(p.stream))) {
            loadThread(p.stream, true);
        } else {
            request(currentKey(), true);
        }
        emit submitted(p.operation == Operation::Reply);
        rebuild();
        emit changed();
        return;
    }
    auto &s = stream(p.stream);
    if (p.revision != s.revision)
        return;
    const auto v = QJsonDocument::fromJson(bytes).object();
    const auto rows = v.value("comments").toArray();
    QHash<QString, qsizetype> positions;
    for (qsizetype index = 0; index < s.rows.size(); ++index) {
        const auto key = s.rows[index].toObject().value("id").toString();
        if (!key.isEmpty())
            positions.insert(key, index);
    }
    const auto previousCount = s.rows.size();
    for (const auto &row : rows) {
        const auto key = row.toObject().value("id").toString();
        if (!key.isEmpty() && positions.contains(key))
            s.rows.replace(positions.value(key), row);
        else {
            if (!key.isEmpty())
                positions.insert(key, s.rows.size());
            s.rows.append(row);
        }
    }
    if (m_authors)
        m_authors->ensure(rows);
    if (p.stream == "all" && p.page == 1) {
        // The separate collection does not affect the ordinary page count.
        s.singerRows = v.value("singer_comments").toArray();
        if (m_authors)
            m_authors->ensure(s.singerRows);
    }
    s.total = v.value(p.stream.startsWith("thread:") ? "reply_total" : "total");
    // A total is optional. A nonempty short page can precede another page when
    // the service supplies a larger total (observed category page has 20/72).
    const auto count = s.rows.size();
    s.more = s.rows.size() > previousCount &&
             (!s.total.isNull() && !s.total.isUndefined() ? count < s.total.toVariant().toLongLong()
                                                          : rows.size() >= 30);
    s.loaded = true;
    s.next = p.page + 1;
    s.error.clear();
    if (rows.isEmpty() && v.value("service_code").toInteger() != 0 &&
        p.stream.startsWith("thread:"))
        s.error = tr("楼层评论暂不可用");
    if (p.stream == "all" && p.page == 1) {
        m_categories = v.value("categories").toArray();
        m_words = v.value("hot_words").toArray();
        if (m_category.isEmpty() && !m_categories.isEmpty())
            m_category = m_categories[0].toObject().value("id").toVariant().toString();
        if (m_word.isEmpty() && !m_words.isEmpty())
            m_word = m_words[0].toObject().value("content").toString();
        m_initialSettled = true;
        finishInitialIfReady();
    }
    rebuild();
    emit changed();
}
void CommentsController::fail(quint64 id, const QByteArray &bytes)
{
    if (!m_pending.contains(id))
        return;
    auto p = m_pending.take(id);
    if (p.epoch != m_epoch || p.session != m_api.sessionRevision() || m_closed)
        return;
    QString error = QJsonDocument::fromJson(bytes).object().value("message").toString();
    if (error.isEmpty())
        error = tr("评论加载失败，请重试");
    if (p.operation == Operation::List) {
        stream(p.stream).error = error;
        emit readFailed(p.stream.startsWith("thread:") ? tr("楼层评论加载失败")
                                                       : tr("评论加载失败"));
        if (p.stream == "all" && p.page == 1) {
            m_initialSettled = true;
            finishInitialIfReady();
        }
    } else
        emit notice(error, true);
    rebuild();
    emit changed();
}
void CommentsController::finishInitialIfReady()
{
    if (m_closed || !m_started || !active() || !m_initialSettled || m_initialCompleted)
        return;
    const auto &all = stream("all");
    if (m_authors && (!m_authors->readyFor(all.rows) || !m_authors->readyFor(all.singerRows))) {
        m_waitingAuthors = true;
        return;
    }
    m_waitingAuthors = false;
    m_initialCompleted = true;
    emit initialFinished();
    if (m_tab && !stream(currentKey()).loaded)
        request(currentKey(), true);
}
QVariantMap CommentsController::comment(const QString &id) const
{
    for (const auto &s : m_streams)
        for (const auto &collection : {s->rows, s->singerRows})
            for (const auto &r : collection) {
                const auto v = r.toObject();
                if (v.value("id").toString() == id)
                    return v.toVariantMap();
            }
    return {};
}
QString CommentsController::threadKey(const QString &id) const
{
    const auto value = comment(id);
    const auto tid = value.value("thread_id").toString();
    const auto pid = value.value("parent_id").toString();
    return "thread:" + (tid.toULongLong() > 0 && tid != id ? tid
                        : pid.toULongLong() > 0            ? pid
                                                           : id);
}
void CommentsController::rememberThread(const QString &id)
{
    m_threadTargets.insert(threadKey(id), comment(id));
}
void CommentsController::toggleThread(const QString &id)
{
    rememberThread(id);
    const auto key = threadKey(id);
    auto &s = stream(key);
    s.expanded = !s.expanded;
    if (s.expanded && !s.loaded)
        request(key, true);
    rebuild();
    emit changed();
}
void CommentsController::loadThread(const QString &id, bool reset)
{
    rememberThread(id);
    request(threadKey(id), reset);
}
void CommentsController::toggleContent(const QString &id)
{
    if (id.isEmpty() || comment(id).isEmpty())
        return;
    const auto key = currentKey() + "/" + id;
    if (m_expandedContents.contains(key))
        m_expandedContents.remove(key);
    else
        m_expandedContents.insert(key);
    rebuild();
    emit changed();
}
void CommentsController::rebuild()
{
    QList<QVariantMap> rows;
    const auto *s = current();
    if (!s) {
        m_model.replace({});
        return;
    }
    QHash<QString, QJsonObject> byId;
    for (const auto &collection : {s->singerRows, s->rows})
        for (const auto &value : collection) {
            auto entry = value.toObject();
            byId.insert(entry.value("id").toString(), entry);
        }
    auto parentId = [](const QJsonObject &value) {
        auto id = value.value("id").toString();
        auto tid = value.value("thread_id").toString();
        if (tid.toULongLong() > 0 && tid != id)
            return tid;
        auto pid = value.value("parent_id").toString();
        return pid.toULongLong() > 0 ? pid : QString();
    };
    const auto keyFor = [&](const QJsonObject &value) {
        const auto parent = parentId(value);
        return "thread:" + (parent.isEmpty() ? value.value("id").toString() : parent);
    };
    QHash<QString, QJsonArray> grouped;
    QJsonArray roots;
    for (const auto &value : s->rows) {
        auto entry = value.toObject();
        auto parent = parentId(entry);
        QString root;
        QSet<QString> visited{entry.value("id").toString()};
        while (!parent.isEmpty() && !visited.contains(parent)) {
            visited.insert(parent);
            if (!byId.contains(parent))
                break;
            const auto next = parentId(byId[parent]);
            if (next.isEmpty()) {
                root = parent;
                break;
            }
            parent = next;
        }
        if (root.isEmpty())
            roots.append(entry);
        else
            grouped[root].append(entry);
    }
    auto append = [&](QJsonObject value, const QString &kind, const QString &root,
                      const QJsonArray &siblings, bool firstReply = false) {
        auto entry = value;
        if (m_authors)
            entry.insert("products", m_authors->products(entry.value("user_id").toString()));
        QString body = value.value("content").toString();
        QString quoteName, quoteContent;
        const auto pid = value.value("parent_id").toString();
        for (const auto &candidate : siblings) {
            auto sibling = candidate.toObject();
            if (pid.toULongLong() > 0 && pid == sibling.value("id").toString() &&
                pid != value.value("id").toString()) {
                quoteName = sibling.value("user_name").toString();
                quoteContent = sibling.value("content").toString();
                break;
            }
        }
        if (value.value("reply_code").toInt() == 1 || pid.toULongLong() > 0 ||
            value.value("original_id").toString().toULongLong() > 0) {
            const auto marker = body.indexOf("//@");
            if (marker >= 0) {
                const auto rest = body.mid(marker + 3);
                auto colon = rest.indexOf(':');
                if (colon < 0)
                    colon = rest.indexOf(QChar(0xff1a));
                if (colon >= 0) {
                    if (quoteName.isEmpty()) {
                        quoteName = rest.left(colon);
                        quoteContent = rest.mid(colon + 1);
                    }
                    body = body.left(marker).trimmed();
                }
            }
        }
        entry.insert("body", body);
        entry.insert("content_expanded", m_expandedContents.contains(currentKey() + "/" +
                                                                     entry.value("id").toString()));
        entry.insert("quote_name", quoteName);
        entry.insert("quote_content", quoteContent);
        const auto thread = m_streams.value(keyFor(byId.value(root)));
        entry.insert("thread_expanded", thread && thread->expanded);
        entry.insert("reply_first", firstReply);
        const bool replyOpen =
            !m_replyTarget.isEmpty() &&
            m_replyTarget.value("id").toString() == entry.value("id").toString() &&
            m_replyRoot == root;
        entry.insert("reply_open", replyOpen);
        const auto context = parentId(value);
        entry.insert("context_reply", !context.isEmpty());
        rows.append({{"entry", QVariant::fromValue(entry)}, {"kind", kind}, {"rootId", root}});
        if (replyOpen) {
            auto target = m_replyTarget;
            target.insert("reply_nested", kind == "reply");
            target.insert("thread_expanded", thread && thread->expanded);
            rows.append({{"entry", target}, {"kind", "composer"}, {"rootId", root}});
        }
    };
    // Singer entries precede the ordinary stream, without participating in
    // its total, page size or hasMore. Both sections stay in the virtual view.
    const auto singers = m_tab == 0 ? s->singerRows : QJsonArray{};
    if (!singers.isEmpty())
        rows.append({{"entry", QVariantMap{}}, {"kind", "singerHeading"}, {"rootId", ""}});
    auto presentedRoots = singers;
    for (const auto &value : roots)
        presentedRoots.append(value);
    for (qsizetype i = 0; i < presentedRoots.size(); ++i) {
        if (!singers.isEmpty() && i == singers.size())
            rows.append({{"entry", QVariantMap{}}, {"kind", "singerDivider"}, {"rootId", ""}});
        const auto value = presentedRoots[i];
        auto entry = value.toObject();
        const auto id = entry.value("id").toString();
        append(entry, i < singers.size() ? "singerComment" : "comment", id, {});
        const auto f = m_streams.value(keyFor(entry));
        if (!f || !f->expanded)
            continue;
        QJsonArray replies = f->rows;
        QSet<QString> seen, originals;
        for (const auto &v : replies) {
            auto r = v.toObject();
            seen.insert(r.value("id").toString());
            originals.insert(r.value("original_id").toString());
        }
        auto previews = grouped.value(id);
        for (const auto &v : entry.value("previews").toArray())
            previews.append(v);
        for (const auto &v : previews) {
            auto r = v.toObject();
            auto rid = r.value("id").toString();
            if (!seen.contains(rid) && !originals.contains(rid)) {
                replies.append(r);
                seen.insert(rid);
            }
        }
        // Display floor times as Beijing time. ISO-shaped timestamps sort lexically;
        // equal or unknown dates retain provider order.
        QList<QJsonObject> ordered;
        for (const auto &v : replies)
            ordered.append(v.toObject());
        std::stable_sort(
            ordered.begin(), ordered.end(), [](const QJsonObject &a, const QJsonObject &b) {
                auto x = a.value("created_at").toString(), y = b.value("created_at").toString();
                return !x.isEmpty() && (y.isEmpty() || x < y);
            });
        auto total =
            !f->total.isNull() && !f->total.isUndefined() ? f->total : entry.value("reply_count");
        const auto rootTotal = entry.value("reply_count");
        if (rootTotal.isDouble() && (!total.isDouble() || rootTotal.toDouble() > total.toDouble()))
            total = rootTotal;
        rows.append({{"entry", QVariantMap{{"count", total.isNull() || total.isUndefined()
                                                         ? QVariant{}
                                                         : total.toVariant()},
                                           {"context", !parentId(entry).isEmpty()}}},
                     {"kind", "threadHeading"},
                     {"rootId", id}});
        for (qsizetype replyIndex = 0; replyIndex < ordered.size(); ++replyIndex)
            append(ordered[replyIndex], "reply", id, replies, replyIndex == 0);
        bool pending = false;
        for (const auto &p : m_pending)
            if (p.stream == keyFor(entry))
                pending = true;
        rows.append({{"entry", QVariantMap{{"loading", pending},
                                           {"error", f->error},
                                           {"hasMore", f->more},
                                           {"empty", replies.isEmpty()}}},
                     {"kind", "threadFooter"},
                     {"rootId", id}});
    }
    if (!singers.isEmpty() && roots.isEmpty())
        rows.append({{"entry", QVariantMap{}}, {"kind", "singerDivider"}, {"rootId", ""}});
    m_model.replace(std::move(rows));
}
void CommentsController::setReplyDraft(const QString &value)
{
    if (value == m_replyDraft)
        return;
    m_replyDraft = value;
    emit changed();
}
void CommentsController::beginReply(const QVariantMap &target, const QString &rootId)
{
    if (busy() || !active())
        return;
    ++m_replyRevision;
    m_replyTarget = target;
    m_replyRoot = rootId;
    m_replyDraft.clear();
    rebuild();
    emit changed();
}
void CommentsController::closeReply()
{
    if (busy())
        return;
    ++m_replyRevision;
    m_replyTarget.clear();
    m_replyRoot.clear();
    m_replyDraft.clear();
    rebuild();
    emit changed();
}
void CommentsController::submitReply()
{
    reply(m_replyTarget, m_replyRoot, m_replyDraft);
}
bool CommentsController::canDelete(const QVariantMap &v) const
{
    return signedIn() &&
           v.value("user_id").toString() == QString::number(m_api.currentAccountUserId());
}
ApiClient::CommentWrite CommentsController::write(const QVariantMap &v, const QString &rootId) const
{
    ApiClient::CommentWrite w;
    w.resource = m_resource;
    w.id = v.value("resource_id").toString();
    w.name = v.value("resource_name").toString();
    w.code = v.value("code").toString();
    w.commentId = v.value("id").toString();
    w.mixSongId = m_mix;
    if (!rootId.isEmpty()) {
        auto root = comment(rootId);
        w.threadId = root.value("thread_id").toString();
        if (w.threadId.isEmpty() || w.threadId == "0")
            w.threadId = root.value("parent_id").toString().toULongLong() > 0
                             ? root.value("parent_id").toString()
                             : rootId;
        if (w.commentId != rootId) {
            w.parentId = w.commentId;
            w.replyUserName = v.value("user_name").toString();
            w.replyContent = v.value("content").toString();
        }
    }
    return w;
}
void CommentsController::mutation(quint64 id, Operation op, const QString &draft,
                                  const QString &rootId)
{
    if (id)
        m_pending.insert(id, {op, op == Operation::Reply ? m_replyRoot : rootId, draft, m_epoch,
                              op == Operation::Reply ? m_replyRevision : 0, m_api.sessionRevision(),
                              0});
    else
        emit notice(tr("评论请求未能启动"), true);
    emit changed();
}
void CommentsController::send()
{
    if (busy() || !active())
        return;
    if (!signedIn()) {
        emit notice(tr("请先登录后再评论"), true);
        return;
    }
    auto content = m_draft.trimmed();
    if (content.isEmpty() || content.toUcs4().size() > 200) {
        emit notice(tr("评论不能为空，且最多 200 字"), true);
        return;
    }
    ApiClient::CommentWrite w;
    w.resource = m_resource;
    w.id = m_id;
    w.name = m_name;
    w.mixSongId = m_mix;
    w.content = content;
    mutation(m_api.sendComment(w), Operation::Send, m_draft);
}
void CommentsController::reply(const QVariantMap &target, const QString &rootId,
                               const QString &content)
{
    if (busy() || !active())
        return;
    if (!signedIn()) {
        emit notice(tr("请先登录后再评论"), true);
        return;
    }
    if (content.trimmed().isEmpty() || content.trimmed().toUcs4().size() > 200) {
        emit notice(tr("评论不能为空，且最多 200 字"), true);
        return;
    }
    auto w = write(target, rootId);
    w.content = content.trimmed();
    mutation(m_api.replyComment(w), Operation::Reply, m_replyDraft);
}
void CommentsController::remove(const QVariantMap &target, const QString &rootId)
{
    if (busy() || !active() || !canDelete(target))
        return;
    auto w = write(target, rootId);
    if (target.value("reply_code").toInt() != 1)
        w.threadId.clear();
    mutation(m_api.deleteComment(w), Operation::Delete, {},
             w.threadId.isEmpty() ? QString() : rootId);
}
} // namespace EchoMusic
