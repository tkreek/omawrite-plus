#include "spellchecker.h"

#include <hunspell/hunspell.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString findDictionary(const QString &language) {
    const QStringList directories = SpellChecker::dictionaryDirectories();
    const auto exists = [&](const QString &name) {
        for (const QString &directory : directories) {
            const QString base = QDir(directory).filePath(name);
            if (QFile::exists(base + QStringLiteral(".dic"))
                    && QFile::exists(base + QStringLiteral(".aff")))
                return base;
        }
        return QString();
    };

    if (const QString exact = exists(language); !exact.isEmpty())
        return exact;

    // Another variant of the same language, such as en_GB for en_AU.
    const QString prefix = language.section(QLatin1Char('_'), 0, 0) + QLatin1Char('_');
    for (const QString &directory : directories) {
        const QStringList dictionaries =
            QDir(directory).entryList({prefix + QStringLiteral("*.dic")}, QDir::Files, QDir::Name);
        for (const QString &dictionary : dictionaries) {
            const QString base = QDir(directory).filePath(QFileInfo(dictionary).completeBaseName());
            if (QFile::exists(base + QStringLiteral(".aff")))
                return base;
        }
    }

    return language == QStringLiteral("en_US") ? QString() : exists(QStringLiteral("en_US"));
}

} // namespace

SpellChecker::SpellChecker(const QString &language) {
    const QString base = dictionaryPath(language);
    if (base.isEmpty())
        return;

    m_hunspell = std::make_unique<Hunspell>(QFile::encodeName(base + QStringLiteral(".aff")).constData(),
                                            QFile::encodeName(base + QStringLiteral(".dic")).constData());
    m_language = QFileInfo(base).fileName();
    const QString encoding = QString::fromStdString(m_hunspell->get_dict_encoding()).toUpper();
    m_utf8 = encoding == QStringLiteral("UTF-8") || encoding == QStringLiteral("UTF8");

    QFile personal(personalDictionaryPath());
    if (personal.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&personal);
        while (!stream.atEnd()) {
            const QString word = stream.readLine().trimmed();
            if (!word.isEmpty())
                m_hunspell->add(encode(word).toStdString());
        }
    }
}

SpellChecker::~SpellChecker() = default;

QByteArray SpellChecker::encode(const QString &word) const {
    return m_utf8 ? word.toUtf8() : word.toLatin1();
}

QString SpellChecker::decode(const std::string &word) const {
    return m_utf8 ? QString::fromStdString(word) : QString::fromLatin1(word.c_str());
}

bool SpellChecker::isCorrect(const QString &word) {
    if (!m_hunspell)
        return true;

    // Typographic apostrophes are how the word looks, but dictionaries spell
    // contractions with the plain one.
    QString normalized = word;
    normalized.replace(QChar(0x2019), QLatin1Char('\''));

    const auto cached = m_cache.constFind(normalized);
    if (cached != m_cache.constEnd())
        return cached.value();

    const bool correct = m_hunspell->spell(encode(normalized).toStdString());
    m_cache.insert(normalized, correct);
    return correct;
}

QStringList SpellChecker::suggestions(const QString &word, int maximum) {
    QStringList result;
    if (!m_hunspell)
        return result;

    QString normalized = word;
    normalized.replace(QChar(0x2019), QLatin1Char('\''));
    for (const std::string &suggestion : m_hunspell->suggest(encode(normalized).toStdString())) {
        result.append(decode(suggestion));
        if (result.size() >= maximum)
            break;
    }
    return result;
}

void SpellChecker::addWord(const QString &word) {
    if (word.isEmpty())
        return;

    m_cache.insert(word, true);
    if (m_hunspell)
        m_hunspell->add(encode(word).toStdString());

    const QString path = personalDictionaryPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << word << '\n';
    }
}

namespace {

const QRegularExpression &wordPattern() {
    static const QRegularExpression wordRe(
        QStringLiteral("[\\p{L}\\p{M}]+(?:['\\x{2019}][\\p{L}\\p{M}]+)*"));
    return wordRe;
}

} // namespace

SpellChecker::Word SpellChecker::wordAt(const QString &text, int column) {
    QRegularExpressionMatchIterator matches = wordPattern().globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (column >= match.capturedStart(0) && column <= match.capturedEnd(0))
            return {int(match.capturedStart(0)), int(match.capturedLength(0))};
    }
    return {-1, 0};
}

QList<SpellChecker::Word> SpellChecker::words(const QString &text) {
    QList<Word> words;
    static const QRegularExpression chunkRe(QStringLiteral("\\S+"));
    const QRegularExpression &wordRe = wordPattern();

    QRegularExpressionMatchIterator chunks = chunkRe.globalMatch(text);
    while (chunks.hasNext()) {
        const QRegularExpressionMatch chunk = chunks.next();
        const QString chunkText = chunk.captured(0);
        if (chunkText.contains(QStringLiteral("://")) || chunkText.contains(QLatin1Char('@'))
                || chunkText.startsWith(QStringLiteral("www."), Qt::CaseInsensitive))
            continue;

        QRegularExpressionMatchIterator matches = wordRe.globalMatch(chunkText);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const int start = chunk.capturedStart(0) + match.capturedStart(0);
            const int end = start + match.capturedLength(0);
            const QString word = match.captured(0);
            if (word.length() < 2)
                continue;
            if (word == word.toUpper())
                continue;
            if ((start > 0 && text.at(start - 1).isDigit())
                    || (end < text.length() && text.at(end).isDigit()))
                continue;
            words.append({start, end - start});
        }
    }
    return words;
}

QStringList SpellChecker::dictionaryDirectories() {
    QStringList directories;
    const QString fromEnvironment = qEnvironmentVariable("DICPATH");
    if (!fromEnvironment.isEmpty())
        directories += fromEnvironment.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    directories << QDir::homePath() + QStringLiteral("/.local/share/hunspell")
                << QStringLiteral("/usr/share/hunspell")
                << QStringLiteral("/usr/share/myspell/dicts")
                << QStringLiteral("/usr/share/myspell");
    return directories;
}

QString SpellChecker::dictionaryPath(const QString &language) {
    return findDictionary(language.isEmpty() ? QLocale::system().name() : language);
}

QString SpellChecker::personalDictionaryPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("dictionary.txt"));
}
