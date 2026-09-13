#ifndef ASE_VIM_REGISTER_H
#define ASE_VIM_REGISTER_H

#include <QByteArray>

/*
 * Vim's unnamed register — see docs/adr/0061.
 *
 * Before this existed, `y` and `d` wrote straight to the system
 * clipboard and `p` read from it, which got both halves wrong at once:
 * `dd` never filled anything (so vim's most common idiom, `dd` then
 * `p` to move a line, silently did nothing), and making it fill the
 * clipboard would have meant every `x` destroying whatever you last
 * copied from another application.
 *
 * So this is the register `y`, `d`, `c` and `x` write and `p`/`P` read.
 * The system clipboard is a separate destination, reached by Ctrl+C /
 * Ctrl+V (and by `y`, which mirrors into it — see the ADR for why yank
 * does and delete doesn't).
 *
 * **Process-wide, deliberately.** Each open buffer is its own
 * EditorViewport (docs/adr/0054), so a per-viewport register would mean
 * yanking in one file and pasting in another silently pasting the wrong
 * thing — the single most obvious use of a register there is.
 *
 * `linewise` is what separates `yy` from `yw`: it decides whether `p`
 * opens a new line below or inserts after the cursor. It travels with
 * the text because it is a property of *this* text, not of the paste.
 *
 * Named registers (`"a`), the numbered ring and the small-delete
 * register stay deferred (docs/adr/0046). This is the one vim uses when
 * you don't name one, which is almost always.
 */
class VimRegister {
public:
    static VimRegister &unnamed() {
        static VimRegister instance;
        return instance;
    }

    void set(const QByteArray &text, bool linewise) {
        m_text = text;
        m_linewise = linewise;
    }

    const QByteArray &text() const { return m_text; }
    bool isLinewise() const { return m_linewise; }
    bool isEmpty() const { return m_text.isEmpty(); }

private:
    VimRegister() = default;

    QByteArray m_text;
    bool m_linewise = false;
};

#endif /* ASE_VIM_REGISTER_H */
