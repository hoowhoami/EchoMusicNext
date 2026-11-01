#include "AudioEffectSearch.h"
#include <algorithm>
namespace EchoMusic::AudioEffectSearch
{
QString normalize(const QString &text)
{
    QString result;
    const auto decomposed = text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    for (auto character : decomposed)
        if (character.isLetterOrNumber())
            result.append(character);
    return result;
}
void prepare(QVariantMap &row)
{
    QStringList names = row.value("aliases").toStringList();
    names.prepend(row.value("name").toString());
    QStringList normalized;
    for (const auto &name : names)
        normalized.append(normalize(name));
    row.insert("_names", normalized);
    row.insert("_author", normalize(row.value("author").toString()));
    row.insert("_labels", normalize(row.value("labels").toStringList().join(' ')));
    row.insert("_intro", normalize(row.value("intro").toString()));
}
static bool oneEdit(const QString &a, const QString &b)
{
    if (a.size() < 4 || a.size() > 64 || qAbs(a.size() - b.size()) > 1)
        return false;
    int i = 0, j = 0, edits = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            ++i;
            ++j;
            continue;
        }
        if (++edits > 1)
            return false;
        if (a.size() >= b.size())
            ++i;
        if (b.size() >= a.size())
            ++j;
    }
    return edits + int(i < a.size() || j < b.size()) <= 1;
}
int rank(const QVariantMap &row, const QStringList &terms)
{
    int rank = 0;
    for (const auto &term : terms) {
        int score = 1000;
        for (const auto &name : row.value("_names").toStringList()) {
            if (name == term)
                score = 0;
            else if (name.startsWith(term))
                score = qMin(score, 10);
            else if (name.contains(term))
                score = qMin(score, 20);
            else if (term.size() >= 4 && term.front().unicode() < 128 && oneEdit(term, name))
                score = qMin(score, 60);
        }
        if (row.value("_author").toString().contains(term))
            score = qMin(score, 30);
        if (row.value("_labels").toString().contains(term))
            score = qMin(score, 35);
        if (row.value("_intro").toString().contains(term))
            score = qMin(score, 45);
        if (score == 1000)
            return -1;
        rank += score;
    }
    return rank;
}
} // namespace EchoMusic::AudioEffectSearch
