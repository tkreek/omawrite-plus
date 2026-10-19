#include "markdownhighlighter.h"
#include "spellchecker.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

// Heading sizes relative to body text, from # to ######.
constexpr qreal headingScales[6] = {1.6, 1.35, 1.15, 1.0, 1.0, 1.0};

constexpr qreal frontMatterScale = 0.85;

// Front matter has to close near the top; a stray --- further down the
// document is a horizontal rule, not the end of a metadata block.
constexpr int frontMatterMaxBlocks = 200;

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {
    rebuildFormats();

    // Whether the top of the document is front matter can change with an edit
    // anywhere inside it, so rescan once the edit (and its rehighlight) settles.
    m_frontMatterTimer.setSingleShot(true);
    m_frontMatterTimer.setInterval(0);
    connect(&m_frontMatterTimer, &QTimer::timeout, this, &MarkdownHighlighter::updateFrontMatter);
    if (document) {
        connect(document, &QTextDocument::contentsChange, &m_frontMatterTimer,
                qOverload<>(&QTimer::start));
    }
    updateFrontMatter();
}

// One setter for the whole theme, so a theme change rehighlights once.
void MarkdownHighlighter::setTheme(bool darkMode, const QString &background,
                                   const QString &foreground, const QString &accent,
                                   const QString &codeBackground) {
    if (m_darkMode == darkMode && m_customBackground == background
            && m_customForeground == foreground && m_customAccent == accent
            && m_customCodeBackground == codeBackground)
        return;

    m_darkMode = darkMode;
    m_customBackground = background;
    m_customForeground = foreground;
    m_customAccent = accent;
    m_customCodeBackground = codeBackground;
    rebuildFormats();
    rehighlightAll();
}

// The hidden marker spacing and heading sizes are measured from the
// document's default font, so they have to be remeasured whenever the editor
// switches typeface or text size.
void MarkdownHighlighter::refreshFont() {
    rebuildFormats();
    rehighlightAll();
}

void MarkdownHighlighter::setSearch(const QString &query, int currentMatchStart) {
    // An edit can carry the painted match away from an offset another match
    // now holds, so the offset matching is not enough to leave things be.
    const bool matchStayed = m_currentMatch.isNull() ? currentMatchStart < 0
                                                     : m_currentMatch.position() == currentMatchStart;
    if (m_searchQuery == query && m_currentMatchStart == currentMatchStart && matchStayed)
        return;
    const bool sameQuery = m_searchQuery == query;
    const QTextBlock left = m_currentMatch.isNull() ? QTextBlock() : m_currentMatch.block();
    m_searchQuery = query;
    m_currentMatchStart = currentMatchStart;
    m_currentMatch = QTextCursor();
    if (currentMatchStart >= 0) {
        m_currentMatch = QTextCursor(document());
        m_currentMatch.setPosition(qBound(0, currentMatchStart, document()->characterCount() - 1));
    }
    if (!sameQuery) {
        rehighlightAll();
        return;
    }

    // Only the current match moved, so restyle just the blocks it left and
    // entered rather than the whole document.
    const QTextBlock entered = m_currentMatch.isNull() ? QTextBlock() : m_currentMatch.block();
    if (left.isValid())
        rehighlightBlock(left);
    if (entered.isValid() && entered != left)
        rehighlightBlock(entered);
}

// QSyntaxHighlighter::rehighlight() marks the blocks dirty one at a time, and
// a laid-out document redoes layout work for the rest of itself after each
// one, so a full restyle grows with the square of its length: 40 s for a 2 MB
// file. A null page size makes the layout skip that work, so restyle under one
// and lay the document out once when the real size comes back.
//
// That layout has to finish here. Restoring the size only starts a lazy one,
// and Qt Quick's render thread would lay out whatever it reaches first,
// starting the layout's timers from the wrong thread.
void MarkdownHighlighter::rehighlightAll() {
    QTextDocument *doc = document();
    if (!doc) {
        rehighlight();
        return;
    }
    const QSizeF pageSize = doc->pageSize();
    doc->setPageSize(QSizeF(0, 0));
    rehighlight();
    doc->setPageSize(pageSize);
    if (QAbstractTextDocumentLayout *layout = doc->documentLayout())
        layout->documentSize();
}

