// Implementación de Acerca de para OpenSCP.
#include "AboutDialog.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>

static constexpr const char* kVersion = "v0.5.0"; // mostrar versión actual

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Acerca de OpenSCP"));

    auto* lay = new QVBoxLayout(this);

    auto* title = new QLabel(QString("<b>OpenSCP %1</b>").arg(kVersion), this);
    title->setTextFormat(Qt::RichText);
    lay->addWidget(title);

    auto* author = new QLabel(tr("Autor: <a href=\"https://github.com/luiscuellar31\">luiscuellar31</a>"), this);
    author->setTextFormat(Qt::RichText);
    author->setOpenExternalLinks(true);
    lay->addWidget(author);

    auto* libsTitle = new QLabel(tr("Librerías utilizadas:"), this);
    lay->addWidget(libsTitle);

    // Espacio reservado para listar librerías (el autor lo completará)
    auto* libsPlaceholder = new QLabel(tr("(Espacio reservado para créditos/licencias de dependencias.)"), this);
    libsPlaceholder->setWordWrap(true);
    lay->addWidget(libsPlaceholder);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btns, &QDialogButtonBox::rejected, this, &AboutDialog::reject);
    connect(btns, &QDialogButtonBox::accepted, this, &AboutDialog::accept);
    lay->addWidget(btns);
}

