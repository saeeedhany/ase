#ifndef ASE_THEMED_DIALOG_H
#define ASE_THEMED_DIALOG_H

#include <QString>

class QWidget;
class EditorViewport;

/* A two-button confirmation in the editor's own colours rather than the
 * desktop's. Returns true when the destructive button was chosen. The
 * safe button is the default, so Return and Escape both take the
 * reversible path. `theme` supplies the palette and may be null, in
 * which case the dialog falls back to the platform style.
 *
 * `safeLabel` names the other button; it defaults to Cancel, which is
 * right when the safe answer is "do nothing" and wrong when it is an
 * action of its own — recovering unsaved work, say. */
bool confirmDestructive(QWidget *parent, const EditorViewport *theme, const QString &title,
                        const QString &message, const QString &destructiveLabel,
                        const QString &safeLabel = QString());

#endif
