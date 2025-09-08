// Implementation of OpenSCP settings dialog.
#include "SettingsDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QMessageBox>
#include <QCheckBox>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Ajustes"));

    auto* root = new QVBoxLayout(this);

    // Language
    {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Idioma:"), this));
        langCombo_ = new QComboBox(this);
        langCombo_->addItem("Español", "es");
        langCombo_->addItem("English", "en");
        row->addWidget(langCombo_, 1);
        root->addLayout(row);
    }

    // Click mode (single or double)
    {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Apertura con:"), this));
        clickMode_ = new QComboBox(this);
        clickMode_->addItem(tr("Doble clic"), 2);
        clickMode_->addItem(tr("Un clic"), 1);
        row->addWidget(clickMode_, 1);
        root->addLayout(row);
    }

    // Show hidden files (at the end)
    {
        auto* row = new QHBoxLayout();
        showHidden_ = new QCheckBox(tr("Mostrar archivos ocultos"), this);
        row->addWidget(showHidden_);
        row->addStretch();
        root->addLayout(row);
    }

    // Show connection window on startup / when the last session closes
    {
        auto* row = new QHBoxLayout();
        showConnOnStart_ = new QCheckBox(tr("Mostrar ventana de conexión al inicio y cuando se cierre la última sesión."), this);
        row->addWidget(showConnOnStart_);
        row->addStretch();
        root->addLayout(row);
    }

    // Load from QSettings
    QSettings s("OpenSCP", "OpenSCP");
    const QString lang  = s.value("UI/language", "es").toString();
    const int li = langCombo_->findData(lang);
    if (li >= 0) langCombo_->setCurrentIndex(li);
    const bool showHidden = s.value("UI/showHidden", false).toBool();
    showHidden_->setChecked(showHidden);
    const bool showConnOnStart = s.value("UI/showConnOnStart", true).toBool();
    if (showConnOnStart_) showConnOnStart_->setChecked(showConnOnStart);
    const bool singleClick = s.value("UI/singleClick", false).toBool();
    clickMode_->setCurrentIndex(singleClick ? 1 : 0);

    // Buttons row: align to right, order: Close (left) then Apply (right)
    auto* btnRow = new QWidget(this);
    auto* hb = new QHBoxLayout(btnRow);
    hb->setContentsMargins(0,0,0,0);
    hb->addStretch();
    closeBtn_ = new QPushButton(tr("Cerrar"), btnRow);
    applyBtn_ = new QPushButton(tr("Aplicar"), btnRow);
    hb->addWidget(closeBtn_);
    hb->addWidget(applyBtn_);
    root->addWidget(btnRow);

    // Visual priority: Apply is the primary (default) only when enabled.
    applyBtn_->setEnabled(false); // disabled until something changes
    applyBtn_->setAutoDefault(true);
    applyBtn_->setDefault(false);
    closeBtn_->setAutoDefault(false);
    closeBtn_->setDefault(false);
    connect(applyBtn_, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(closeBtn_, &QPushButton::clicked, this, &SettingsDialog::reject);

    // Enable Apply when any control differs from persisted values
    connect(langCombo_, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateApplyFromControls);
    connect(clickMode_, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateApplyFromControls);
    connect(showHidden_, &QCheckBox::toggled, this, &SettingsDialog::updateApplyFromControls);
    connect(showConnOnStart_, &QCheckBox::toggled, this, &SettingsDialog::updateApplyFromControls);
    updateApplyFromControls();
}

void SettingsDialog::onApply() {
    const QString chosenLang  = langCombo_->currentData().toString();

    QSettings s("OpenSCP", "OpenSCP");
    const QString prevLang = s.value("UI/language", "es").toString();
    s.setValue("UI/language", chosenLang);
    s.setValue("UI/showHidden", showHidden_ && showHidden_->isChecked());
    s.setValue("UI/showConnOnStart", showConnOnStart_ && showConnOnStart_->isChecked());
    const bool singleClick = (clickMode_ && clickMode_->currentData().toInt() == 1);
    s.setValue("UI/singleClick", singleClick);
    s.sync();

    // Only notify if language actually changed
    if (prevLang != chosenLang) {
        QMessageBox::information(this, tr("Idioma"), tr("El cambio de idioma se aplicará al reiniciar."));
    }
    if (applyBtn_) { applyBtn_->setEnabled(false); applyBtn_->setDefault(false); }
}

void SettingsDialog::updateApplyFromControls() {
    QSettings s("OpenSCP", "OpenSCP");
    const QString prevLang = s.value("UI/language", "es").toString();
    const bool showHidden = s.value("UI/showHidden", false).toBool();
    const bool showConnOnStart = s.value("UI/showConnOnStart", true).toBool();
    const bool singleClick = s.value("UI/singleClick", false).toBool();

    const QString curLang = langCombo_ ? langCombo_->currentData().toString() : prevLang;
    const bool curShowHidden = showHidden_ && showHidden_->isChecked();
    const bool curShowConn = showConnOnStart_ && showConnOnStart_->isChecked();
    const bool curSingleClick = (clickMode_ && clickMode_->currentData().toInt() == 1);

    const bool modified = (curLang != prevLang) ||
                          (curShowHidden != showHidden) ||
                          (curShowConn != showConnOnStart) ||
                          (curSingleClick != singleClick);
    if (applyBtn_) {
        applyBtn_->setEnabled(modified);
        applyBtn_->setDefault(modified);
    }
}
