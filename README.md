# Omawrite Plus

A fork of [Omawrite](https://github.com/omacom-io/omawrite), the dead-simple Markdown
writing app built with Qt Quick and C++ that automatically follows system dark/light
mode. Omawrite Plus adds:

- **Live preview.** Markdown renders as you type: sized headings, hidden markers,
  code blocks, task lists, and strikethrough. The line holding the caret shows its
  raw Markdown.
- **Front matter.** YAML front matter is set apart, and can be folded away.
- **Text size.** Pick the writing size in the font picker too.
- **Spell check.** Hunspell underlines misspellings, with suggestions on right-click.
- **Typewriter sounds.** An IBM Selectric II as you type, and a bell on `Enter`.

The screenshots below are of the original Omawrite.

<img width="2948" height="3227" alt="screenshot-2026-06-23_15-24-08" src="https://github.com/user-attachments/assets/4e930c0d-edda-4046-b444-a59eff523329" />
<img width="2948" height="3227" alt="screenshot-2026-06-23_15-23-23" src="https://github.com/user-attachments/assets/8ced7c26-961b-4ded-b263-84403001a951" />


## Install

Build and install the `omawrite-plus` package from this repository. It replaces the
`omawrite` package from the Omarchy Package Repository and keeps its `omawrite`
command, settings, and file associations.

```sh
cd pkgbuild
makepkg -f
sudo pacman -U omawrite-plus-*.pkg.tar.*
```

## Command line

`omawrite` opens an empty document and `omawrite FILE` opens a Markdown file.
`omawrite --help` and `omawrite --version` answer in the terminal without opening a window.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+N` opens a new Omawrite Plus window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+Shift+X` toggle bold, italic, and strikethrough Markdown. `Ctrl+K` inserts a link.
- `Ctrl+Shift+M` shows or hides the document's front matter.
- `Ctrl+=` and `Ctrl+-` change the text size, `Ctrl+0` returns to the default.
  `Ctrl+scroll` does the same.
- `Ctrl+Shift+C` copies the rendered document as HTML, with plain text as a fallback.
- `Ctrl+Shift+T` turns typewriter sounds on or off.
- `F7` turns spell check on or off.
- `Ctrl+?`, `Ctrl+/`, or `F1` shows the keyboard shortcut reference.
- In the unsaved-changes dialog, `Alt+C`, `Alt+D`, and `Alt+S` choose Cancel,
  Discard, and Save.

Once a document has been saved to a file, edits are auto-saved to that file a
moment after you stop typing, so `Ctrl+S` is rarely needed. Untitled drafts are
never auto-saved — they wait for an explicit save so Omawrite Plus never picks a file
name for you. Unsaved drafts are recovered after an abnormal exit. Omawrite Plus also
watches open files and warns before an external change can replace local work;
auto-save pauses until you reload, keep your version, or save. A recovered draft
is never auto-saved over its file — save it to keep it.

YAML front matter at the top of a document (between `---` fences) is set apart in
a smaller face with its keys highlighted. The eye button in the footer, or
`Ctrl+Shift+M`, folds it down to its opening fence; moving the caret onto that
line unfolds it while you edit. Omawrite Plus remembers the choice.

The font button in the footer picks the writing font from any installed text font,
and Omawrite Plus remembers the choice. IBM Plex Mono is the default.

The font picker also sets the writing text size. Until you pick one, text follows
the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
Omawrite Plus at the size it is designed around; larger and smaller sizes scale from there.
The size row in the font picker, `Ctrl+=`, `Ctrl+-`, and `Ctrl+scroll` set your own
size on top of that, from 50% to 300% in browser-sized steps. It is remembered for the
next window you open.

Misspelled words get a red underline, leaving out code, front matter, link
addresses, and the word you are still typing. Right-click one for suggestions or
to add it to your dictionary. The tick button in the footer, or `F7`, turns spell
check off. It uses Hunspell with the dictionary for your locale, so install one
such as `hunspell-en_us`.

The speaker button in the footer, or `Ctrl+Shift+T`, turns on typewriter sounds:
an IBM Selectric II for typing, and a margin bell and carriage return for `Enter`.
They are off by default. The sounds are cut from two CC0 recordings on Freesound,
[secretmojo's Selectric II](https://freesound.org/people/secretmojo/sounds/224012/)
and [knufds' bell and carriage reset](https://freesound.org/people/knufds/sounds/345955/),
by `sounds/extract.py`.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative` (which includes Qt Quick Controls), `qt6-multimedia`
- `hunspell` and a Hunspell dictionary such as `hunspell-en_us`
- `xdg-desktop-portal` and a portal backend

The IBM Plex Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright IBM Corp.

## Build and install

For a local build, install the Qt requirements above, a C++17 compiler such as
`gcc`, and `make`. The build scripts use `qmake6` (or `qmake`), and the tests use
QtTest; both ship in `qt6-base`.

```sh
./bin/build
./bin/test
./build/omawrite
```

`bin/test` runs the Qt test suite with the offscreen platform, so it does not
require an active graphical session. To open a file with the local build:

```sh
./build/omawrite path/to/document.md
```

On Arch Linux or Omarchy, install `base-devel` for the packaging tools, then
build and install the package:

```sh
./bin/install
```

This runs the local build and `makepkg -fsi`, which installs the package through
pacman and may ask for authentication. To build a package archive for inspection
without installing it:

```sh
cd pkgbuild
makepkg -f
```