// The block holding the caret shows its raw Markdown; every other block is
// rendered.
void MarkdownHighlighter::setCaret(int position) {
    const QTextBlock left = m_caret.isNull() ? QTextBlock() : m_caret.block();
    const bool wasInFrontMatter = caretInFrontMatter();
    if (m_caret.isNull()) {
        m_caret = QTextCursor(document());
        // Loading a file inserts at the caret, which must not drag it along
        // to the end; the editor reports where it really went.
        m_caret.setKeepPositionOnInsert(true);
    }
    m_caret.setPosition(qBound(0, position, document()->characterCount() - 1));
    if (wasInFrontMatter != caretInFrontMatter())
        applyFrontMatterVisibility();

    // Only the lines the caret left and entered can show different markers,
    // and the entered one also holds back the word being typed from spell
    // check.
    const QTextBlock entered = m_caret.block();
    if (left.isValid() && left != entered)
        rehighlightBlock(left);
    rehighlightBlock(entered);
}

void MarkdownHighlighter::setSpellChecker(SpellChecker *spellChecker) {
    if (m_spellChecker == spellChecker)
        return;
    m_spellChecker = spellChecker;
    rehighlightAll();
}

QPair<int, int> MarkdownHighlighter::typedWord(const QTextBlock &block, int column) const {
    if (column < 0)
        return {-1, -1};
    const SpellChecker::Word word = SpellChecker::wordAt(block.text(), column);
    return {word.start, word.length};
}

void MarkdownHighlighter::setFrontMatterHidden(bool hidden) {
    if (m_frontMatterHidden == hidden)
        return;

    m_frontMatterHidden = hidden;
    applyFrontMatterVisibility();
    const QTextBlock first = document() ? document()->firstBlock() : QTextBlock();
    if (first.isValid())
        rehighlightBlock(first);
}

int MarkdownHighlighter::frontMatterEnd(const QTextDocument *document) {
    if (!document)
        return -1;

    QTextBlock block = document->firstBlock();
    if (!block.isValid() || block.text().trimmed() != QStringLiteral("---"))
        return -1;

    for (block = block.next(); block.isValid() && block.blockNumber() < frontMatterMaxBlocks;
         block = block.next()) {
        const QString text = block.text().trimmed();
        if (text == QStringLiteral("---") || text == QStringLiteral("..."))
            return block.blockNumber();
    }
    return -1;
}

void MarkdownHighlighter::updateFrontMatter() {
    const int end = frontMatterEnd(document());
    if (end == m_frontMatterEnd)
        return;

    const int previous = m_frontMatterEnd;
    m_frontMatterEnd = end;
    for (QTextBlock block = document()->firstBlock();
         block.isValid() && block.blockNumber() <= qMax(previous, end); block = block.next()) {
        rehighlightBlock(block);
    }
    applyFrontMatterVisibility();
    emit frontMatterChanged();
}

bool MarkdownHighlighter::caretInFrontMatter() const {
    const int active = activeBlock();
    return active >= 0 && active <= m_frontMatterEnd;
}

