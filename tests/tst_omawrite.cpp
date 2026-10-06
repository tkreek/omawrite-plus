#include <QtTest>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

#include "backend.h"
#include "markdownhighlighter.h"
#include "spellchecker.h"

class OmawriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDirectory.isValid());
        QQuickStyle::setStyle(QStringLiteral("Material"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDirectory.path());
    }

    void countsWords() {
        QCOMPARE(Backend::countWords(QStringLiteral("one two-three don't 42")), 4);
        QCOMPARE(Backend::countWords(QStringLiteral("你好 世界")), 2);
        QCOMPARE(Backend::countWords(QString()), 0);
    }

    void normalizesLinks() {
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("www.example.com/path")),
                 QStringLiteral("https://www.example.com/path"));
        QCOMPARE(Backend::normalizedLinkUrl(QStringLiteral("mailto:writer@example.com")),
                 QStringLiteral("mailto:writer@example.com"));
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("example.com")).isEmpty());
        QVERIFY(Backend::normalizedLinkUrl(QStringLiteral("file:///tmp/private")).isEmpty());
    }

    void suggestsSafeNames() {
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("My first draft\nBody")),
                 QStringLiteral("My first draft.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("A/B")), QStringLiteral("A-B.md"));
        QCOMPARE(Backend::suggestedFileName(QString()), QStringLiteral("Untitled.md"));
        QCOMPARE(Backend::suggestedFileName(QStringLiteral("Already.md")),
                 QStringLiteral("Already.md"));
    }

    void keepsNotoFromCrowdingTheFontList() {
        const QStringList fonts = Backend::selectableFontFamilies({
            QStringLiteral("Noto Sans Devanagari"), QStringLiteral("Liberation Serif"),
            QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Tamil UI"),
            QStringLiteral("adwaita Sans"), QStringLiteral("IBM Plex Mono"),
            QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans Mono"),
            QStringLiteral("Liberation Serif"), QStringLiteral("Monospace"),
            QStringLiteral("Standard Symbols PS"), QStringLiteral("D050000L"),
            QStringLiteral("Noto Color Emoji"), QStringLiteral("Nimbus Sans [UKWN]"),
            QStringLiteral("Nimbus Sans [URW ]")});
        QCOMPARE(fonts, QStringList({QStringLiteral("IBM Plex Mono"),
                                     QStringLiteral("adwaita Sans"),
                                     QStringLiteral("Liberation Serif"),
                                     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Noto Sans"),
                                     QStringLiteral("Noto Sans Mono"),
                                     QStringLiteral("Noto Serif")}));
    }

    void remembersEditorFont() {
        const QString installed = Backend().availableFonts().value(1);
        QVERIFY(!installed.isEmpty());

        {
            Backend backend;
            QCOMPARE(backend.editorFont(), Backend::defaultEditorFont());
            QSignalSpy fontSpy(&backend, &Backend::editorFontChanged);
            backend.setEditorFont(installed);
            QCOMPARE(fontSpy.count(), 1);
        }

        QCOMPARE(Backend().editorFont(), installed);

        // A remembered font that has since been uninstalled falls back.
        QSettings().setValue(QStringLiteral("editor/font"), QStringLiteral("No Such Font"));
        QCOMPARE(Backend().editorFont(), Backend::defaultEditorFont());
        QSettings().remove(QStringLiteral("editor/font"));
    }

    void findsInlineMarkdownRanges() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("**bold** and *italic* and [site](https://example.com)"));
        QCOMPARE(markup.size(), 3);
        QCOMPARE(markup.at(0).content.start, 2);
        QCOMPARE(markup.at(0).content.length, 4);
        QCOMPARE(markup.at(2).content.length, 4);
        QCOMPARE(markup.at(2).markers[0].length, 1);
    }

    void keepsCodeSpansLiteral() {
        const auto markup = MarkdownHighlighter::inlineMarkup(
            QStringLiteral("`**not bold**` and ~~gone~~"));
        QCOMPARE(markup.size(), 2);
        QCOMPARE(markup.at(0).kind, MarkdownHighlighter::InlineKind::Strike);
        QCOMPARE(markup.at(0).content.start, 21);
        QCOMPARE(markup.at(1).kind, MarkdownHighlighter::InlineKind::Code);
        QCOMPARE(markup.at(1).content.length, 12);
    }

    void revealsMarkdownOnlyOnTheActiveLine() {
        QTextDocument document;
        QFont font;
        font.setPixelSize(20);
        document.setDefaultFont(font);
        document.setPlainText(QStringLiteral("# Title\n**bold**\n```\ncode\n```"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();

        const auto formatAt = [&](int blockNumber, int column) {
            const QTextBlock block = document.findBlockByNumber(blockNumber);
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (column >= range.start && column < range.start + range.length)
                    return range.format;
            }
            return QTextCharFormat();
        };
        const auto hidden = [&](int blockNumber, int column) {
            return formatAt(blockNumber, column).fontPointSize() == 1.0;
        };

        QVERIFY(hidden(0, 0));
        QVERIFY(hidden(1, 0));
        QCOMPARE(formatAt(0, 2).intProperty(QTextFormat::FontPixelSize), 32);
        QVERIFY(formatAt(3, 0).background().style() != Qt::NoBrush);

        highlighter.setActiveBlock(1);
        QVERIFY(hidden(0, 0));
        QVERIFY(!hidden(1, 0));
        QCOMPARE(formatAt(1, 3).fontWeight(), int(QFont::Bold));

        highlighter.setActiveBlock(0);
        QVERIFY(!hidden(0, 0));
        QCOMPARE(formatAt(0, 0).intProperty(QTextFormat::FontPixelSize), 32);
        QVERIFY(hidden(1, 0));
    }

    void detectsFrontMatter() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("---\ntitle: Hi\n---\n# Body"));
        QCOMPARE(MarkdownHighlighter::frontMatterEnd(&document), 2);

        document.setPlainText(QStringLiteral("---\ntitle: Hi\n# Never closed"));
        QCOMPARE(MarkdownHighlighter::frontMatterEnd(&document), -1);

        document.setPlainText(QStringLiteral("Intro\n---\ntitle: Hi\n---"));
        QCOMPARE(MarkdownHighlighter::frontMatterEnd(&document), -1);
    }

    void foldsHiddenFrontMatterUntilTheCaretEntersIt() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("---\ntitle: Hi\ntags: [a]\n---\n# Body"));
        MarkdownHighlighter highlighter(&document);
        highlighter.rehighlight();
        QVERIFY(highlighter.hasFrontMatter());

        const auto visible = [&](int blockNumber) {
            return document.findBlockByNumber(blockNumber).isVisible();
        };

        highlighter.setActiveBlock(4);
        highlighter.setFrontMatterHidden(true);
        QVERIFY(visible(0));
        QVERIFY(!visible(1));
        QVERIFY(!visible(3));
        QVERIFY(visible(4));

        highlighter.setActiveBlock(0);
        QVERIFY(visible(1) && visible(3));

        highlighter.setActiveBlock(4);
        QVERIFY(!visible(2));

        // Breaking the closing fence turns the block back into plain Markdown.
        QTextCursor cursor(document.findBlockByNumber(3));
        cursor.insertText(QStringLiteral("x"));
        highlighter.updateFrontMatter();
        QVERIFY(!highlighter.hasFrontMatter());
        QVERIFY(visible(1) && visible(2) && visible(3));
    }

    void remembersTypewriterSounds() {
        QCOMPARE(Backend().typewriterSounds(), false);

        Backend backend;
        QSignalSpy spy(&backend, &Backend::typewriterSoundsChanged);
        backend.setTypewriterSounds(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(Backend().typewriterSounds(), true);

        backend.setTypewriterSounds(false);
        QCOMPARE(Backend().typewriterSounds(), false);
    }

    void findsWordsWorthChecking() {
        const QString text = QStringLiteral("Visit https://ex.com or me@ex.com, it\u2019s NASA 3rd teh");
        QStringList found;
        for (const SpellChecker::Word &word : SpellChecker::words(text))
            found.append(text.mid(word.start, word.length));
        QCOMPARE(found, QStringList({QStringLiteral("Visit"), QStringLiteral("or"),
                                     QStringLiteral("it\u2019s"), QStringLiteral("teh")}));
    }

    void underlinesMisspelledProse() {
        if (SpellChecker::dictionaryPath(QStringLiteral("en_US")).isEmpty())
            QSKIP("No en_US Hunspell dictionary installed (set DICPATH to use another).");

        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(SpellChecker::personalDictionaryPath());
        SpellChecker checker(QStringLiteral("en_US"));
        QVERIFY(checker.isAvailable());
        QVERIFY(checker.isCorrect(QStringLiteral("doesn\u2019t")));
        QVERIFY(!checker.isCorrect(QStringLiteral("teh")));
        QVERIFY(checker.suggestions(QStringLiteral("teh")).contains(QStringLiteral("the")));

        QTextDocument document;
        document.setPlainText(QStringLiteral("I saw teh `teh` [teh](https://tehx.com)\n```\nteh\n```"));
        MarkdownHighlighter highlighter(&document);
        highlighter.setSpellChecker(&checker);
        highlighter.rehighlight();

        const auto misspelled = [&](int blockNumber, int column) {
            const QTextBlock block = document.findBlockByNumber(blockNumber);
            for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
                if (column >= range.start && column < range.start + range.length)
                    return range.format.boolProperty(MarkdownHighlighter::MisspelledProperty);
            }
            return false;
        };

        QVERIFY(misspelled(0, 6));    // teh
        QVERIFY(!misspelled(0, 11));  // `teh`
        QVERIFY(misspelled(0, 17));   // [teh] link text
        QVERIFY(!misspelled(0, 30));  // the link's address
        QVERIFY(!misspelled(2, 0));   // fenced code

        // The word being typed is left alone until the caret leaves it.
        highlighter.setActiveBlock(0, 9);
        QVERIFY(!misspelled(0, 6));
        highlighter.setActiveBlock(0, 2);
        QVERIFY(misspelled(0, 6));

        checker.addWord(QStringLiteral("teh"));
        QVERIFY(SpellChecker(QStringLiteral("en_US")).isCorrect(QStringLiteral("teh")));
        highlighter.rehighlight();
        QVERIFY(!misspelled(0, 6));

        highlighter.setSpellChecker(nullptr);
        QFile::remove(SpellChecker::personalDictionaryPath());
        QStandardPaths::setTestModeEnabled(false);
    }

    void offersSpellingSuggestionsInTheEditor() {
        if (SpellChecker::dictionaryPath().isEmpty())
            QSKIP("No Hunspell dictionary installed for the system locale.");

        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        Backend backend;
        backend.setSpellCheck(true);
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);

        editor->setProperty("text", QStringLiteral("Fix teh word"));
        editor->setProperty("cursorPosition", 0);
        QCoreApplication::processEvents();

        const QVariantMap misspelling = backend.misspellingAt(5);
        QCOMPARE(misspelling.value(QStringLiteral("word")).toString(), QStringLiteral("teh"));
        QCOMPARE(misspelling.value(QStringLiteral("start")).toInt(), 4);
        QVERIFY(misspelling.value(QStringLiteral("suggestions")).toStringList()
                    .contains(QStringLiteral("the")));
        QVERIFY(backend.misspellingAt(1).isEmpty());

        backend.setSpellCheck(false);
        QVERIFY(backend.misspellingAt(5).isEmpty());
        QVERIFY(window->findChild<QObject *>(QStringLiteral("spellingMenu")));
    }

    void loadsCurrentOmarchyTheme() {
        QTemporaryDir homeDirectory;
        QVERIFY(homeDirectory.isValid());

        const QByteArray originalHome = qgetenv("HOME");
        struct HomeRestorer {
            QByteArray value;
            ~HomeRestorer() { qputenv("HOME", value); }
        } restoreHome{originalHome};
        QVERIFY(qputenv("HOME", homeDirectory.path().toUtf8()));

        const QString themeDirectory = homeDirectory.path()
            + QStringLiteral("/.local/state/omarchy/current/theme");
        QVERIFY(QDir().mkpath(themeDirectory));

        QFile colorsFile(themeDirectory + QStringLiteral("/colors.toml"));
        QVERIFY(colorsFile.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray palette(
            "mode = \"light\"\n"
            "accent = \"#112233\"\n"
            "selection = \"#445566\"\n"
            "background = \"#fefefe\"\n"
            "foreground = \"#101010\"\n");
        QCOMPARE(colorsFile.write(palette), qint64(palette.size()));
        colorsFile.close();

        Backend backend;
        QCOMPARE(backend.themeBackground(), QStringLiteral("#fefefe"));
        QCOMPARE(backend.themeForeground(), QStringLiteral("#101010"));
        QCOMPARE(backend.themeAccent(), QStringLiteral("#112233"));
        QCOMPARE(backend.themeSelection(), QStringLiteral("#445566"));
        QVERIFY(!backend.darkMode());
    }

    void ignoresFileWatcherEventsForSavedContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("first-save.md"));
        Backend backend;
        QSignalSpy externalChangeSpy(&backend, &Backend::externalChangeDetected);

        backend.saveAs(QUrl::fromLocalFile(path));
        QVERIFY(QFileInfo::exists(path));

        QFile sameContents(path);
        QVERIFY(sameContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sameContents.close();
        QTest::qWait(100);
        QCOMPARE(externalChangeSpy.count(), 0);

        QFile changedContents(path);
        QVERIFY(changedContents.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(changedContents.write("changed elsewhere"), qint64(17));
        changedContents.close();
        QTRY_COMPARE(externalChangeSpy.count(), 1);
    }

    void keepsCursorAndSelectionStableAcrossInsertions() {
        const QString mutationsPath = QFINDTESTDATA("../src/EditorMutations.js");
        QVERIFY(!mutationsPath.isEmpty());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray harness = R"QML(
            import QtQuick
            import "EditorMutations.js" as EditorMutations

            TextEdit {
                property string insertionText
                property int insertionCursor
                property string wrappedText
                property int wrappedSelectionStart
                property int wrappedSelectionEnd

                Component.onCompleted: {
                    text = "alpha omega";
                    cursorPosition = 5;
                    EditorMutations.replaceRange(this, 5, 5, "one\r\ntwo");
                    insertionText = text;
                    insertionCursor = cursorPosition;

                    text = "alpha beta omega";
                    select(6, 10);
                    EditorMutations.replaceRange(this, selectionStart, selectionEnd,
                                                 "**beta**", 2, 6);
                    wrappedText = text;
                    wrappedSelectionStart = selectionStart;
                    wrappedSelectionEnd = selectionEnd;
                }
            }
        )QML";
        const QUrl harnessUrl = QUrl::fromLocalFile(
            QFileInfo(mutationsPath).absolutePath() + QStringLiteral("/MutationHarness.qml"));
        component.setData(harness, harnessUrl);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));

        QCOMPARE(editor->property("insertionText").toString(),
                 QStringLiteral("alphaone\ntwo omega"));
        QCOMPARE(editor->property("insertionCursor").toInt(), 12);
        QCOMPARE(editor->property("wrappedText").toString(),
                 QStringLiteral("alpha **beta** omega"));
        QCOMPARE(editor->property("wrappedSelectionStart").toInt(), 8);
        QCOMPARE(editor->property("wrappedSelectionEnd").toInt(), 12);
    }

    void savesAndOpensFromFooterButtons() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QVERIFY(window->findChild<QObject *>(QStringLiteral("sourceEditor")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("renderedPreview")));
        QVERIFY(!window->findChild<QObject *>(QStringLiteral("modeToggle")));

        QObject *saveButton = window->findChild<QObject *>(QStringLiteral("saveButton"));
        QObject *openButton = window->findChild<QObject *>(QStringLiteral("openButton"));
        QVERIFY(saveButton);
        QVERIFY(openButton);

        QSignalSpy saveDialogSpy(&backend, &Backend::saveDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(saveButton, "clicked"));
        QCOMPARE(saveDialogSpy.count(), 1);

        QSignalSpy openDialogSpy(&backend, &Backend::openDialogRequested);
        QVERIFY(QMetaObject::invokeMethod(openButton, "clicked"));
        QCOMPARE(openDialogSpy.count(), 1);
    }

    void scalesTextWithDesktopTextSize() {
        const QString mainQmlPath = QFINDTESTDATA("../src/Main.qml");
        QVERIFY(!mainQmlPath.isEmpty());

        Backend backend;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));

        QObject *editor = window->findChild<QObject *>(QStringLiteral("sourceEditor"));
        QVERIFY(editor);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 20);

        // `omarchy display text size 16` sets the GNOME factor to 16/12.
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 27);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);

        backend.setTextScale(9.0 / 12.0);
        QCOMPARE(window->property("editorFontPixelSize").toInt(), 15);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 15);

        // A size picked in the app wins over the desktop until it is cleared.
        backend.setEditorFontSize(30);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 30);
        backend.setTextScale(16.0 / 12.0);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 30);
        QCOMPARE(Backend().editorFontSize(), 30);

        backend.setEditorFontSize(0);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);
        QCOMPARE(Backend().editorFontSize(), 0);

        backend.setEditorFontSize(500);
        QCOMPARE(backend.editorFontSize(), Backend::maximumEditorFontSize);
        backend.setEditorFontSize(0);

        // The font picker steps from whatever size is showing.
        QObject *larger = window->findChild<QObject *>(QStringLiteral("largerButton"));
        QObject *smaller = window->findChild<QObject *>(QStringLiteral("smallerButton"));
        QObject *system = window->findChild<QObject *>(QStringLiteral("systemSizeButton"));
        QVERIFY(larger && smaller && system);
        QVERIFY(QMetaObject::invokeMethod(larger, "activated"));
        QCOMPARE(backend.editorFontSize(), 28);
        QVERIFY(QMetaObject::invokeMethod(smaller, "activated"));
        QVERIFY(QMetaObject::invokeMethod(smaller, "activated"));
        QCOMPARE(backend.editorFontSize(), 26);
        QVERIFY(QMetaObject::invokeMethod(system, "activated"));
        QCOMPARE(backend.editorFontSize(), 0);
        QCOMPARE(editor->property("font").value<QFont>().pixelSize(), 27);
    }

    void remembersLastSaveDirectory() {
        QTemporaryDir saveDirectory;
        QVERIFY(saveDirectory.isValid());

        const QString savedPath = saveDirectory.filePath(QStringLiteral("first.md"));
        Backend savedDocument;
        savedDocument.saveAs(QUrl::fromLocalFile(savedPath));

        Backend nextDocument;
        QSignalSpy saveDialogSpy(&nextDocument, &Backend::saveDialogRequested);
        nextDocument.saveAsDialog();
        QCOMPARE(saveDialogSpy.count(), 1);

        const QUrl suggestedUrl = saveDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).absolutePath(),
                 saveDirectory.path());
        QCOMPARE(QFileInfo(suggestedUrl.toLocalFile()).fileName(),
                 QStringLiteral("Untitled.md"));

        QSettings().setValue(QStringLiteral("file/lastSaveDirectory"),
                             saveDirectory.filePath(QStringLiteral("missing")));
        Backend fallbackDocument;
        QSignalSpy fallbackDialogSpy(&fallbackDocument, &Backend::saveDialogRequested);
        fallbackDocument.saveAsDialog();
        const QUrl fallbackUrl = fallbackDialogSpy.takeFirst().constFirst().toUrl();
        QCOMPARE(QFileInfo(fallbackUrl.toLocalFile()).absolutePath(), QDir::homePath());
    }

private:
    QTemporaryDir m_settingsDirectory;
};

QTEST_MAIN(OmawriteTest)
#include "tst_omawrite.moc"
