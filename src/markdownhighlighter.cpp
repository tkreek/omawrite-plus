#include "markdownhighlighter.h"

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

namespace {

// Heading sizes relative to body text, from # to ######.
constexpr qreal headingScales[6] = {1.6, 1.35, 1.15, 1.0, 1.0, 1.0};

bool overlaps(const MarkdownHighlighter::Span &a, const MarkdownHighlighter::Span &b) {
    return a.start < b.start + b.length && b.start < a.start + a.length;
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();
}

void MarkdownHighlighter::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    rebuildFormats();
    rehighlight();
}

// The hidden marker spacing and heading sizes are measured from the
// document's default font, so they have to be remeasured whenever the editor
// switches typeface or text size.
void MarkdownHighlighter::refreshFont() {
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setColors(const QString &background, const QString &foreground,
                                    const QString &accent) {
    if (m_customBackground == background && m_customForeground == foreground
            && m_customAccent == accent)
        return;

    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    rebuildFormats();
    rehighlight();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart)
        return;
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    rehighlight();
}

// The block holding the caret shows its raw Markdown; every other block is
// rendered. Only the two blocks whose state flips need to be redrawn.
void MarkdownHighlighter::setActiveBlock(int blockNumber) {
    if (m_activeBlock == blockNumber)
        return;

    const int previous = m_activeBlock;
    m_activeBlock = blockNumber;
    if (!document())
        return;
    for (const int number : {previous, blockNumber}) {
        const QTextBlock block = document()->findBlockByNumber(number);
        if (block.isValid())
            rehighlightBlock(block);
    }
}

void MarkdownHighlighter::rebuildFormats() {
    const QColor marker = m_darkMode ? QColor(QStringLiteral("#4f525a"))
                                     : QColor(QStringLiteral("#aeb1b5"));
    const QColor background = !m_customBackground.isEmpty() ? QColor(m_customBackground)
        : (m_darkMode ? QColor(QStringLiteral("#101010")) : QColor(QStringLiteral("#ffffff")));
    const QColor text = !m_customForeground.isEmpty() ? QColor(m_customForeground)
        : (m_darkMode ? QColor(QStringLiteral("#eeeeee")) : QColor(QStringLiteral("#222324")));
    const QColor link = !m_customAccent.isEmpty() ? QColor(m_customAccent)
        : (m_darkMode ? QColor(QStringLiteral("#5584aa")) : QColor(QStringLiteral("#2077b2")));
    const QColor quote = marker;
    const QColor codeBackground = m_darkMode ? QColor(QStringLiteral("#1c1a1a"))
                                             : QColor(QStringLiteral("#f8f8f8"));
    const QFont baseFont = document() ? document()->defaultFont() : QFont();

    m_markerFormat = QTextCharFormat();
    m_markerFormat.setForeground(marker);

    // A sub-pixel font size combined with a stretch factor used to make these
    // markers occupy (close to) zero space, but that combination deadlocks Qt's
    // font metrics engine on some platforms. Instead, use a normal font size and
    // cancel out its advance width with negative absolute letter-spacing.
    m_hiddenMarkerFormat = QTextCharFormat();
    m_hiddenMarkerFormat.setForeground(background);
    m_hiddenMarkerFormat.setFontPointSize(1.0);

    QFont hiddenFont = baseFont;
    hiddenFont.setPointSizeF(1.0);
    const qreal charWidth = QFontMetricsF(hiddenFont).horizontalAdvance(QLatin1Char('['));

    m_hiddenMarkerFormat.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    m_hiddenMarkerFormat.setFontLetterSpacing(-charWidth);

    // Fence lines keep their height so a code block does not jump when the
    // caret enters it; they are only painted out.
    m_invisibleFormat = QTextCharFormat();
    m_invisibleFormat.setForeground(background);

    m_bulletFormat = QTextCharFormat();
    m_bulletFormat.setForeground(link);
    m_bulletFormat.setFontWeight(QFont::Bold);

    for (int level = 0; level < 6; ++level) {
        QTextCharFormat &heading = m_headingFormats[level];
        heading = QTextCharFormat();
        heading.setForeground(text);
        heading.setFontWeight(QFont::Bold);
        if (baseFont.pixelSize() > 0) {
            heading.setProperty(QTextFormat::FontPixelSize,
                                qRound(baseFont.pixelSize() * headingScales[level]));
        } else if (baseFont.pointSizeF() > 0) {
            heading.setFontPointSize(baseFont.pointSizeF() * headingScales[level]);
        }
    }

    m_boldFormat = QTextCharFormat();
    m_boldFormat.setFontWeight(QFont::Bold);
    m_boldFormat.setForeground(text);

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);
    m_italicFormat.setForeground(text);

    m_strikeFormat = QTextCharFormat();
    m_strikeFormat.setFontStrikeOut(true);

    m_doneTaskFormat = QTextCharFormat();
    m_doneTaskFormat.setFontStrikeOut(true);
    m_doneTaskFormat.setForeground(marker);

    m_codeFormat = QTextCharFormat();
    m_codeFormat.setForeground(text);
    m_codeFormat.setBackground(codeBackground);
    if (!QFontInfo(baseFont).fixedPitch()) {
        m_codeFormat.setFontFamilies(
            {QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
    }

    m_codeMarkerFormat = m_codeFormat;
    m_codeMarkerFormat.setForeground(marker);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const bool active = currentBlock().blockNumber() == m_activeBlock;
    if (!highlightCodeBlock(text, active) && !text.isEmpty()) {
        highlightMarkers(text, active);
        if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
            || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))
            || text.contains(QLatin1Char('~'))) {
            highlightInline(text, active);
        }
    }
    highlightSearch(text);
}

