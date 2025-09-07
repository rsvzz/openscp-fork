// Implementación del diálogo de configuración de OpenSCP.
#include "SettingsDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QSettings>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Configuración"));

    auto* root = new QVBoxLayout(this);

    // Idioma
    {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Idioma:"), this));
        langCombo_ = new QComboBox(this);
        langCombo_->addItem("Español", "es");
        langCombo_->addItem("English", "en");
        row->addWidget(langCombo_, 1);
        root->addLayout(row);
    }

    // Cargar desde QSettings
    QSettings s("OpenSCP", "OpenSCP");
    const QString lang  = s.value("UI/language", "es").toString();
    const int li = langCombo_->findData(lang);
    if (li >= 0) langCombo_->setCurrentIndex(li);

    // Botones
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    root->addWidget(btns);
}

void SettingsDialog::accept() {
    const QString chosenLang  = langCombo_->currentData().toString();

    QSettings s("OpenSCP", "OpenSCP");
    const QString prevLang = s.value("UI/language", "es").toString();
    s.setValue("UI/language", chosenLang);
    s.sync();

    // Solo avisar si realmente cambió el idioma
    if (prevLang != chosenLang) {
        QMessageBox::information(this, tr("Idioma"), tr("El cambio de idioma se aplicará al reiniciar."));
    }

    QDialog::accept();
}