// Hidden front matter collapses to its opening fence. Moving the caret into
// that line, or into the block from the keyboard, opens it up again.
void MarkdownHighlighter::applyFrontMatterVisibility() {
    QTextDocument *document = this->document();
    if (!document)
        return;

    const bool collapse = m_frontMatterHidden && hasFrontMatter() && !caretInFrontMatter();
    const int last = qMax(m_collapsedThrough, m_frontMatterEnd);
    int dirtyStart = -1;
    int dirtyEnd = -1;
    for (QTextBlock block = document->findBlockByNumber(1);
         block.isValid() && block.blockNumber() <= last; block = block.next()) {
        const bool visible = !collapse || block.blockNumber() > m_frontMatterEnd;
        if (block.isVisible() == visible)
            continue;
        block.setVisible(visible);
        if (dirtyStart < 0)
            dirtyStart = block.position();
        dirtyEnd = block.position() + block.length();
    }
    m_collapsedThrough = collapse ? m_frontMatterEnd : 0;

    if (dirtyStart >= 0)
        document->markContentsDirty(dirtyStart, dirtyEnd - dirtyStart);
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
    const QColor codeBackground = !m_customCodeBackground.isEmpty() ? QColor(m_customCodeBackground)
        : (m_darkMode ? QColor(QStringLiteral("#1c1a1a")) : QColor(QStringLiteral("#f8f8f8")));
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

    m_italicFormat = QTextCharFormat();
    m_italicFormat.setFontItalic(true);

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

    // A fence or backtick recedes the way a heading's `#` does, over the
    // panel it opens.
    m_codeMarkerFormat = m_codeFormat;
    m_codeMarkerFormat.setForeground(marker);

    // A hidden backtick keeps the panel behind it, so the code reads as one.
    m_hiddenCodeMarkerFormat = m_hiddenMarkerFormat;
    m_hiddenCodeMarkerFormat.setForeground(codeBackground);
    m_hiddenCodeMarkerFormat.setBackground(codeBackground);

    // Front matter is metadata, so it sits back from the prose: smaller, in
    // the code face, with keys picked out in the accent colour.
    m_frontMatterFormat = QTextCharFormat();
    m_frontMatterFormat.setForeground(QColor::fromRgbF(
        text.redF() * 0.6 + background.redF() * 0.4,
        text.greenF() * 0.6 + background.greenF() * 0.4,
        text.blueF() * 0.6 + background.blueF() * 0.4));
    if (m_codeFormat.hasProperty(QTextFormat::FontFamilies))
        m_frontMatterFormat.setFontFamilies(m_codeFormat.fontFamilies().toStringList());
    if (baseFont.pixelSize() > 0) {
        m_frontMatterFormat.setProperty(QTextFormat::FontPixelSize,
                                        qRound(baseFont.pixelSize() * frontMatterScale));
    } else if (baseFont.pointSizeF() > 0) {
        m_frontMatterFormat.setFontPointSize(baseFont.pointSizeF() * frontMatterScale);
    }

    m_frontMatterKeyFormat = m_frontMatterFormat;
    m_frontMatterKeyFormat.setForeground(link);

    m_frontMatterFenceFormat = m_frontMatterFormat;
    m_frontMatterFenceFormat.setForeground(marker);

    m_quoteFormat = QTextCharFormat();
    m_quoteFormat.setForeground(quote);
    m_quoteFormat.setFontItalic(true);

    m_linkFormat = QTextCharFormat();
    m_linkFormat.setForeground(link);
    m_linkFormat.setFontUnderline(true);

    m_misspelledFormat = QTextCharFormat();
    // Qt Quick only draws plain underlines, not the wavy spell-check style.
    m_misspelledFormat.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    m_misspelledFormat.setProperty(MisspelledProperty, true);
    m_misspelledFormat.setUnderlineColor(m_darkMode ? QColor(QStringLiteral("#e0575b"))
                                                    : QColor(QStringLiteral("#d12f2f")));
    // Qt Quick paints the underline in the text colour unless the format
    // spells the text colour out, so plain body text needs one to go red.
    m_misspelledPlainFormat = m_misspelledFormat;
    m_misspelledPlainFormat.setForeground(text);

    m_searchFormat = QTextCharFormat();
    m_searchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#725b18"))
                                            : QColor(QStringLiteral("#ffe58a")));
    m_currentSearchFormat = QTextCharFormat();
    m_currentSearchFormat.setBackground(m_darkMode ? QColor(QStringLiteral("#b36b20"))
                                                   : QColor(QStringLiteral("#ffad42")));
}

