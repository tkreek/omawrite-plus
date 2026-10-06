#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

class Hunspell;

class SpellChecker {
public:
    // Loads the Hunspell dictionary for the given language (such as "en_US"),
    // falling back to another dictionary of the same language and then to US
    // English. An empty language means the system locale.
    explicit SpellChecker(const QString &language = QString());
    ~SpellChecker();

    bool isAvailable() const { return m_hunspell != nullptr; }
    QString language() const { return m_language; }

    bool isCorrect(const QString &word);
    QStringList suggestions(const QString &word, int maximum = 6);
    // Accepts the word from now on and remembers it in the personal dictionary.
    void addWord(const QString &word);

    struct Word {
        int start;
        int length;
    };

    // The words in a line of prose worth checking: no URLs, e-mail addresses,
    // acronyms, or words run together with digits.
    static QList<Word> words(const QString &text);
    // The word touching a column (including just after its last letter), or
    // a Word with start -1.
    static Word wordAt(const QString &text, int column);
    static QStringList dictionaryDirectories();
    // The .dic/.aff path (without extension) that would be loaded for a
    // language, or an empty string when no dictionary is installed.
    static QString dictionaryPath(const QString &language = QString());
    static QString personalDictionaryPath();

private:
    QByteArray encode(const QString &word) const;
    QString decode(const std::string &word) const;

    std::unique_ptr<Hunspell> m_hunspell;
    QString m_language;
    bool m_utf8 = true;
    QHash<QString, bool> m_cache;
};
