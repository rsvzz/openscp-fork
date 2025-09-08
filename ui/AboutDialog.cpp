// Implementation of the "About" dialog for OpenSCP.
#include "AboutDialog.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>

static constexpr const char* kVersion = "v0.5.0"; // show current version

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

    // Placeholder to list libraries (to be completed by the author)
    auto* libsPlaceholder = new QLabel(tr("(Espacio reservado para créditos/licencias de dependencias.)"), this);
    libsPlaceholder->setWordWrap(true);
    lay->addWidget(libsPlaceholder);

    // Report an issue link at the bottom (opens Issues page)
    {
        const QString linkText = tr("Informar de un error");
        auto* report = new QLabel(QString("<a href=\"https://github.com/luiscuellar31/openscp/issues\">%1</a>").arg(linkText), this);
        report->setTextFormat(Qt::RichText);
        report->setOpenExternalLinks(true);
        report->setWordWrap(true);
        lay->addWidget(report);
    }

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(btns, &QDialogButtonBox::rejected, this, &AboutDialog::reject);
    connect(btns, &QDialogButtonBox::accepted, this, &AboutDialog::accept);
    lay->addWidget(btns);
}
