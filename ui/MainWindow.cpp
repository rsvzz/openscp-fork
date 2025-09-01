#include "MainWindow.hpp"
#include <QApplication>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QFileDialog>
#include <QStatusBar>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QKeySequence>

static constexpr int NAME_COL = 0;

#include <QDirIterator>

// Copia recursivamente un archivo o carpeta.
// Devuelve true si todo salió bien; en caso contrario, false y escribe el error.
static bool copyEntryRecursively(const QString& srcPath, const QString& dstPath, QString& error) {
    QFileInfo srcInfo(srcPath);

    if (srcInfo.isFile()) {
        // Asegura carpeta destino
        QDir().mkpath(QFileInfo(dstPath).dir().absolutePath());
        if (QFile::exists(dstPath)) QFile::remove(dstPath);
        if (!QFile::copy(srcPath, dstPath)) {
            error = QString("No se pudo copiar archivo: %1").arg(srcPath);
            return false;
        }
        return true;
    }

    if (srcInfo.isDir()) {
        // Crea carpeta destino
        if (!QDir().mkpath(dstPath)) {
            error = QString("No se pudo crear carpeta destino: %1").arg(dstPath);
            return false;
        }
        // Itera recursivo
        QDirIterator it(srcPath, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            const QString rel = QDir(srcPath).relativeFilePath(fi.absoluteFilePath());
            const QString target = QDir(dstPath).filePath(rel);

            if (fi.isDir()) {
                if (!QDir().mkpath(target)) {
                    error = QString("No se pudo crear subcarpeta destino: %1").arg(target);
                    return false;
                }
            } else {
                // Asegura carpeta contenedora
                QDir().mkpath(QFileInfo(target).dir().absolutePath());
                if (QFile::exists(target)) QFile::remove(target);
                if (!QFile::copy(fi.absoluteFilePath(), target)) {
                    error = QString("Falló al copiar: %1").arg(fi.absoluteFilePath());
                    return false;
                }
            }
        }
        return true;
    }

    error = "Entrada de origen ni archivo ni carpeta.";
    return false;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Modelos
    leftModel_  = new QFileSystemModel(this);
    rightModel_ = new QFileSystemModel(this);

    leftModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    rightModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);

    // Rutas iniciales: HOME
    const QString home = QDir::homePath();
    leftModel_->setRootPath(home);
    rightModel_->setRootPath(home);

    // Vistas
    leftView_  = new QTreeView(this);
    rightView_ = new QTreeView(this);

    leftView_->setModel(leftModel_);
    rightView_->setModel(rightModel_);
    leftView_->setRootIndex(leftModel_->index(home));
    rightView_->setRootIndex(rightModel_->index(home));

    // Ajustes visuales básicos
    auto tuneView = [](QTreeView* v){
        v->setSelectionMode(QAbstractItemView::ExtendedSelection);
        v->setSortingEnabled(true);
        v->sortByColumn(NAME_COL, Qt::AscendingOrder);
        v->header()->setStretchLastSection(true);
        v->setColumnWidth(NAME_COL, 280);
    };
    tuneView(leftView_);
    tuneView(rightView_);

    // Entradas de ruta (arriba)
    leftPath_  = new QLineEdit(home, this);
    rightPath_ = new QLineEdit(home, this);
    connect(leftPath_,  &QLineEdit::returnPressed, this, &MainWindow::leftPathEntered);
    connect(rightPath_, &QLineEdit::returnPressed, this, &MainWindow::rightPathEntered);

    // Splitter central con dos paneles
    auto* splitter = new QSplitter(this);
    auto* leftPane  = new QWidget(this);
    auto* rightPane = new QWidget(this);

    auto* leftLayout  = new QVBoxLayout(leftPane);
    auto* rightLayout = new QVBoxLayout(rightPane);
    leftLayout->setContentsMargins(0,0,0,0);
    rightLayout->setContentsMargins(0,0,0,0);

    leftLayout->addWidget(leftPath_);
    leftLayout->addWidget(leftView_);
    rightLayout->addWidget(rightPath_);
    rightLayout->addWidget(rightView_);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    setCentralWidget(splitter);

    // Barra de herramientas //
    auto* tb = addToolBar("Main");
    actChooseLeft_  = tb->addAction("Carpeta izquierda",  this, &MainWindow::chooseLeftDir);
    actChooseRight_ = tb->addAction("Carpeta derecha",    this, &MainWindow::chooseRightDir);
    tb->addSeparator();
    // accion de copiar o F5
    actCopyF5_ = tb->addAction("Copiar (F5)", this, &MainWindow::copyLeftToRight);
    actCopyF5_->setShortcut(QKeySequence(Qt::Key_F5));
    // accion de mover o F6
    actMoveF6_ = tb->addAction("Mover (F6)", this, &MainWindow::moveLeftToRight);
    actMoveF6_->setShortcut(QKeySequence(Qt::Key_F6));
    // accion de eliminar
    actDelete_ = tb->addAction("Borrar (Supr)", this, &MainWindow::deleteFromLeft);
    actDelete_->setShortcut(QKeySequence(Qt::Key_Delete));



    statusBar()->showMessage("Listo");

    setWindowTitle("OpenSCP (demo local) — dos paneles");
    resize(1100, 650);
}