// Returns true when the block is a fence or sits inside a fenced code block,
// in which case no other Markdown applies to it.
bool MarkdownHighlighter::highlightCodeBlock(const QString &text, bool active) {
    const bool insideCode = previousBlockState() == InCodeBlock;
    static const QRegularExpression fenceRe(QStringLiteral("^\\s{0,3}(```|~~~)"));
    if (fenceRe.match(text).hasMatch()) {
        setCurrentBlockState(insideCode ? Normal : InCodeBlock);
        setFormat(0, text.length(), active ? m_codeMarkerFormat : m_invisibleFormat);
        return true;
    }

    setCurrentBlockState(insideCode ? InCodeBlock : Normal);
    if (insideCode)
        setFormat(0, text.length(), m_codeFormat);
    return insideCode;
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    int from = 0;
    while ((from = text.indexOf(m_searchQuery, from, Qt::CaseInsensitive)) >= 0) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
        from += qMax(1, m_searchQuery.length());
    }
}

// Layers a format over whatever is already set, so inline styles keep the
// size of the heading they sit in.
void MarkdownHighlighter::mergeFormat(int start, int length, const QTextCharFormat &format) {
    const int end = start + length;
    for (int i = start; i < end;) {
        const QTextCharFormat current = this->format(i);
        int run = 1;
        while (i + run < end && this->format(i + run) == current)
            ++run;
        QTextCharFormat merged = current;
        merged.merge(format);
        setFormat(i, run, merged);
        i += run;
    }
}

