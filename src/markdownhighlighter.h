#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>

class SpellChecker;

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setTheme(bool darkMode, const QString &background, const QString &foreground,
                  const QString &accent, const QString &codeBackground);
    void setSearch(const QString &query, int currentMatchStart);
    // The caret's block shows raw Markdown; its column keeps the word being
    // typed from being flagged before it is finished.
    void setCaret(int position);
    // Underlines misspelled words while set; null turns spell check off.
    void setSpellChecker(SpellChecker *spellChecker);
    int activeBlock() const { return m_caret.isNull() ? -1 : m_caret.blockNumber(); }
    void setFrontMatterHidden(bool hidden);
    bool hasFrontMatter() const { return m_frontMatterEnd > 0; }
    // Rescans the top of the document for a front matter block. Edits schedule
    // this on their own; it is public so the result can be forced.
    void updateFrontMatter();
    void refreshFont();

    struct Span {
        int start;
        int length;
    };

    // Marks the characters of a misspelled word in the layout's formats.
    static constexpr int MisspelledProperty = QTextFormat::UserProperty + 1;

    enum class InlineKind { Bold, Italic, Strikethrough, Link };

    // Carried from block to block so a fenced run of code knows it is inside
    // one. Stored on the block, which is where Backend reads it.
    // Inside a fence the state is InsideFence plus twice the fence characters
    // past three that opened it, plus one for tildes, since only a fence of the
    // same character at least that long closes it.
    enum BlockState { Prose = 0, InsideFence = 1 };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
        Span destination{};
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and to hide markers outside the line being edited, and
    // Backend to find what the caret sits in.
    static QList<InlineMarkup> inlineMarkup(const QString &text);
    static QString linkUrlAt(const QString &text, int position);
    // Block number of the fence closing a front matter block that opens the
    // document, or -1 when there is none.
    static int frontMatterEnd(const QTextDocument *document);

    // Inline code spans, backticks included.
    static QList<Span> codeSpans(const QString &text);
    static int openingFenceLength(const QString &text);
    static bool isThematicBreak(const QString &text);
    static bool closesFence(const QString &text, int openingLength,
                            QChar marker = QLatin1Char('`'));

    // Where query occurs in text, ignoring case and, for the query's plain
    // Latin and Greek letters, diacritics: "tara" finds "țară". An accented
    // letter in the query matches only itself, though ş and ș, ţ and ț count
    // as one. Folding keeps one UTF-16 unit per unit, so offsets and lengths
    // are the text's own.
    static QList<int> findMatches(const QString &text, const QString &query);

signals:
    void frontMatterChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    void rehighlightAll();
    bool highlightCodeBlock(const QString &text, bool active);
    void highlightFrontMatter(const QString &text, bool fence);
    void applyFrontMatterVisibility();
    bool caretInFrontMatter() const;
    void highlightMarkers(const QString &text, bool active);
    void highlightInline(const QString &text, bool active);
    void highlightSearch(const QString &text);
    void highlightSpelling(const QString &text, bool active);
    QPair<int, int> typedWord(const QTextBlock &block, int column) const;
    void mergeFormat(int start, int length, const QTextCharFormat &format);

    bool m_darkMode = true;
    SpellChecker *m_spellChecker = nullptr;
    int m_frontMatterEnd = -1;
    int m_collapsedThrough = 0;
    bool m_frontMatterHidden = false;
    QTimer m_frontMatterTimer;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QString m_customCodeBackground;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_invisibleFormat;
    QTextCharFormat m_bulletFormat;
    QTextCharFormat m_headingFormats[6];
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikeFormat;
    QTextCharFormat m_doneTaskFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_codeMarkerFormat;
    QTextCharFormat m_hiddenCodeMarkerFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_frontMatterFormat;
    QTextCharFormat m_frontMatterKeyFormat;
    QTextCharFormat m_frontMatterFenceFormat;
    QTextCharFormat m_linkFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    // Moves with edits, so it still finds the block the current match was
    // painted in after text above it changes.
    QTextCursor m_currentMatch;
    // The block holding the caret shows its raw Markdown. A cursor rather than
    // a block number, so it follows the text through edits above it.
    QTextCursor m_caret;
    QTextCharFormat m_misspelledFormat;
    QTextCharFormat m_misspelledPlainFormat;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
