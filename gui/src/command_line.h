#ifndef ASE_COMMAND_LINE_H
#define ASE_COMMAND_LINE_H

#include <QWidget>

class QLabel;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
class SmoothLineEdit;
class EditorViewport;

/*
 * The `:` line, living in the status bar rather than floating over the
 * document — see docs/adr/0073. The bar already carries the mode, the
 * message and the position, so command entry was the one part of vim's
 * bottom line that floated somewhere else.
 *
 * One instance for the window, re-pointed at whichever buffer is active
 * (the same shape OutputPanel uses), because there is one status bar.
 * While the prompt is up it takes the mode and message area; the
 * position and LSP readouts on the right stay put.
 */
class CommandLine : public QWidget {
    Q_OBJECT

public:
    explicit CommandLine(QWidget *parent = nullptr);

    /* Which buffer a typed command acts on. */
    void setViewport(EditorViewport *viewport);
    /* `prefix` is the character that opened it — ':' today, '/' and '?'
     * once search lands. It is shown, not typed into the field. */
    void openPrompt(QChar prefix);
    void closePrompt();
    bool isPromptOpen() const { return m_open; }
    void refreshTheme();

signals:
    /* The bar has to hide the mode and message labels while this is up,
     * and they belong to the window, not here. */
    void promptOpened();
    void promptClosed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void run();

    EditorViewport *m_viewport = nullptr;
    QLabel *m_prefix;
    SmoothLineEdit *m_edit;
    QGraphicsOpacityEffect *m_opacity;
    QPropertyAnimation *m_fade;
    bool m_open = false;
};

#endif /* ASE_COMMAND_LINE_H */