void MainWindow::chooseLeftDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Selecciona carpeta izquierda", leftPath_->text());
    if (!dir.isEmpty()) setLeftRoot(dir);
}

void MainWindow::chooseRightDir() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Selecciona carpeta derecha", rightPath_->text());
    if (!dir.isEmpty()) setRightRoot(dir);
}

void MainWindow::leftPathEntered()  { setLeftRoot(leftPath_->text()); }
void MainWindow::rightPathEntered() { setRightRoot(rightPath_->text()); }

void MainWindow::setLeftRoot(const QString& path) {
    if (QDir(path).exists()) {
        leftPath_->setText(path);
        leftView_->setRootIndex(leftModel_->index(path));
        statusBar()->showMessage("Izquierda: " + path, 3000);
    } else {
        QMessageBox::warning(this, "Ruta inválida", "La carpeta no existe.");
    }
}

void MainWindow::setRightRoot(const QString& path) {
    if (QDir(path).exists()) {
        rightPath_->setText(path);
        rightView_->setRootIndex(rightModel_->index(path));
        statusBar()->showMessage("Derecha: " + path, 3000);
    } else {
        QMessageBox::warning(this, "Ruta inválida", "La carpeta no existe.");
    }
}

void MainWindow::copyLeftToRight() {
    const QString dstDirPath = rightPath_->text();
    QDir dstDir(dstDirPath);
    if (!dstDir.exists()) {
        QMessageBox::warning(this, "Destino inválido", "La carpeta de destino no existe.");
        return;
    }

    const auto rows = leftView_->selectionModel()->selectedRows(NAME_COL);
    if (rows.isEmpty()) {
        QMessageBox::information(this, "Copiar", "No hay entradas seleccionadas en el panel izquierdo.");
        return;
    }

    int ok = 0, fail = 0;
    QString lastError;

    for (const QModelIndex& idx : rows) {
        const QFileInfo fi = leftModel_->fileInfo(idx);
        const QString target = dstDir.filePath(fi.fileName());

        QString err;
        if (copyEntryRecursively(fi.absoluteFilePath(), target, err)) {
            ok++;
        } else {
            fail++;
            lastError = err;
        }
    }

    QString msg = QString("Copiados OK: %1  |  Fallidos: %2").arg(ok).arg(fail);
    if (fail > 0 && !lastError.isEmpty()) msg += "\nÚltimo error: " + lastError;
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::moveLeftToRight() {
    const QString dstDirPath = rightPath_->text();
    QDir dstDir(dstDirPath);
    if (!dstDir.exists()) {
        QMessageBox::warning(this, "Destino inválido", "La carpeta de destino no existe.");
        return;
    }

    const auto rows = leftView_->selectionModel()->selectedRows(NAME_COL);
    if (rows.isEmpty()) {
        QMessageBox::information(this, "Mover", "No hay entradas seleccionadas en el panel izquierdo.");
        return;
    }

    if (QMessageBox::question(this, "Confirmar mover",
        "Esto copiará y luego eliminará el origen.\n¿Deseas continuar?")
        != QMessageBox::Yes) {
        return;
    }

    int ok = 0, fail = 0;
    QString lastError;

    for (const QModelIndex& idx : rows) {
        const QFileInfo fi = leftModel_->fileInfo(idx);
        const QString target = dstDir.filePath(fi.fileName());

        QString err;
        if (copyEntryRecursively(fi.absoluteFilePath(), target, err)) {
            // Elimina origen (archivo o carpeta) tras copiar
            bool removed = fi.isDir() ? QDir(fi.absoluteFilePath()).removeRecursively()
                                      : QFile::remove(fi.absoluteFilePath());
            if (removed) ok++;
            else { fail++; lastError = "No se pudo borrar origen: " + fi.absoluteFilePath(); }
        } else {
            fail++; lastError = err;
        }
    }

    QString msg = QString("Movidos OK: %1  |  Fallidos: %2").arg(ok).arg(fail);
    if (fail > 0 && !lastError.isEmpty()) msg += "\nÚltimo error: " + lastError;
    statusBar()->showMessage(msg, 5000);
}

void MainWindow::deleteFromLeft() {
    const auto rows = leftView_->selectionModel()->selectedRows(NAME_COL);
    if (rows.isEmpty()) {
        QMessageBox::information(this, "Borrar", "No hay entradas seleccionadas en el panel izquierdo.");
        return;
    }

    if (QMessageBox::warning(this, "Confirmar borrado",
        "Esto eliminará permanentemente los elementos seleccionados en el panel izquierdo.\n¿Deseas continuar?",
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    int ok = 0, fail = 0;
    for (const QModelIndex& idx : rows) {
        const QFileInfo fi = leftModel_->fileInfo(idx);
        bool removed = fi.isDir() ? QDir(fi.absoluteFilePath()).removeRecursively()
                                  : QFile::remove(fi.absoluteFilePath());
        if (removed) ok++; else fail++;
    }

    statusBar()->showMessage(QString("Borrados: %1  |  Fallidos: %2").arg(ok).arg(fail), 5000);
}