void MarkdownHighlighter::highlightBlock(const QString &text) {
    const int number = currentBlock().blockNumber();
    if (number <= m_frontMatterEnd) {
        setCurrentBlockState(Prose);
        highlightFrontMatter(text, number == 0 || number == m_frontMatterEnd);
        highlightSearch(text);
        return;
    }

    const bool active = !m_caret.isNull() && m_caret.block() == currentBlock();
    if (!highlightCodeBlock(text, active)) {
        if (!text.isEmpty()) {
            highlightMarkers(text, active);
            if (text.contains(QLatin1Char('`')) || text.contains(QLatin1Char('*'))
                || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('['))
                || text.contains(QLatin1Char('~'))) {
                highlightInline(text, active);
            }
            highlightSpelling(text, active);
        }
    }
    highlightSearch(text);
}

// Returns true when the block is a fence or sits inside a fenced code block,
// in which case no other Markdown applies to it.
bool MarkdownHighlighter::highlightCodeBlock(const QString &text, bool active) {
    // A fenced run of code is literal from its opening fence through its
    // closing one, so no markup runs inside it. The state rides from block to
    // block, and Qt rehighlights the rest of the document when it changes.
    const int previous = previousBlockState();
    if (previous >= InsideFence) {
        const int fence = previous - InsideFence;
        const bool closing = closesFence(text, fence / 2 + 3,
                                         fence % 2 ? QLatin1Char('~') : QLatin1Char('`'));
        setCurrentBlockState(closing ? Prose : previous);
        setFormat(0, text.length(), !closing ? m_codeFormat
                                    : active ? m_codeMarkerFormat : m_invisibleFormat);
        return true;
    }

    const int opening = openingFenceLength(text);
    if (opening > 0) {
        const bool tildes = text.trimmed().startsWith(QLatin1Char('~'));
        setCurrentBlockState(InsideFence + (opening - 3) * 2 + (tildes ? 1 : 0));
        setFormat(0, text.length(), active ? m_codeMarkerFormat : m_invisibleFormat);
        return true;
    }
    setCurrentBlockState(Prose);
    return false;
}

void MarkdownHighlighter::highlightFrontMatter(const QString &text, bool fence) {
    if (fence) {
        setFormat(0, text.length(), m_frontMatterFenceFormat);
        return;
    }

    setFormat(0, text.length(), m_frontMatterFormat);
    static const QRegularExpression commentRe(QStringLiteral("^\\s*#"));
    if (commentRe.match(text).hasMatch()) {
        setFormat(0, text.length(), m_frontMatterFenceFormat);
        return;
    }

    static const QRegularExpression keyRe(QStringLiteral("^(\\s*(?:-\\s+)?)([^\\s:#][^:]*):(?=\\s|$)"));
    const QRegularExpressionMatch key = keyRe.match(text);
    if (key.hasMatch()) {
        setFormat(key.capturedStart(2), key.capturedLength(2) + 1, m_frontMatterKeyFormat);
    }
}

