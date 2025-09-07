// Diálogo Acerca de OpenSCP.
#pragma once
#include <QDialog>

class QLabel;

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

