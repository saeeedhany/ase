#include "themed_dialog.h"

#include "editor_viewport.h"

#include <QMessageBox>
#include <QPushButton>

bool confirmDestructive(QWidget *parent, const EditorViewport *theme, const QString &title,
                        const QString &message, const QString &destructiveLabel,
                        const QString &safeLabel) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::NoIcon);
    box.setWindowTitle(title);
    box.setText(message);
    QPushButton *destructive = box.addButton(destructiveLabel, QMessageBox::DestructiveRole);
    QPushButton *safe = box.addButton(
        safeLabel.isEmpty() ? QStringLiteral("Cancel") : safeLabel, QMessageBox::RejectRole);
    box.setDefaultButton(safe);
    if (theme != nullptr) {
        box.setStyleSheet(QStringLiteral("QMessageBox { background-color: %1; }"
                                          "QMessageBox QLabel { color: %2; }"
                                          "QPushButton { background-color: %1; color: %2; "
                                          "border: 1px solid %3; padding: 4px 14px; min-width: 60px; }"
                                          "QPushButton:hover, QPushButton:default { border-color: %2; }")
                               .arg(theme->panelBackgroundColor().name(), theme->textColor().name(),
                                    theme->panelBorderColor().name()));
    }
    box.exec();
    return box.clickedButton() == destructive;
}
