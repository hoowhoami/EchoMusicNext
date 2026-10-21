#include "AudioEffectCatalog.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
namespace EchoMusic
{
namespace
{
QString identity(const QVariantMap &effect)
{
    return effect.value("source").toString() + "-" + effect.value("id").toString();
}
bool resource(const QUrl &url)
{
    const auto host = url.host().toLower();
    return url.scheme() == "https" && url.userName().isEmpty() && url.password().isEmpty() &&
           (host == "kugou.com" || host.endsWith(".kugou.com")) && url.port(443) == 443;
}
} // namespace
AudioEffectCatalog::AudioEffectCatalog(ApiClient &api, PlaybackAudioControls &controls,
                                       QObject *parent)
    : QAbstractListModel(parent), m_api(api), m_controls(controls)
{
    connect(&m_installer, &ImpulseResponseInstaller::closed, this, &AudioEffectCatalog::closed);
    connect(&m_installer, &ImpulseResponseInstaller::progress, this,
            [this](quint64 generation, double progress) {
                if (!m_closing && generation == m_downloadGeneration) {
                    m_progress = progress;
                    emit changed();
                }
            });
    connect(&m_installer, &ImpulseResponseInstaller::finished, this,
            [this](quint64 generation, bool success, const QString &, const QString &error) {
                if (!m_closing && generation == m_downloadGeneration)
                    finishDownload(success, error);
            });
    connect(&controls, &PlaybackAudioControls::changed, this, [this] {
        if (rowCount())
            emit dataChanged(index(0), index(rowCount() - 1));
        emit changed();
    });
    m_revision = api.sessionRevision();
    connect(&api, &ApiClient::succeeded, this, &AudioEffectCatalog::succeeded);
    connect(&api, &ApiClient::failed, this, [this](quint64 id, int, const QByteArray &) {
        if (id != m_request && id != m_common)
            return;
        if (id == m_request)
            m_request = 0;
        if (id == m_common)
            m_common = 0;
        m_error = tr("音效加载失败，请重试");
        emit changed();
    });
    connect(&api, &ApiClient::sessionChanged, this, [this] {
        cancel();
        m_revision = m_api.sessionRevision();
        beginResetModel();
        m_rows.clear();
        endResetModel();
        m_commonEffect = {};
        m_page = 1;
        m_total = 0;
        m_more = true;
        m_brand = 0;
        m_brandName.clear();
        emit changed();
    });
}
QVariant AudioEffectCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() ||
        role != Qt::UserRole + 1)
        return {};
    return decorate(m_rows[index.row()]);
}
QVariantMap AudioEffectCatalog::decorate(QVariantMap effect) const
{
    if (effect.isEmpty())
        return effect;
    const auto path = m_controls.downloadedImpulseResponse(identity(effect));
    effect.insert("downloaded", !path.isEmpty());
    effect.insert("active", !path.isEmpty() && m_controls.impulseEnabled() &&
                                m_controls.impulseResponse() == path);
    return effect;
}
void AudioEffectCatalog::cancel(bool download)
{
    for (auto *id : {&m_request, &m_common}) {
        if (*id)
            m_api.cancel(*id);
        *id = 0;
    }
    if (!download)
        return;
    ++m_downloadGeneration;
    m_installer.cancel();
    m_downloading = {};
    m_urls.clear();
    m_progress = 0;
}
void AudioEffectCatalog::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    cancel();
    m_installer.shutdown();
}
void AudioEffectCatalog::selectCategory(const QString &category)
{
    if (!QStringList{"artist", "headphone", "market"}.contains(category) || m_closing)
        return;
    if (m_category == category && !m_rows.isEmpty())
        return;
    m_category = category;
    m_brand = 0;
    m_brandName.clear();
    refresh();
}
void AudioEffectCatalog::setSort(int sort)
{
    if (sort < 2 || sort > 4 || m_sort == sort)
        return;
    m_sort = sort;
    if (m_category == "market")
        refresh();
}
void AudioEffectCatalog::selectBrand(int row)
{
    if (!brands() || row < 0 || row >= m_rows.size() ||
        m_rows[row].value("source").toString() != "brand")
        return;
    m_brand = m_rows[row].value("id").toULongLong();
    m_brandName = m_rows[row].value("name").toString();
    refresh();
}
void AudioEffectCatalog::allBrands()
{
    m_brand = 0;
    m_brandName.clear();
    refresh();
}
void AudioEffectCatalog::refresh()
{
    if (m_closing)
        return;
    cancel(false);
    beginResetModel();
    m_rows.clear();
    endResetModel();
    m_commonEffect = {};
    m_page = 1;
    m_total = 0;
    m_more = true;
    m_error.clear();
    m_revision = m_api.sessionRevision();
    requestPage();
    if (brands())
        m_common = m_api.commonHeadphoneEffect();
    emit changed();
}
void AudioEffectCatalog::loadMore()
{
    if (!m_closing && !loading() && m_more && m_rows.size() < 10000)
        requestPage();
}
void AudioEffectCatalog::requestPage()
{
    const auto category = m_category == "artist"   ? ApiClient::EffectCategory::Artist
                          : m_category == "market" ? ApiClient::EffectCategory::Market
                          : brands()               ? ApiClient::EffectCategory::HeadphoneBrands
                                                   : ApiClient::EffectCategory::HeadphoneModels;
    m_request = m_api.audioEffects(category, quint32(m_page), 20, quint32(m_sort), m_brand);
    if (!m_request)
        m_error = tr("无法发起音效请求");
    emit changed();
}
void AudioEffectCatalog::succeeded(quint64 id, const QByteArray &bytes)
{
    if (m_closing || m_revision != m_api.sessionRevision() || (id != m_request && id != m_common))
        return;
    const auto document = QJsonDocument::fromJson(bytes);
    if (id == m_common) {
        m_common = 0;
        if (document.isObject()) {
            m_commonEffect = document.object().toVariantMap();
        }
        emit changed();
        return;
    }
    m_request = 0;
    const auto page = document.object();
    const auto items = page.value("items").toArray();
    if (!page.value("items").isArray()) {
        m_error = tr("音效响应不完整");
        emit changed();
        return;
    }
    QList<QVariantMap> added;
    for (const auto &value : items) {
        auto item = value.toObject().toVariantMap();
        if (brands())
            item.insert("source", "brand");
        bool exists = false;
        for (const auto &old : m_rows)
            if (old.value("id") == item.value("id") &&
                old.value("source") == item.value("source")) {
                exists = true;
                break;
            }
        if (!exists)
            added.append(item);
    }
    if (!added.isEmpty()) {
        const int first = int(m_rows.size());
        beginInsertRows({}, first, first + int(added.size()) - 1);
        m_rows.append(added);
        endInsertRows();
    }
    m_total = page.value("total").toInt();
    m_more = !items.isEmpty() && (!page.value("total").isDouble() || m_page * 20 < m_total);
    ++m_page;
    m_error.clear();
    emit changed();
}
void AudioEffectCatalog::activate(int row)
{
    if (m_closing || downloading() || row < -1 || row >= m_rows.size() ||
        (row == -1 && m_commonEffect.isEmpty()))
        return;
    const auto effect = row == -1 ? m_commonEffect : m_rows[row];
    if (effect.value("source").toString() == "brand") {
        selectBrand(row);
        return;
    }
    const auto downloaded = m_controls.downloadedImpulseResponse(identity(effect));
    if (!downloaded.isEmpty()) {
        useDownloaded(identity(effect));
        return;
    }
    if (!effect.value("unavailable_reason").toString().isEmpty()) {
        m_error = effect.value("unavailable_reason").toString();
        emit changed();
        emit notice(m_error, true);
        return;
    }
    if (!effect.value("vpf_urls").toList().isEmpty()) {
        m_error = tr("此音效需要支持 VPF 的音效引擎");
        emit changed();
        emit notice(m_error, true);
        return;
    }
    m_urls.clear();
    for (const auto &raw : effect.value("sound_urls").toList()) {
        const QUrl url(raw.toString());
        if (resource(url))
            m_urls.append(url.toString());
    }
    if (m_urls.isEmpty()) {
        m_error = tr("此音效没有可用的脉冲响应资源");
        emit changed();
        emit notice(m_error, true);
        return;
    }
    m_downloading = effect;
    const auto directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/audio-effects";
    m_destination = directory + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".irs";
    m_installer.install(++m_downloadGeneration, m_urls, m_destination);
    emit changed();
}
void AudioEffectCatalog::useDownloaded(const QString &key)
{
    if (m_closing)
        return;
    if (m_controls.effectPending()) {
        emit notice(tr("音效正在处理中，请稍后重试"), true);
        return;
    }
    const auto path = m_controls.downloadedImpulseResponse(key);
    if (path.isEmpty()) {
        emit notice(tr("音效文件已移除，请重新下载"), true);
        return;
    }
    m_controls.selectImpulseResponse(path);
    if (m_controls.impulseResponse() != path || !m_controls.impulseEnabled())
        emit notice(m_controls.errorMessage().isEmpty() ? tr("音效暂时无法使用")
                                                        : m_controls.errorMessage(),
                    true);
    else
        emit notice(tr("已使用音效"), false);
}
void AudioEffectCatalog::finishDownload(bool success, const QString &error)
{
    if (success) {
        m_controls.registerImpulseResponse(m_destination, m_downloading.value("name").toString(),
                                           identity(m_downloading));
        m_error.clear();
        emit downloaded(identity(m_downloading), m_downloading.value("name").toString());
    } else {
        m_error = error.isEmpty() ? tr("音效下载失败") : error;
        emit notice(m_error, true);
    }
    m_downloading = {};
    m_urls.clear();
    m_progress = 0;
    emit changed();
}
} // namespace EchoMusic
