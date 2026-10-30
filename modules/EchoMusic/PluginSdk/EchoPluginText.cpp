#include "EchoPluginText.h"
#include "EchoPluginContext.h"
#include "PlaybackService.h"
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QtConcurrentRun>
#ifdef ECHO_HAS_ICU_SEGMENTATION
#include <unicode/ubrk.h>
#include <unicode/uloc.h>
#include <unicode/uchar.h>
#endif

namespace EchoMusic
{
namespace
{
struct WordResult
{
    QVariantList lines;
    QString error;
};
WordResult segment(const QStringList &texts, const QString &locale,
                   const std::shared_ptr<std::atomic_bool> &canceled)
{
    WordResult result;
#ifdef ECHO_HAS_ICU_SEGMENTATION
    QByteArray localeId;
    if (!locale.isEmpty()) {
        int32_t parsed = 0;
        auto status = U_ZERO_ERROR;
        const auto bytes = locale.toLatin1();
        const auto required = uloc_forLanguageTag(bytes.constData(), nullptr, 0, &parsed, &status);
        if ((U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) || parsed != bytes.size() ||
            required < 0 || required > 4096)
            return {{}, QStringLiteral("Invalid language tag")};
        localeId.resize(required + 1);
        status = U_ZERO_ERROR;
        const auto written = uloc_forLanguageTag(bytes.constData(), localeId.data(),
                                                 localeId.size(), &parsed, &status);
        if (U_FAILURE(status) || written != required || parsed != bytes.size())
            return {{}, QStringLiteral("Invalid language tag")};
        localeId.resize(written);
    }
    auto status = U_ZERO_ERROR;
    const auto close = [](UBreakIterator *value) { ubrk_close(value); };
    const std::unique_ptr<UBreakIterator, decltype(close)> breaker(
        ubrk_open(UBRK_WORD, locale.isEmpty() ? nullptr : localeId.constData(), nullptr, 0,
                  &status),
        close);
    if (U_FAILURE(status) || !breaker)
        return {{}, QStringLiteral("Cannot initialize word breaker")};
    for (const auto &text : texts) {
        if (canceled->load(std::memory_order_relaxed))
            return {};
        status = U_ZERO_ERROR;
        ubrk_setText(breaker.get(), reinterpret_cast<const UChar *>(text.utf16()),
                     int32_t(text.size()), &status);
        if (U_FAILURE(status))
            return {{}, QStringLiteral("Cannot segment input text")};
        QVariantList words;
        int32_t start = ubrk_first(breaker.get());
        for (int32_t end = ubrk_next(breaker.get()); end != UBRK_DONE;
             start = end, end = ubrk_next(breaker.get())) {
            if (canceled->load(std::memory_order_relaxed))
                return {};
            words.append(QVariantMap{
                {"segment", text.mid(start, end - start)},
                {"index", start},
                {"isWordLike", ubrk_getRuleStatus(breaker.get()) >= UBRK_WORD_NONE_LIMIT}});
        }
        result.lines.append(QVariant(words));
    }
#else
    Q_UNUSED(texts)
    Q_UNUSED(locale)
    Q_UNUSED(canceled)
    result.error = QStringLiteral("Word segmentation is unavailable");
#endif
    return result;
}
} // namespace

EchoPluginText::EchoPluginText(EchoPluginContext &scope) : QObject(&scope), m_scope(scope)
{
    connect(&scope, &EchoPluginContext::activeChanged, this, [this] {
        if (!m_scope.active())
            shutdown();
    });
}
bool EchoPluginText::available()
{
#ifdef ECHO_HAS_ICU_SEGMENTATION
    return true;
#else
    return false;
#endif
}
QString EchoPluginText::backend()
{
#ifdef ECHO_HAS_ICU_SEGMENTATION
    UVersionInfo version{};
    char value[U_MAX_VERSION_STRING_LENGTH]{};
    u_getVersion(version);
    u_versionToString(version, value);
    return QStringLiteral("icu4c-") + QString::fromLatin1(value);
#else
    return {};
#endif
}
QString EchoPluginText::unicodeVersion()
{
#ifdef ECHO_HAS_ICU_SEGMENTATION
    UVersionInfo version{};
    char value[U_MAX_VERSION_STRING_LENGTH]{};
    u_getUnicodeVersion(version);
    u_versionToString(version, value);
    return QString::fromLatin1(value);
#else
    return {};
#endif
}
EchoPluginOperation *EchoPluginText::segmentWords(const QStringList &texts, const QString &locale)
{
    if (!m_scope.active() || m_closed)
        return m_scope.rejected("ScopeClosed", "Plugin presentation has ended");
    if (!available())
        return m_scope.rejected("Unavailable", "Compatible ICU word segmentation is unavailable");
    if (texts.size() > 64 ||
        !QRegularExpression(QStringLiteral("^[a-zA-Z0-9-]{0,64}$")).match(locale).hasMatch())
        return m_scope.rejected("InvalidArgument",
                                "At most 64 texts and a language tag are allowed");
    qsizetype units = 0;
    for (const auto &text : texts) {
        units += text.size();
        if (units > 65536)
            return m_scope.rejected("InvalidArgument", "At most 65536 UTF-16 units are allowed");
    }
    if (m_pending >= 2)
        return m_scope.rejected("Busy", "At most two word segmentation requests may be pending");
    auto *operation = m_scope.operation();
    if (operation->finished())
        return operation;
    const auto canceled = std::make_shared<std::atomic_bool>(false);
    operation->start([canceled] { canceled->store(true, std::memory_order_relaxed); });
    const QPointer<EchoPluginOperation> guard(operation);
    const auto epoch = m_scope.accountEpoch();
    const auto revision = m_scope.host().playback.sourceRevision();
    ++m_pending;
    m_cancellations.append(canceled);
    auto *watcher = new QFutureWatcher<WordResult>(this);
    connect(&m_scope.host().playback, &PlaybackService::changed, watcher, [this, guard, revision] {
        if (guard && !guard->finished() && revision != m_scope.host().playback.sourceRevision())
            guard->cancel();
    });
    connect(watcher, &QFutureWatcher<WordResult>::finished, this,
            [this, watcher, guard, canceled, epoch, revision] {
                const auto result = watcher->result();
                watcher->deleteLater();
                m_cancellations.removeOne(canceled);
                --m_pending;
                if (guard && !guard->finished()) {
                    if (m_closed || !m_scope.active() || epoch != m_scope.accountEpoch() ||
                        revision != m_scope.host().playback.sourceRevision())
                        guard->cancel();
                    else if (!result.error.isEmpty())
                        guard->fail("TextSegmentationError", result.error);
                    else
                        guard->succeed(QVariantMap{{"backend", backend()},
                                                   {"unicodeVersion", unicodeVersion()},
                                                   {"lines", result.lines}});
                }
                if (m_pending == 0)
                    emit drained();
            });
    watcher->setFuture(
        QtConcurrent::run([texts, locale, canceled] { return segment(texts, locale, canceled); }));
    return operation;
}
void EchoPluginText::shutdown()
{
    m_closed = true;
    for (const auto &canceled : m_cancellations)
        canceled->store(true, std::memory_order_relaxed);
}
} // namespace EchoMusic
