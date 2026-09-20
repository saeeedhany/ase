#ifndef ASE_KEYBINDINGS_H
#define ASE_KEYBINDINGS_H

#include <QKeySequence>
#include <QString>
#include <QStringList>
#include <QVector>

class QKeyEvent;
class CommandRegistry;
struct AseConfig;

/*
 * Which chord runs which command.
 *
 * The defaults are a table rather than a chain of `if (key == ...)`, so
 * the same lookup serves a user's `key.ctrl+s = editor.compile` and the
 * built-in binding it replaces. See docs/adr/0113.
 */
namespace keys {

/* A chord, or two chords separated by '>' — `ctrl+w>j` is "Ctrl+W, then
 * j". '>' because it cannot occur inside a chord: '.' is already the
 * mode separator and ',' is the comma key. See docs/adr/0120. */
constexpr char kSequenceSeparator = '>';

struct Binding {
    const char *chord;   /* canonical form, as core/src/keymap.c spells it */
    const char *command;
    const char *mode;    /* "", or "normal"/"insert"/"visual" for a Vim-only binding */
};

/* True when any binding starts with `chord` and continues — i.e. the
 * chord is a prefix rather than a command in its own right. */
bool isPrefix(const AseConfig *config, const QString &chord);

/* The command bound to `prefix` followed by `second`, or empty. */
QString commandForSequence(const AseConfig *config, const QString &prefix, const QString &second);

/* Everything `prefix` can be followed by, as "key  command" lines, for
 * showing the user what their options are. */
QStringList sequenceHints(const AseConfig *config, const QString &prefix);

/* A chord as a person writes it: "ctrl+shift+f" -> "Ctrl+Shift+F",
 * "equal" -> "=". For anything shown to the user. */
QString pretty(const QString &chord);

/* The built-in table. */
const QVector<Binding> &defaults();

/* The chord a key press names, canonically, or empty when the event
 * carries no key this editor can name. */
QString chordFor(const QKeyEvent *event);

/* The command bound to `chord`, preferring a binding for `mode` over an
 * unqualified one, and the user's config over the defaults. Empty when
 * nothing is bound; the literal "none" when a binding disables a
 * default. */
QString commandFor(const AseConfig *config, const QString &chord, const QString &mode);

/* Every problem in the user's `key.*` settings: an unparseable chord, or
 * a command no one registered. Returned rather than printed so the
 * caller decides how to say it. */
/*
 * vim's `nnoremap`, as config: `vim.normal.Y = y$` makes Y behave as
 * y$. The value is a key sequence replayed through the Normal-mode
 * dispatcher, so counts and a pending operator compose with it for
 * free. Empty when the key is not remapped.
 *
 * `vim.<key>` applies in both Normal and Visual; `vim.normal.<key>` and
 * `vim.visual.<key>` are one each, and beat the unqualified form.
 * See docs/adr/0134.
 */
QString vimRemap(const AseConfig *config, const QString &mode, QChar key);

QStringList problems(const AseConfig *config, const CommandRegistry &registry,
                     const QStringList &alsoKnown = QStringList());

/* Every chord that runs `command`: the user's bindings for it, plus any
 * default the user has not pointed somewhere else. The reverse of
 * commandFor, for the window shortcuts, which have to be built from a
 * chord rather than looked up when one arrives. */
QStringList chordsFor(const AseConfig *config, const QString &command);

/* A canonical chord as Qt spells it, for a QShortcut. Empty when Qt has
 * no sequence for it. */
QKeySequence sequenceFor(const QString &chord);

} // namespace keys

#endif
