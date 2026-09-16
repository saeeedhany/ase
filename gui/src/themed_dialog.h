#ifndef ASE_THEMED_DIALOG_H
#define ASE_THEMED_DIALOG_H

#include <QString>

class QWidget;
class EditorViewport;

/* A yes/no confirmation in the editor's own colours rather than the
 * desktop's. Returns true when the destructive button was chosen; the
 * Cancel button is the default, so Return and Escape both decline.
 * `theme` supplies the palette and may be null, in which case the
 * dialog falls back to the platform style. */
bool confirmDestructive(QWidget *parent, const EditorViewport *theme, const QString &title,
                        const QString &message, const QString &destructiveLabel);

#endif
