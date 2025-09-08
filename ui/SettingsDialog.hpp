// Settings dialog: language, hidden files, click mode, and startup behavior.
#pragma once
#include <QDialog>

class QComboBox;
class QPushButton;
class QCheckBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onApply(); // save without closing; disable until further changes
    void updateApplyFromControls(); // enable Apply if any option differs from persisted

private:
    QComboBox* langCombo_  = nullptr;   // es/en
    QCheckBox* showHidden_ = nullptr;   // show hidden files
    QComboBox* clickMode_  = nullptr;   // single click vs double click
    QCheckBox* showConnOnStart_ = nullptr; // show site manager at startup and when closing last session
    QPushButton* applyBtn_ = nullptr;   // Apply button (enabled only when modified)
    QPushButton* closeBtn_ = nullptr;   // Close button (never primary/default)
};