void MarkdownHighlighter::highlightMarkers(const QString &text, bool active) {
    int first = 0;
    while (first < text.length() && text.at(first).isSpace())
        ++first;
    if (first >= text.length())
        return;

    const QChar firstChar = text.at(first);
    if (first == 0 && firstChar == QLatin1Char('#')) {
        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s+)(.*)$"));
        const QRegularExpressionMatch heading = headingRe.match(text);
        if (heading.hasMatch()) {
            const QTextCharFormat &headingFormat =
                m_headingFormats[heading.capturedLength(1) - 1];
            setFormat(0, text.length(), headingFormat);
            const int markerLength = heading.capturedLength(1) + heading.capturedLength(2);
            if (active)
                mergeFormat(0, markerLength, m_markerFormat);
            else
                setFormat(0, markerLength, m_hiddenMarkerFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('>')) {
        static const QRegularExpression quoteRe(QStringLiteral("^(\\s*>+\\s?)(.*)$"));
        const QRegularExpressionMatch quote = quoteRe.match(text);
        if (quote.hasMatch()) {
            setFormat(0, quote.capturedLength(1), active ? m_markerFormat : m_bulletFormat);
            setFormat(quote.capturedStart(2), quote.capturedLength(2), m_quoteFormat);
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('*')
            || firstChar == QLatin1Char('_')) {
        static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
        if (ruleRe.match(text).hasMatch()) {
            setFormat(0, text.length(), m_markerFormat);
            return;
        }
    }

    if (firstChar == QLatin1Char('-') || firstChar == QLatin1Char('+')
            || firstChar == QLatin1Char('*') || firstChar.isDigit()) {
        static const QRegularExpression listRe(
            QStringLiteral("^(\\s*(?:[-+*]|\\d+[.)])\\s+)(\\[[ xX]\\]\\s+)?(.*)$"));
        const QRegularExpressionMatch list = listRe.match(text);
        if (list.hasMatch()) {
            const QTextCharFormat &markerFormat = active ? m_markerFormat : m_bulletFormat;
            setFormat(0, list.capturedLength(1), markerFormat);
            if (list.capturedLength(2) > 0) {
                setFormat(list.capturedStart(2), list.capturedLength(2), markerFormat);
                if (text.at(list.capturedStart(2) + 1) != QLatin1Char(' '))
                    setFormat(list.capturedStart(3), list.capturedLength(3), m_doneTaskFormat);
            }
        }
    }
}

void MarkdownHighlighter::highlightInline(const QString &text, bool active) {
    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        const QTextCharFormat &contentFormat =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
            : item.kind == InlineKind::Strike ? m_strikeFormat
            : item.kind == InlineKind::Code ? m_codeFormat
                                            : m_linkFormat;
        mergeFormat(item.content.start, item.content.length, contentFormat);
        for (const Span &marker : item.markers) {
            if (!active)
                setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
            else if (item.kind == InlineKind::Code)
                mergeFormat(marker.start, marker.length, m_codeMarkerFormat);
            else
                mergeFormat(marker.start, marker.length, m_markerFormat);
        }
    }
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('`'))
            && !text.contains(QLatin1Char('~'))) {
        return markup;
    }

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    // Code spans are literal, so anything else that falls inside one is dropped.
    QList<InlineMarkup> code;
    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    QRegularExpressionMatchIterator codeMatches = codeRe.globalMatch(text);
    while (codeMatches.hasNext()) {
        const QRegularExpressionMatch match = codeMatches.next();
        const Span whole = span(match, 0);
        code.append({InlineKind::Code, span(match, 1),
                     {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }
    const auto append = [&](const InlineMarkup &item) {
        const Span whole{item.markers[0].start,
                         item.markers[1].start + item.markers[1].length - item.markers[0].start};
        for (const InlineMarkup &literal : code) {
            const Span literalWhole{literal.markers[0].start,
                                    literal.markers[1].start + 1 - literal.markers[0].start};
            if (overlaps(whole, literalWhole))
                return;
        }
        markup.append(item);
    };

    static const QRegularExpression boldRe(QStringLiteral("(\\*\\*|__)(.+?)(\\1)"));
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(text);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        append({InlineKind::Bold, span(match, 2), {span(match, 1), span(match, 3)}});
    }

    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*([^*\\n]+)\\*(?!\\*)|(?<!_)_([^_\\n]+)_(?!_)"));
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(text);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        append({InlineKind::Italic, span(match, contentIndex),
                {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|[^)])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(text);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        append({InlineKind::Link, content,
                {{whole.start, 1},
                 {contentEnd, whole.start + whole.length - contentEnd}}});
    }

    static const QRegularExpression strikeRe(QStringLiteral("(~~)(.+?)(~~)"));
    QRegularExpressionMatchIterator strikeMatches = strikeRe.globalMatch(text);
    while (strikeMatches.hasNext()) {
        const QRegularExpressionMatch match = strikeMatches.next();
        append({InlineKind::Strike, span(match, 2), {span(match, 1), span(match, 3)}});
    }

    markup.append(code);
    return markup;
}
