#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);
    void setActiveBlock(int blockNumber);
    int activeBlock() const { return m_activeBlock; }
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

    enum class InlineKind { Bold, Italic, Link, Strike, Code };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
    };

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and to hide markers outside the line being edited.
    static QList<InlineMarkup> inlineMarkup(const QString &text);
    // Block number of the fence closing a front matter block that opens the
    // document, or -1 when there is none.
    static int frontMatterEnd(const QTextDocument *document);

signals:
    void frontMatterChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum BlockState { Normal = 0, InCodeBlock = 1 };

    void rebuildFormats();
    bool highlightCodeBlock(const QString &text, bool active);
    void highlightFrontMatter(const QString &text, bool fence);
    void applyFrontMatterVisibility();
    bool caretInFrontMatter() const;
    void highlightMarkers(const QString &text, bool active);
    void highlightInline(const QString &text, bool active);
    void highlightSearch(const QString &text);
    void mergeFormat(int start, int length, const QTextCharFormat &format);

    bool m_darkMode = true;
    int m_activeBlock = -1;
    int m_frontMatterEnd = -1;
    int m_collapsedThrough = 0;
    bool m_frontMatterHidden = false;
    QTimer m_frontMatterTimer;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
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
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_frontMatterFormat;
    QTextCharFormat m_frontMatterKeyFormat;
    QTextCharFormat m_frontMatterFenceFormat;
    QTextCharFormat m_linkFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
};