void MarkdownHighlighter::highlightSpelling(const QString &text, bool active) {
    if (!m_spellChecker)
        return;

    // Code is literal and a link's destination is not prose, so blank them
    // out (keeping every column where it was) before picking out words.
    QString prose = text;
    for (const Span &code : codeSpans(text))
        prose.replace(code.start, code.length, QString(code.length, QLatin1Char(' ')));
    for (const InlineMarkup &item : inlineMarkup(text)) {
        if (item.kind == InlineKind::Link) {
            prose.replace(item.markers[1].start, item.markers[1].length,
                          QString(item.markers[1].length, QLatin1Char(' ')));
        }
    }

    const QPair<int, int> typing = active ? typedWord(currentBlock(), m_caret.positionInBlock())
                                          : QPair<int, int>{-1, -1};
    for (const SpellChecker::Word &word : SpellChecker::words(prose)) {
        if (word.start == typing.first)
            continue;
        if (m_spellChecker->isCorrect(text.mid(word.start, word.length)))
            continue;
        for (int i = word.start; i < word.start + word.length; ++i) {
            QTextCharFormat merged = format(i);
            merged.merge(merged.hasProperty(QTextFormat::ForegroundBrush)
                             ? m_misspelledFormat : m_misspelledPlainFormat);
            setFormat(i, 1, merged);
        }
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

// Three or more backticks or tildes, indented at most three spaces. An info
// string may follow, but a backtick fence's may not hold a backtick:
// "```code```" is inline code.
int MarkdownHighlighter::openingFenceLength(const QString &text) {
    static const QRegularExpression openingRe(
        QStringLiteral("^ {0,3}(?:(`{3,})[^`]*|(~{3,}).*)$"));
    const QRegularExpressionMatch match = openingRe.match(text);
    return match.hasMatch() ? int(qMax(match.capturedLength(1), match.capturedLength(2))) : 0;
}

// At least as many of the same fence character as opened it, and nothing after.
bool MarkdownHighlighter::closesFence(const QString &text, int openingLength, QChar marker) {
    static const QRegularExpression closingRe(QStringLiteral("^ {0,3}(`{3,}|~{3,})\\s*$"));
    const QRegularExpressionMatch match = closingRe.match(text);
    return match.hasMatch() && match.capturedLength(1) >= openingLength
        && match.captured(1).at(0) == marker;
}

namespace {

// Each UTF-16 unit case-folded and, for Latin and Greek letters, stripped of
// its canonical diacritics: 'Ț' becomes 't'. Other scripts keep their marks,
// since there they tell letters apart (и and й, か and が), and Hangul
// syllables would fold to their first jamo. Surrogates map to themselves here
// and are folded in pairs by searchFolded.
const char16_t *searchFoldTable() {
    static const std::vector<char16_t> table = [] {
        std::vector<char16_t> folded(0x10000);
        for (int unit = 0; unit < 0x10000; ++unit) {
            const QChar original{char16_t(unit)};
            if (original.isSurrogate()) {
                folded[unit] = char16_t(unit);
                continue;
            }
            const QChar caseFolded = original.toCaseFolded();
            QChar base = caseFolded;
            while (base.decompositionTag() == QChar::Canonical) {
                const QChar next = base.decomposition().at(0);
                if (next.isSurrogate())
                    break;
                base = next.toCaseFolded();
            }
            const bool strips = base.script() == QChar::Script_Latin
                || base.script() == QChar::Script_Greek;
            folded[unit] = strips ? base.unicode() : caseFolded.unicode();
        }
        // A stroke or a missing dot is part of the letter, not a mark on it,
        // so these have no decomposition to strip.
        const std::pair<char16_t, char16_t> plain[] = {
            {u'\u0142', u'l'}, {u'\u0111', u'd'}, {u'\u00f8', u'o'},
            {u'\u0127', u'h'}, {u'\u0131', u'i'}};
        for (char16_t &unit : folded) {
            for (const auto &[from, to] : plain) {
                if (unit == from)
                    unit = to;
            }
        }
        return folded;
    }();
    return table.data();
}

// Romanian is written with ș and ț but much text has the older ş and ţ.
QChar commaBelow(QChar c) {
    if (c == QChar(0x015F))
        return QChar(0x0219);
    if (c == QChar(0x0163))
        return QChar(0x021B);
    return c;
}

// The text folded unit by unit through the table, then each surrogate pair
// case-folded as the one letter it encodes, which stays a pair: 𐐀 becomes 𐐨.
QString searchFolded(const QString &text) {
    const char16_t *fold = searchFoldTable();
    QString folded(text.size(), Qt::Uninitialized);
    const QChar *source = text.constData();
    QChar *target = folded.data();
    for (qsizetype i = 0; i < text.size(); ++i)
        target[i] = QChar(fold[source[i].unicode()]);
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (!source[i].isHighSurrogate() || !source[i + 1].isLowSurrogate())
            continue;
        const char32_t caseFolded = QChar::toCaseFolded(QChar::surrogateToUcs4(source[i], source[i + 1]));
        if (QChar::requiresSurrogates(caseFolded)) {
            target[i] = QChar(QChar::highSurrogate(caseFolded));
            target[i + 1] = QChar(QChar::lowSurrogate(caseFolded));
        }
        ++i;
    }
    return folded;
}

} // namespace

QList<int> MarkdownHighlighter::findMatches(const QString &text, const QString &query) {
    QList<int> matches;
    const qsizetype length = query.size();
    if (length == 0 || text.size() < length)
        return matches;

    const QString foldedQuery = searchFolded(query);
    // The query's accented letters, case-folded, which the text must match
    // as written rather than folded.
    QList<std::pair<qsizetype, QChar>> exact;
    for (qsizetype i = 0; i < length; ++i) {
        const QChar unit = query.at(i);
        const QChar caseFolded = unit.toCaseFolded();
        if (!unit.isSurrogate() && foldedQuery.at(i) != caseFolded)
            exact.append({i, commaBelow(caseFolded)});
    }

    const QString foldedText = searchFolded(text);

    qsizetype from = 0;
    while ((from = foldedText.indexOf(foldedQuery, from)) >= 0) {
        const bool accentsMatch = std::all_of(exact.cbegin(), exact.cend(), [&](const auto &letter) {
            return commaBelow(text.at(from + letter.first).toCaseFolded()) == letter.second;
        });
        if (accentsMatch) {
            matches.append(int(from));
            from += length;
        } else {
            ++from;
        }
    }
    return matches;
}

void MarkdownHighlighter::highlightSearch(const QString &text) {
    if (m_searchQuery.isEmpty())
        return;

    for (const int from : findMatches(text, m_searchQuery)) {
        const int documentStart = currentBlock().position() + from;
        QTextCharFormat format = this->format(from);
        format.setBackground(documentStart == m_currentMatchStart
                                 ? m_currentSearchFormat.background()
                                 : m_searchFormat.background());
        setFormat(from, m_searchQuery.length(), format);
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

    if (isThematicBreak(text)) {
        setFormat(0, text.length(), m_markerFormat);
        return;
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

// A line of three or more -, * or _ (spaces allowed between) is a rule, not
// emphasis: "***" and "___" draw a line.
bool MarkdownHighlighter::isThematicBreak(const QString &text) {
    static const QRegularExpression ruleRe(QStringLiteral("^\\s{0,3}([-*_])(?:\\s*\\1){2,}\\s*$"));
    return ruleRe.match(text).hasMatch();
}

void MarkdownHighlighter::highlightInline(const QString &text, bool active) {
    // Inline styles merge onto what the block already set, so a heading stays
    // bold and a quote keeps its colour under emphasis.

    // Code keeps its own styling even when emphasis or a link encloses it. Its
    // backticks are markers like any other, hidden off the caret's line.
    QList<bool> inCode(text.length(), false);
    const QList<Span> code = codeSpans(text);
    for (const Span &span : code) {
        mergeFormat(span.start, span.length, m_codeFormat);
        for (int i = span.start; i < span.start + span.length; ++i)
            inCode[i] = true;
    }

    // Outer markup first, so what it encloses has the last word: a link inside
    // bold keeps its colour. Strikethrough draws over code too.
    QList<InlineMarkup> markup = inlineMarkup(text);
    std::stable_sort(markup.begin(), markup.end(),
                     [](const InlineMarkup &a, const InlineMarkup &b) {
                         return a.content.length > b.content.length;
                     });
    for (const InlineMarkup &item : markup) {
        const QTextCharFormat &contentFormat =
            item.kind == InlineKind::Bold ? m_boldFormat
            : item.kind == InlineKind::Italic ? m_italicFormat
            : item.kind == InlineKind::Strikethrough ? m_strikeFormat
                                                     : m_linkFormat;
        for (int i = item.content.start; i < item.content.start + item.content.length; ++i) {
            if (!inCode[i] || item.kind == InlineKind::Strikethrough)
                mergeFormat(i, 1, contentFormat);
        }
    }

    // Last, so no enclosing style shows a marker that should be hidden.
    for (const Span &span : code) {
        for (const int marker : {span.start, span.start + span.length - 1}) {
            if (active)
                mergeFormat(marker, 1, m_codeMarkerFormat);
            else
                setFormat(marker, 1, m_hiddenCodeMarkerFormat);
        }
    }
    for (const InlineMarkup &item : markup) {
        for (const Span &marker : item.markers) {
            if (active)
                mergeFormat(marker.start, marker.length, m_markerFormat);
            else
                setFormat(marker.start, marker.length, m_hiddenMarkerFormat);
        }
    }
}

QList<MarkdownHighlighter::Span> MarkdownHighlighter::codeSpans(const QString &text) {
    QList<Span> spans;
    if (!text.contains(QLatin1Char('`')))
        return spans;

    static const QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    int from = 0;
    while ((from = text.indexOf(QLatin1Char('`'), from)) >= 0) {
        int backslashes = 0;
        for (int i = from - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
            ++backslashes;
        if (backslashes % 2 != 0) {
            ++from;
            continue;
        }

        // Backslashes escape opening backticks in prose, but are literal once
        // inside code, including immediately before the closing backtick.
        const auto match = codeRe.match(text, from, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchorAtOffsetMatchOption);
        if (!match.hasMatch()) {
            ++from;
            continue;
        }
        spans.append({from, int(match.capturedLength(0))});
        from = match.capturedEnd(0);
    }
    return spans;
}

QList<MarkdownHighlighter::InlineMarkup> MarkdownHighlighter::inlineMarkup(const QString &text) {
    QList<InlineMarkup> markup;
    if (!text.contains(QLatin1Char('*')) && !text.contains(QLatin1Char('_'))
            && !text.contains(QLatin1Char('[')) && !text.contains(QLatin1Char('~'))) {
        return markup;
    }
    if (isThematicBreak(text))
        return markup;

    const auto span = [](const QRegularExpressionMatch &match, int group) {
        return Span{int(match.capturedStart(group)), int(match.capturedLength(group))};
    };

    // Inline code is literal, so a `*`, `_` or `[` inside backticks is not a
    // marker: `default_line_height` keeps its underscores. Markup whose
    // markers land in a code span is dropped; markup that merely wraps one
    // (**bold with `code` inside**) still applies.
    // A backslash escapes a marker, as it does a backtick: \\*literal\\*.
    const QList<Span> code = codeSpans(text);
    const auto escaped = [&text](int position) {
        int backslashes = 0;
        for (int i = position - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
            ++backslashes;
        return backslashes % 2 != 0;
    };
    const auto append = [&](const InlineMarkup &item) {
        for (const Span &marker : item.markers) {
            if (escaped(marker.start))
                return false;
            for (const Span &span : code) {
                if (marker.start < span.start + span.length
                        && span.start < marker.start + marker.length)
                    return false;
            }
        }
        markup.append(item);
        return true;
    };

    // "***word***" is bold and italic at once, as Ctrl+B then Ctrl+I writes it:
    // the outer two asterisks of each run are bold, the inner one italic. Its
    // runs are then masked so the patterns below do not split them again.
    QString unclaimed = text;
    static const QRegularExpression boldItalicRe(
        QStringLiteral("(?<!\\*)\\*\\*\\*(?![\\s*])(.*?[^\\s*])\\*\\*\\*(?!\\*)"));
    QRegularExpressionMatchIterator boldItalicMatches = boldItalicRe.globalMatch(text);
    while (boldItalicMatches.hasNext()) {
        const QRegularExpressionMatch match = boldItalicMatches.next();
        const Span whole = span(match, 0);
        const int end = whole.start + whole.length;
        if (!append({InlineKind::Bold, {whole.start + 2, whole.length - 4},
                     {{whole.start, 2}, {end - 2, 2}}}))
            continue;
        markup.append({InlineKind::Italic, span(match, 1),
                       {{whole.start + 2, 1}, {end - 3, 1}}});
        for (int i : {whole.start, whole.start + 1, whole.start + 2, end - 3, end - 2, end - 1})
            unclaimed[i] = QChar::ObjectReplacementCharacter;
    }

    // Underscores only delimit emphasis at a word boundary, so identifiers such
    // as snake_case_name read as themselves. Asterisks delimit anywhere.
    static const QRegularExpression boldRe(
        QStringLiteral("\\*\\*(.+?)\\*\\*|(?<!\\w)__(.+?)__(?!\\w)"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator boldMatches = boldRe.globalMatch(unclaimed);
    while (boldMatches.hasNext()) {
        const QRegularExpressionMatch match = boldMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        append({InlineKind::Bold, span(match, contentIndex),
                {{whole.start, 2}, {whole.start + whole.length - 2, 2}}});
    }

    // An asterisk delimiter may not be followed (opening) or preceded
    // (closing) by whitespace, so `* first *important* point` keeps its bullet
    // and `2 * 3 * 4` stays literal instead of pairing stray asterisks.
    static const QRegularExpression italicRe(
        QStringLiteral("(?<!\\*)\\*(?![\\s*])([^*\\n]*[^\\s*])\\*(?!\\*)|(?<!\\w)_([^_\\n]+)_(?!\\w)"),
        QRegularExpression::UseUnicodePropertiesOption);
    QRegularExpressionMatchIterator italicMatches = italicRe.globalMatch(unclaimed);
    while (italicMatches.hasNext()) {
        const QRegularExpressionMatch match = italicMatches.next();
        const Span whole = span(match, 0);
        const int contentIndex = match.capturedStart(1) >= 0 ? 1 : 2;
        append({InlineKind::Italic, span(match, contentIndex),
                {{whole.start, 1}, {whole.start + whole.length - 1, 1}}});
    }

    // Exactly two tildes, opening before text and closing after it.
    static const QRegularExpression strikeRe(
        QStringLiteral("(?<!~)~~(?![\\s~])(.*?[^\\s~])~~(?!~)"));
    QRegularExpressionMatchIterator strikeMatches = strikeRe.globalMatch(unclaimed);
    while (strikeMatches.hasNext()) {
        const QRegularExpressionMatch match = strikeMatches.next();
        const Span whole = span(match, 0);
        append({InlineKind::Strikethrough, span(match, 1),
                {{whole.start, 2}, {whole.start + whole.length - 2, 2}}});
    }

    static const QRegularExpression linkRe(
        QStringLiteral("\\[([^\\]]+)\\]\\(((?:\\\\.|\\([^()]*\\)|[^()])+)\\)"));
    QRegularExpressionMatchIterator linkMatches = linkRe.globalMatch(unclaimed);
    while (linkMatches.hasNext()) {
        const QRegularExpressionMatch match = linkMatches.next();
        const Span whole = span(match, 0);
        const Span content = span(match, 1);
        const int contentEnd = content.start + content.length;
        append({InlineKind::Link, content,
                {{whole.start, 1},
                 {contentEnd, whole.start + whole.length - contentEnd}},
                span(match, 2)});
    }

    return markup;
}

QString MarkdownHighlighter::linkUrlAt(const QString &text, int position) {
    if (position < 0 || position >= text.length())
        return {};

    const QList<InlineMarkup> markup = inlineMarkup(text);
    for (const InlineMarkup &item : markup) {
        if (item.kind != InlineKind::Link
                || position < item.content.start
                || position >= item.content.start + item.content.length) {
            continue;
        }
        // Markdown escapes punctuation in a destination, as the editor does
        // for parentheses; show the URL itself.
        static const QRegularExpression escapedPunctuation(
            QStringLiteral(R"(\\([!-/:-@\[-`{-~]))"));
        return text.mid(item.destination.start, item.destination.length)
            .replace(escapedPunctuation, QStringLiteral("\\1"));
    }
    return {};
}
