// Diálogo de Configuración: idioma.
#pragma once
#include <QDialog>

class QComboBox;
class QPushButton;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void accept() override; // guarda y aplica

private:
    QComboBox* langCombo_  = nullptr;   // es/en
};
