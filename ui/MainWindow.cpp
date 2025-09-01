// OpenSCP main window: two‑panel file manager with SFTP support.
// Implements local/local and local/remote operations (copy/move, download/upload,
// create/rename/delete on remote), with recursive transfers, collision handling
// and cancellable progress dialogs.
#include "MainWindow.hpp"
#include "openscp/SftpClient.hpp"
#include "openscp/Libssh2SftpClient.hpp"  //nuevo 
#include "ConnectionDialog.hpp"
#include "RemoteModel.hpp"
#include <QApplication>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QSize>             // por QSize(16,16)
#include <QFileDialog>
#include <QStatusBar>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QDirIterator>
#include <QInputDialog>
#include <QAbstractButton>

#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QProgressDialog>
#include <QLocale>
#include <QPushButton>
#include <QListView>


static constexpr int NAME_COL = 0;

MainWindow::~MainWindow() = default; // <- define el destructor aquí

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

static QString tempDownloadPathFor(const QString& remoteName) {
  QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (base.isEmpty()) base = QDir::homePath() + "/Downloads";
  QDir().mkpath(base);
  return QDir(base).filePath(remoteName);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // Modelos
    leftModel_       = new QFileSystemModel(this);
    rightLocalModel_ = new QFileSystemModel(this);

    leftModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    rightLocalModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);

    // Rutas iniciales: HOME
    const QString home = QDir::homePath();
    leftModel_->setRootPath(home);
    rightLocalModel_->setRootPath(home);

    // Vistas
    leftView_  = new QTreeView(this);
    rightView_ = new QTreeView(this);

    leftView_->setModel(leftModel_);
    rightView_->setModel(rightLocalModel_);
    leftView_->setRootIndex(leftModel_->index(home));
    rightView_->setRootIndex(rightLocalModel_->index(home));

    // Ajustes visuales básicos
    auto tuneView = [](QTreeView* v){
        v->setSelectionMode(QAbstractItemView::ExtendedSelection);
        v->setSortingEnabled(true);
        v->sortByColumn(0, Qt::AscendingOrder);
        v->header()->setStretchLastSection(true);
        v->setColumnWidth(0, 280);
    };
    tuneView(leftView_);
    tuneView(rightView_);

    // Entradas de ruta (arriba)
    leftPath_  = new QLineEdit(home, this);
    rightPath_ = new QLineEdit(home, this);
    connect(leftPath_,  &QLineEdit::returnPressed, this, &MainWindow::leftPathEntered);
    connect(rightPath_, &QLineEdit::returnPressed, this, &MainWindow::rightPathEntered);

    // --- Splitter central con dos paneles ---
    auto* splitter = new QSplitter(this);
    auto* leftPane  = new QWidget(this);
    auto* rightPane = new QWidget(this);

    auto* leftLayout  = new QVBoxLayout(leftPane);
    auto* rightLayout = new QVBoxLayout(rightPane);
    leftLayout->setContentsMargins(0,0,0,0);
    rightLayout->setContentsMargins(0,0,0,0);

    // --- Sub-toolbar IZQUIERDA (de panel) ---
    leftPaneBar_ = new QToolBar("LeftBar", leftPane);
    leftPaneBar_->setIconSize(QSize(16,16));
    actUpLeft_ = leftPaneBar_->addAction("Arriba", this, &MainWindow::goUpLeft);
    leftLayout->addWidget(leftPaneBar_);

    // Widgets del panel izquierdo: toolbar -> path -> view
    leftLayout->addWidget(leftPath_);
    leftLayout->addWidget(leftView_);

    // --- Sub-toolbar DERECHA (de panel) ---
    rightPaneBar_ = new QToolBar("RightBar", rightPane);
    rightPaneBar_->setIconSize(QSize(16,16));
    actUpRight_ = rightPaneBar_->addAction("Arriba", this, &MainWindow::goUpRight);

    // (recomendado) mover "Descargar (F7)" aquí:
    actDownloadF7_ = rightPaneBar_->addAction("Descargar (F7)", this, &MainWindow::downloadRightToLeft);
    actDownloadF7_->setShortcut(QKeySequence(Qt::Key_F7));
    this->addAction(actDownloadF7_);     // atajo global
    actDownloadF7_->setEnabled(false);   // empieza deshabilitado en local

    rightPaneBar_->addSeparator();
    actUploadRight_ = rightPaneBar_->addAction("Subir…", this, &MainWindow::uploadViaDialog);
    actNewDirRight_  = rightPaneBar_->addAction("Nueva carpeta", this, &MainWindow::newDirRight);
    actRenameRight_  = rightPaneBar_->addAction("Renombrar",     this, &MainWindow::renameRightSelected);
    actDeleteRight_  = rightPaneBar_->addAction("Borrar",        this, &MainWindow::deleteRightSelected);
    // Deshabilitar al inicio (no hay sesión remota)
    if (actDownloadF7_)  actDownloadF7_->setEnabled(false);
    actUploadRight_->setEnabled(false);
    actNewDirRight_->setEnabled(false);
    actRenameRight_->setEnabled(false);
    actDeleteRight_->setEnabled(false);

    // Widgets del panel derecho: toolbar -> path -> view
    rightLayout->addWidget(rightPaneBar_);
    rightLayout->addWidget(rightPath_);
    rightLayout->addWidget(rightView_);

    // Montar paneles en el splitter
    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    setCentralWidget(splitter);

    // --- Toolbar principal (superior) ---
    auto* tb = addToolBar("Main");
    actChooseLeft_   = tb->addAction("Carpeta izquierda",  this, &MainWindow::chooseLeftDir);
    tb->addSeparator();
    actChooseRight_  = tb->addAction("Carpeta derecha",    this, &MainWindow::chooseRightDir);
    tb->addSeparator();
    actCopyF5_ = tb->addAction("Copiar (F5)", this, &MainWindow::copyLeftToRight);
    actCopyF5_->setShortcut(QKeySequence(Qt::Key_F5));
    tb->addSeparator();
    actMoveF6_ = tb->addAction("Mover (F6)", this, &MainWindow::moveLeftToRight);
    actMoveF6_->setShortcut(QKeySequence(Qt::Key_F6));
    tb->addSeparator();
    actDelete_ = tb->addAction("Borrar (Supr)", this, &MainWindow::deleteFromLeft);
    actDelete_->setShortcut(QKeySequence(Qt::Key_Delete));
    tb->addSeparator();
    actConnect_    = tb->addAction("Conectar (SFTP)", this, &MainWindow::connectSftp);
    tb->addSeparator();
    actDisconnect_ = tb->addAction("Desconectar",     this, &MainWindow::disconnectSftp);
    actDisconnect_->setEnabled(false);

    // Atajos globales para acciones que no están en la toolbar principal
    this->addAction(actMoveF6_);
    this->addAction(actDelete_);

    // Doble click en panel derecho para navegar remoto / abrir archivos
    connect(rightView_, &QTreeView::activated, this, &MainWindow::rightItemActivated);

    downloadDir_ = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloadDir_.isEmpty())
    downloadDir_ = QDir::homePath() + "/Downloads";
    QDir().mkpath(downloadDir_);

    statusBar()->showMessage("Listo");
    setWindowTitle("OpenSCP (demo) — local/local (clic en Conectar para remoto)");
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

void MainWindow::rightPathEntered() {
    if (rightIsRemote_) setRightRemoteRoot(rightPath_->text());
    else setRightRoot(rightPath_->text());
}


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
        rightView_->setRootIndex(rightLocalModel_->index(path)); // <-- aquí
        statusBar()->showMessage("Derecha: " + path, 3000);
    } else {
        QMessageBox::warning(this, "Ruta inválida", "La carpeta no existe.");
    }
}

static QString joinRemotePath(const QString& base, const QString& name) {
  if (base == "/") return "/" + name;
  return base.endsWith('/') ? base + name : base + "/" + name;
}

void MainWindow::copyLeftToRight() {
    if (rightIsRemote_) {
        // ---- Rama REMOTA: subir archivos (PUT) al directorio remoto actual ----
        if (!sftp_ || !rightRemoteModel_) {
            QMessageBox::warning(this, "SFTP", "No hay sesión SFTP activa.");
            return;
        }

        // Selección en panel izquierdo (origen local)
        auto sel = leftView_->selectionModel();
        if (!sel) {
            QMessageBox::warning(this, "Copiar", "No hay selección disponible.");
            return;
        }
        const auto rows = sel->selectedRows(NAME_COL);
        if (rows.isEmpty()) {
            QMessageBox::information(this, "Copiar", "No hay entradas seleccionadas en el panel izquierdo.");
            return;
        }

        // Políticas de colisión
        enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
        OverwritePolicy policy = OverwritePolicy::Ask;

        int ok = 0, fail = 0, skipped = 0;
        QString lastError;
        const QString remoteBase = rightRemoteModel_->rootPath();

        for (const QModelIndex& idx : rows) {
            const QFileInfo fi = leftModel_->fileInfo(idx);

            if (fi.isDir()) {
                // Subir carpeta recursivamente
                // Asegura carpeta destino base
                const QString remoteDirBase = joinRemotePath(remoteBase, fi.fileName());

                // Reutiliza diálogo de progreso por archivo al subir archivos individuales
                // y permite cancelar a mitad de la operación completa.
                bool canceled = false;

                // Crea el directorio raíz si no existe
                {
                    bool isDir = false; std::string se;
                    bool ex = sftp_->exists(remoteDirBase.toStdString(), isDir, se);
                    if (!se.empty()) { ++fail; lastError = QString::fromStdString(se); continue; }
                    if (!ex) {
                        std::string me;
                        if (!sftp_->mkdir(remoteDirBase.toStdString(), me, 0755)) {
                            ++fail; lastError = QString::fromStdString(me); continue;
                        }
                    }
                }

                // Itera recursivo local
                QDirIterator it(fi.absoluteFilePath(), QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    const QFileInfo sfi = it.fileInfo();
                    const QString rel = QDir(fi.absoluteFilePath()).relativeFilePath(sfi.absoluteFilePath());
                    const QString rTarget = joinRemotePath(remoteDirBase, rel);

                    if (sfi.isDir()) {
                        // Asegura directorio remoto
                        bool isDir = false; std::string se;
                        bool ex = sftp_->exists(rTarget.toStdString(), isDir, se);
                        if (!se.empty()) { ++fail; lastError = QString::fromStdString(se); canceled = true; break; }
                        if (!ex) {
                            std::string me;
                            if (!sftp_->mkdir(rTarget.toStdString(), me, 0755)) { ++fail; lastError = QString::fromStdString(me); canceled = true; break; }
                        }
                        continue;
                    }

                    // Archivo: colisión
                    bool isDir = false; std::string sErr;
                    const bool exists = sftp_->exists(rTarget.toStdString(), isDir, sErr);
                    if (!sErr.empty()) { ++fail; lastError = QString::fromStdString(sErr); canceled = true; break; }
                    QString targetPath = rTarget;
                    if (exists) {
                        // Consulta comparación tamaño/fecha para decidir
                        openscp::FileInfo rinfo{}; std::string stErr;
                        bool haveStat = sftp_->stat(rTarget.toStdString(), rinfo, stErr);
                        const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
                          .arg(sfi.size())
                          .arg(QLocale().toString(sfi.lastModified(), QLocale::ShortFormat))
                          .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
                          .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));

                        QMessageBox msg(this);
                        msg.setWindowTitle("Conflicto remoto");
                        msg.setText(QString("«%1» ya existe.\n%2").arg(rel, cmp));
                        QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
                        QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
                        QAbstractButton* btRename       = msg.addButton("Renombrar", QMessageBox::ActionRole);
                        QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
                        QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
                        msg.exec();

                        if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
                        else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;

                        if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
                        if (msg.clickedButton() == btRename) {
                            bool okName=false;
                            const QString baseName = sfi.fileName();
                            QString newName = QInputDialog::getText(this, "Renombrar",
                                 "Nuevo nombre:", QLineEdit::Normal, baseName, &okName);
                            if (!okName || newName.isEmpty()) { ++skipped; continue; }
                            // Recalcula target con el nuevo nombre
                            const QString parentRel = QFileInfo(rel).path();
                            const QString parentRemote = parentRel.isEmpty() ? remoteDirBase : joinRemotePath(remoteDirBase, parentRel);
                            targetPath = joinRemotePath(parentRemote, newName);
                        }
                        // Si overwrite o overwrite all: seguimos
                    }

                    // Progreso por archivo
                    QProgressDialog dlg(QString("Subiendo %1").arg(rel), "Cancelar", 0, 100, this);
                    dlg.setWindowModality(Qt::ApplicationModal);
                    dlg.setMinimumDuration(0);

                    std::string perr;
                    bool pres = sftp_->put(sfi.absoluteFilePath().toStdString(), targetPath.toStdString(), perr,
                        [&](std::size_t done, std::size_t total){
                            int pct = (total > 0) ? int((done * 100) / total) : 0;
                            dlg.setValue(pct);
                            qApp->processEvents();
                        },
                        [&](){ return dlg.wasCanceled(); });
                    dlg.setValue(100);

                    if (!pres) { ++fail; lastError = QString::fromStdString(perr); canceled = dlg.wasCanceled(); break; }
                    else { ++ok; }
                }

                if (canceled) break; // salir del for de selección
                continue; // pasa a siguiente selección
            }

            // Construye destino remoto
            QString remoteTarget = joinRemotePath(remoteBase, fi.fileName());

            // ¿Existe en remoto?
            bool isDir = false;
            std::string sErr;
            const bool exists = sftp_->exists(remoteTarget.toStdString(), isDir, sErr);
            if (!sErr.empty()) { ++fail; lastError = QString::fromStdString(sErr); continue; }
            if (exists) {
                if (policy == OverwritePolicy::Ask) {
                    // Prepara comparación
                    openscp::FileInfo rinfo{}; std::string stErr;
                    bool haveStat = sftp_->stat(remoteTarget.toStdString(), rinfo, stErr);
                    const QFileInfo lfi = fi;
                    const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
                      .arg(lfi.size())
                      .arg(QLocale().toString(lfi.lastModified(), QLocale::ShortFormat))
                      .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
                      .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));

                    QMessageBox msg(this);
                    msg.setWindowTitle("Conflicto remoto");
                    msg.setText(QString("«%1» ya existe.\n%2").arg(fi.fileName(), cmp));
                    QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
                    QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
                    QAbstractButton* btRename       = msg.addButton("Renombrar", QMessageBox::ActionRole);
                    QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
                    QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
                    msg.exec();

                    if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
                    else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;

                    if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
                    if (msg.clickedButton() == btRename) {
                        bool okName=false;
                        QString newName = QInputDialog::getText(this, "Renombrar",
                             "Nuevo nombre:", QLineEdit::Normal, fi.fileName(), &okName);
                        if (!okName || newName.isEmpty()) { ++skipped; continue; }
                        // Recalcula destino
                        const QString newTarget = joinRemotePath(remoteBase, newName);
                        remoteTarget = newTarget;
                    }
                } else if (policy == OverwritePolicy::SkipAll) {
                    ++skipped; continue;
                }
                // OverwriteAll o Yes → seguimos; el put trunca
            }

            // Progreso visual durante la subida
            QProgressDialog dlg("Subiendo " + fi.fileName(), "Cancelar", 0, 100, this);
            dlg.setWindowModality(Qt::ApplicationModal);
            dlg.setMinimumDuration(0);

            std::string err;
            bool res = sftp_->put(
                fi.absoluteFilePath().toStdString(),   // local
                remoteTarget.toStdString(),            // remoto
                err,
                [&](std::size_t done, std::size_t total) {
                    int pct = (total > 0) ? int((done * 100) / total) : 0;
                    dlg.setValue(pct);
                    qApp->processEvents();
                },
                [&](){ return dlg.wasCanceled(); }
            );
            dlg.setValue(100);

            if (res) ++ok; else { ++fail; lastError = QString::fromStdString(err); }
        }

        // Refresca el listado remoto (para que aparezcan los archivos recién subidos)
        QString dummy;
        rightRemoteModel_->setRootPath(remoteBase, &dummy);

        // Feedback
        QString msg = QString("Subidos: %1  |  Fallidos: %2  |  Saltados: %3").arg(ok).arg(fail).arg(skipped);
        if (fail > 0 && !lastError.isEmpty()) msg += "\nÚltimo error: " + lastError;
        statusBar()->showMessage(msg, 6000);
        return;
    }

    // ---- Rama LOCAL→LOCAL: tu lógica existente tal cual ----
    const QString dstDirPath = rightPath_->text();
    QDir dstDir(dstDirPath);
    if (!dstDir.exists()) {
        QMessageBox::warning(this, "Destino inválido", "La carpeta de destino no existe.");
        return;
    }

    auto sel = leftView_->selectionModel();
    if (!sel) {
        QMessageBox::warning(this, "Copiar", "No hay selección disponible.");
        return;
    }
    const auto rows = sel->selectedRows(NAME_COL);
    if (rows.isEmpty()) {
        QMessageBox::information(this, "Copiar", "No hay entradas seleccionadas en el panel izquierdo.");
        return;
    }

    enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
    OverwritePolicy policy = OverwritePolicy::Ask;

    int ok = 0, fail = 0, skipped = 0;
    QString lastError;

    for (const QModelIndex& idx : rows) {
        const QFileInfo fi = leftModel_->fileInfo(idx);
        const QString target = dstDir.filePath(fi.fileName());

        if (QFileInfo::exists(target)) {
            if (policy == OverwritePolicy::Ask) {
                auto ret = QMessageBox::question(this, "Conflicto",
                    QString("«%1» ya existe en destino.\n¿Sobrescribir?").arg(fi.fileName()),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::YesToAll | QMessageBox::NoToAll);
                if (ret == QMessageBox::YesToAll) policy = OverwritePolicy::OverwriteAll;
                else if (ret == QMessageBox::NoToAll) policy = OverwritePolicy::SkipAll;

                if (ret == QMessageBox::No || policy == OverwritePolicy::SkipAll) {
                    ++skipped;
                    continue;
                }
            }
            QFileInfo tfi(target);
            if (tfi.isDir()) QDir(target).removeRecursively();
            else QFile::remove(target);
        }

        QString err;
        if (copyEntryRecursively(fi.absoluteFilePath(), target, err)) {
            ++ok;
        } else {
            ++fail;
            lastError = err;
        }
    }

    QString msg = QString("Copiados: %1  |  Fallidos: %2  |  Saltados: %3")
                    .arg(ok).arg(fail).arg(skipped);
    if (fail > 0 && !lastError.isEmpty()) msg += "\nÚltimo error: " + lastError;
    statusBar()->showMessage(msg, 6000);
}

void MainWindow::moveLeftToRight() {
    // Si el panel derecho es remoto: mover = subir (PUT) y borrar origen local
    if (rightIsRemote_) {
        if (!sftp_ || !rightRemoteModel_) {
            QMessageBox::warning(this, "SFTP", "No hay sesión SFTP activa.");
            return;
        }

        const auto rows = leftView_->selectionModel()->selectedRows(NAME_COL);
        if (rows.isEmpty()) {
            QMessageBox::information(this, "Mover", "No hay entradas seleccionadas en el panel izquierdo.");
            return;
        }

        if (QMessageBox::question(this, "Confirmar mover",
            "Esto subirá al servidor y eliminará el origen local.\n¿Deseas continuar?") != QMessageBox::Yes) {
            return;
        }

        enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
        OverwritePolicy policy = OverwritePolicy::Ask;

        int ok = 0, fail = 0, skipped = 0;
        QString lastError;
        const QString remoteBase = rightRemoteModel_->rootPath();

        for (const QModelIndex& idx : rows) {
            const QFileInfo fi = leftModel_->fileInfo(idx);
            bool itemAllOk = true; // éxito global para este ítem (archivo o carpeta)

            if (fi.isDir()) {
                const QString remoteDirBase = joinRemotePath(remoteBase, fi.fileName());
                // Crea directorio raíz si no existe
                {
                    bool isDir = false; std::string se;
                    bool ex = sftp_->exists(remoteDirBase.toStdString(), isDir, se);
                    if (!se.empty()) { itemAllOk = false; lastError = QString::fromStdString(se); }
                    else if (!ex) {
                        std::string me;
                        if (!sftp_->mkdir(remoteDirBase.toStdString(), me, 0755)) { itemAllOk = false; lastError = QString::fromStdString(me); }
                    }
                }
                if (!itemAllOk) { ++fail; continue; }

                bool canceled = false;
                QDirIterator it(fi.absoluteFilePath(), QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    const QFileInfo sfi = it.fileInfo();
                    const QString rel = QDir(fi.absoluteFilePath()).relativeFilePath(sfi.absoluteFilePath());
                    QString rTarget = joinRemotePath(remoteDirBase, rel);

                    if (sfi.isDir()) {
                        // asegura dir remoto
                        bool isDir = false; std::string se;
                        bool ex = sftp_->exists(rTarget.toStdString(), isDir, se);
                        if (!se.empty()) { itemAllOk = false; lastError = QString::fromStdString(se); canceled = true; break; }
                        if (!ex) {
                            std::string me;
                            if (!sftp_->mkdir(rTarget.toStdString(), me, 0755)) { itemAllOk = false; lastError = QString::fromStdString(me); canceled = true; break; }
                        }
                        continue;
                    }

                    // archivo: colisión
                    bool isDir = false; std::string sErr;
                    const bool exists = sftp_->exists(rTarget.toStdString(), isDir, sErr);
                    if (!sErr.empty()) { itemAllOk = false; lastError = QString::fromStdString(sErr); canceled = true; break; }
                    if (exists) {
                        if (policy == OverwritePolicy::Ask) {
                            openscp::FileInfo rinfo{}; std::string stErr; bool haveStat = sftp_->stat(rTarget.toStdString(), rinfo, stErr);
                            const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
                              .arg(sfi.size())
                              .arg(QLocale().toString(sfi.lastModified(), QLocale::ShortFormat))
                              .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
                              .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));
                            QMessageBox msg(this);
                            msg.setWindowTitle("Conflicto remoto");
                            msg.setText(QString("«%1» ya existe.\n%2").arg(rel, cmp));
                            QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
                            QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
                            QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
                            QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
                            msg.exec();
                            if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
                            else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;
                            if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
                        } else if (policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
                    }

                    QProgressDialog dlg(QString("Subiendo %1").arg(rel), "Cancelar", 0, 100, this);
                    dlg.setWindowModality(Qt::ApplicationModal);
                    dlg.setMinimumDuration(0);
                    std::string perr;
                    bool pres = sftp_->put(sfi.absoluteFilePath().toStdString(), rTarget.toStdString(), perr,
                        [&](std::size_t done, std::size_t total){
                            int pct = (total > 0) ? int((done * 100) / total) : 0;
                            dlg.setValue(pct); qApp->processEvents();
                        },
                        [&](){ return dlg.wasCanceled(); });
                    dlg.setValue(100);
                    if (!pres) { itemAllOk = false; lastError = QString::fromStdString(perr); canceled = dlg.wasCanceled(); break; }
                }

                if (itemAllOk) {
                    // eliminar origen local
                    if (QDir(fi.absoluteFilePath()).removeRecursively()) ++ok; else { ++fail; lastError = "No se pudo borrar origen: " + fi.absoluteFilePath(); }
                } else {
                    ++fail;
                }
                if (canceled) break;
                continue;
            }

            // archivo individual
            QString remoteTarget = joinRemotePath(remoteBase, fi.fileName());
            bool isDir = false; std::string sErr;
            const bool exists = sftp_->exists(remoteTarget.toStdString(), isDir, sErr);
            if (!sErr.empty()) { ++fail; lastError = QString::fromStdString(sErr); continue; }
            if (exists) {
                if (policy == OverwritePolicy::Ask) {
                    openscp::FileInfo rinfo{}; std::string stErr; bool haveStat = sftp_->stat(remoteTarget.toStdString(), rinfo, stErr);
                    const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
                      .arg(fi.size())
                      .arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat))
                      .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
                      .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));
                    QMessageBox msg(this);
                    msg.setWindowTitle("Conflicto remoto");
                    msg.setText(QString("«%1» ya existe.\n%2").arg(fi.fileName(), cmp));
                    QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
                    QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
                    QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
                    QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
                    msg.exec();
                    if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
                    else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;
                    if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
                } else if (policy == OverwritePolicy::SkipAll) { ++skipped; continue; }
            }

            QProgressDialog dlg(QString("Subiendo %1").arg(fi.fileName()), "Cancelar", 0, 100, this);
            dlg.setWindowModality(Qt::ApplicationModal); dlg.setMinimumDuration(0);
            std::string err;
            bool res = sftp_->put(
                fi.absoluteFilePath().toStdString(),
                remoteTarget.toStdString(),
                err,
                [&](std::size_t done, std::size_t total){ int pct = (total > 0) ? int((done * 100) / total) : 0; dlg.setValue(pct); qApp->processEvents(); },
                [&](){ return dlg.wasCanceled(); }
            );
            dlg.setValue(100);
            if (res) {
                // borrar origen local
                if (QFile::remove(fi.absoluteFilePath())) ++ok; else { ++fail; lastError = "No se pudo borrar origen: " + fi.absoluteFilePath(); }
            } else { ++fail; lastError = QString::fromStdString(err); }
        }

        QString msg = QString("Movidos OK: %1  |  Fallidos: %2  |  Omitidos: %3").arg(ok).arg(fail).arg(skipped);
        if (fail > 0 && !lastError.isEmpty()) msg += "\nÚltimo error: " + lastError;
        statusBar()->showMessage(msg, 5000);
        return;
    }

    // ---- Rama LOCAL→LOCAL existente ----
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

void MainWindow::connectSftp() {
    // Pide datos de conexión
    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Crea cliente (mock por ahora)
    sftp_ = std::make_unique<openscp::Libssh2SftpClient>();
    std::string err;
    const auto opt = dlg.options();
    if (!sftp_->connect(opt, err)) {
        QMessageBox::critical(this, "Error de conexión", QString::fromStdString(err));
        sftp_.reset();
        return;
    }

    // Crea modelo remoto y cámbialo en la vista derecha
    delete rightRemoteModel_;
    rightRemoteModel_ = new RemoteModel(sftp_.get(), this);

    QString e;
    if (!rightRemoteModel_->setRootPath("/", &e)) {
        QMessageBox::critical(this, "Error listando remoto", e);
        sftp_.reset();
        delete rightRemoteModel_;
        rightRemoteModel_ = nullptr;
        return;
    }

    rightView_->setModel(rightRemoteModel_);
    rightPath_->setText("/");
    rightIsRemote_ = true;
    actConnect_->setEnabled(false);
    actDisconnect_->setEnabled(true);
    statusBar()->showMessage("Conectado (SFTP) a " + QString::fromStdString(opt.host), 4000);
    setWindowTitle("OpenSCP (demo) — local/remoto (SFTP)");
    if (actDownloadF7_) actDownloadF7_->setEnabled(true);
    if (actUploadRight_)     actUploadRight_->setEnabled(true);
    if (actNewDirRight_)  actNewDirRight_->setEnabled(true);
    if (actRenameRight_)  actRenameRight_->setEnabled(true);
    if (actDeleteRight_)  actDeleteRight_->setEnabled(true);

}

void MainWindow::disconnectSftp() {
    if (sftp_) sftp_->disconnect();
    sftp_.reset();
    if (rightRemoteModel_) {
        rightView_->setModel(rightLocalModel_);
        delete rightRemoteModel_;
        rightRemoteModel_ = nullptr;
    }
    rightIsRemote_ = false;
    actConnect_->setEnabled(true);
    actDisconnect_->setEnabled(false);
    statusBar()->showMessage("Desconectado", 3000);
    setWindowTitle("OpenSCP (demo) — local/local");

    if (actDownloadF7_) actDownloadF7_->setEnabled(false);
    if (actUploadRight_)     actUploadRight_->setEnabled(false);
    if (actNewDirRight_)  actNewDirRight_->setEnabled(false);
    if (actRenameRight_)  actRenameRight_->setEnabled(false);
    if (actDeleteRight_)  actDeleteRight_->setEnabled(false);

}

// Auxiliar para construir rutas remotas
static QString rJoin(const QString& base, const QString& name) {
  if (base == "/") return "/" + name;
  return base.endsWith('/') ? (base + name) : (base + "/" + name);
}

void MainWindow::newDirRight() {
  if (!rightIsRemote_ || !sftp_ || !rightRemoteModel_) return;
  bool ok = false;
  const QString name = QInputDialog::getText(this, "Nueva carpeta", "Nombre:", QLineEdit::Normal, {}, &ok);
  if (!ok || name.isEmpty()) return;

  const QString path = rJoin(rightRemoteModel_->rootPath(), name);
  std::string err;
  if (!sftp_->mkdir(path.toStdString(), err, 0755)) {
    QMessageBox::critical(this, "SFTP", QString("No se pudo crear carpeta:\n%1").arg(QString::fromStdString(err)));
    return;
  }
  QString dummy;
  rightRemoteModel_->setRootPath(rightRemoteModel_->rootPath(), &dummy); // refresca
}

void MainWindow::renameRightSelected() {
  if (!rightIsRemote_ || !sftp_ || !rightRemoteModel_) return;
  auto sel = rightView_->selectionModel();
  if (!sel) return;
  const auto rows = sel->selectedRows();
  if (rows.size() != 1) {
    QMessageBox::information(this, "Renombrar", "Selecciona exactamente un elemento.");
    return;
  }
  const QModelIndex idx = rows.first();
  const QString oldName = rightRemoteModel_->nameAt(idx);

  bool ok = false;
  const QString newName = QInputDialog::getText(this, "Renombrar",
    "Nuevo nombre:", QLineEdit::Normal, oldName, &ok);
  if (!ok || newName.isEmpty() || newName == oldName) return;

  const QString base = rightRemoteModel_->rootPath();
  const QString from = rJoin(base, oldName);
  const QString to   = rJoin(base, newName);

  std::string err;
  if (!sftp_->rename(from.toStdString(), to.toStdString(), err, /*overwrite*/false)) {
    QMessageBox::critical(this, "SFTP", QString("No se pudo renombrar:\n%1").arg(QString::fromStdString(err)));
    return;
  }
  QString dummy;
  rightRemoteModel_->setRootPath(base, &dummy); // refresca
}

void MainWindow::uploadViaDialog() {
  if (!rightIsRemote_ || !sftp_ || !rightRemoteModel_) {
    QMessageBox::information(this, "Subir", "El panel derecho no es remoto o no hay sesión activa.");
    return;
  }

  // Diálogo no nativo que permite seleccionar múltiples archivos y también carpetas
  const QString startDir = uploadDir_.isEmpty() ? QDir::homePath() : uploadDir_;
  QFileDialog dlg(this, "Selecciona archivos o carpetas a subir", startDir);
  dlg.setFileMode(QFileDialog::ExistingFiles);
  dlg.setOption(QFileDialog::DontUseNativeDialog, true);
  dlg.setOption(QFileDialog::ShowDirsOnly, false);
  dlg.setViewMode(QFileDialog::Detail);
  // Permitir selección múltiple en vistas internas
  if (auto* lv = dlg.findChild<QListView*>("listView")) lv->setSelectionMode(QAbstractItemView::ExtendedSelection);
  if (auto* tv = dlg.findChild<QTreeView*>())           tv->setSelectionMode(QAbstractItemView::ExtendedSelection);

  if (dlg.exec() != QDialog::Accepted) return;
  const QStringList picks = dlg.selectedFiles();
  if (picks.isEmpty()) return;
  uploadDir_ = QFileInfo(picks.first()).dir().absolutePath();

  // Construir cola combinada de archivos (archivos directos + contenido de carpetas)
  QStringList files;
  for (const QString& p : picks) {
    QFileInfo fi(p);
    if (fi.isDir()) {
      QDirIterator it(p, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
      while (it.hasNext()) { it.next(); if (it.fileInfo().isFile()) files << it.filePath(); }
    } else if (fi.isFile()) {
      files << fi.absoluteFilePath();
    }
  }
  if (files.isEmpty()) { statusBar()->showMessage("Nada para subir.", 4000); return; }

  // Subir con colisiones y progreso
  enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
  OverwritePolicy policy = OverwritePolicy::Ask;
  const QString remoteBase = rightRemoteModel_->rootPath();
  int ok = 0, fail = 0, skipped = 0;
  QString lastErr;

  QProgressDialog globalDlg("Subiendo...", "Cancelar", 0, files.size(), this);
  globalDlg.setWindowModality(Qt::ApplicationModal);
  globalDlg.setMinimumDuration(0);

  int index = 0;
  for (const QString& localPath : files) {
    if (globalDlg.wasCanceled()) break;
    const QFileInfo fi(localPath);
    const QString remoteSubdir = fi.path().startsWith(uploadDir_) ? fi.path().mid(uploadDir_.size()).trimmed() : QString();
    // Calcula target en remoto (manteniendo estructura relativa si viene de carpeta)
    QString targetDir = remoteBase;
    if (!remoteSubdir.isEmpty()) {
      QString rel = remoteSubdir;
      if (rel.startsWith('/')) rel.remove(0,1);
      targetDir = joinRemotePath(remoteBase, rel);
      // asegura el directorio remoto contenedor
      bool isDir = false; std::string se;
      bool ex = sftp_->exists(targetDir.toStdString(), isDir, se);
      if (!se.empty()) { ++fail; lastErr = QString::fromStdString(se); ++index; globalDlg.setValue(index); continue; }
      if (!ex) {
        std::string me;
        if (!sftp_->mkdir(targetDir.toStdString(), me, 0755)) { ++fail; lastErr = QString::fromStdString(me); ++index; globalDlg.setValue(index); continue; }
      }
    }

    QString remoteTarget = joinRemotePath(targetDir, fi.fileName());

    // ¿Existe en remoto?
    bool isDir = false; std::string sErr;
    const bool exists = sftp_->exists(remoteTarget.toStdString(), isDir, sErr);
    if (!sErr.empty()) { ++fail; lastErr = QString::fromStdString(sErr); ++index; globalDlg.setValue(index); continue; }
    if (exists) {
      if (policy == OverwritePolicy::Ask) {
        // Comparar tamaño/fecha
        openscp::FileInfo rinfo{}; std::string stErr;
        bool haveStat = sftp_->stat(remoteTarget.toStdString(), rinfo, stErr);
        const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
          .arg(fi.size())
          .arg(QLocale().toString(fi.lastModified(), QLocale::ShortFormat))
          .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
          .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));

        QMessageBox msg(this);
        msg.setWindowTitle("Conflicto remoto");
        msg.setText(QString("«%1» ya existe.\n%2").arg(fi.fileName(), cmp));
        QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
        QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
        QAbstractButton* btRename       = msg.addButton("Renombrar", QMessageBox::ActionRole);
        QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
        QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
        msg.exec();

        if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
        else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;

        if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; ++index; globalDlg.setValue(index); continue; }
        if (msg.clickedButton() == btRename) {
          bool okName=false;
          QString newName = QInputDialog::getText(this, "Renombrar",
               "Nuevo nombre:", QLineEdit::Normal, fi.fileName(), &okName);
          if (!okName || newName.isEmpty()) { ++skipped; ++index; globalDlg.setValue(index); continue; }
          remoteTarget = joinRemotePath(targetDir, newName);
        }
        // overwrite/overwrite all: continuar
      } else if (policy == OverwritePolicy::SkipAll) {
        ++skipped; ++index; globalDlg.setValue(index); continue;
      }
    }

    // Progreso por archivo
    QProgressDialog dlg(QString("Subiendo %1").arg(fi.fileName()), "Cancelar", 0, 100, this);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);

    std::string err;
    bool res = sftp_->put(
        fi.absoluteFilePath().toStdString(),
        remoteTarget.toStdString(),
        err,
        [&](std::size_t done, std::size_t total){
          int pct = (total > 0) ? int((done * 100) / total) : 0;
          dlg.setValue(pct);
          qApp->processEvents();
        },
        [&](){ return dlg.wasCanceled() || globalDlg.wasCanceled(); }
    );
    dlg.setValue(100);

    if (res) ++ok; else { ++fail; lastErr = QString::fromStdString(err); }
    ++index;
    globalDlg.setValue(index);
  }

  // Refrescar remoto
  QString dummy; rightRemoteModel_->setRootPath(remoteBase, &dummy);

  QString msg = QString("Subidos: %1  |  Fallidos: %2  |  Omitidos: %3").arg(ok).arg(fail).arg(skipped);
  if (fail > 0 && !lastErr.isEmpty()) msg += "\nÚltimo error: " + lastErr;
  statusBar()->showMessage(msg, 6000);
}

void MainWindow::uploadDirViaDialog() {
  if (!rightIsRemote_ || !sftp_ || !rightRemoteModel_) {
    QMessageBox::information(this, "Subir carpeta", "El panel derecho no es remoto o no hay sesión activa.");
    return;
  }

  // Selecciona una carpeta local
  const QString startDir = uploadDir_.isEmpty() ? QDir::homePath() : uploadDir_;
  const QString dir = QFileDialog::getExistingDirectory(this, "Selecciona carpeta a subir", startDir);
  if (dir.isEmpty()) return;
  uploadDir_ = dir;

  const QFileInfo root(dir);
  if (!root.exists() || !root.isDir()) {
    QMessageBox::warning(this, "Subir carpeta", "Ruta inválida.");
    return;
  }

  const QString remoteBase = rightRemoteModel_->rootPath();
  const QString remoteDirBase = joinRemotePath(remoteBase, root.fileName());

  // Política de colisión
  enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
  OverwritePolicy policy = OverwritePolicy::Ask;

  // Crea el directorio raíz si no existe
  {
    bool isDir = false; std::string se;
    bool ex = sftp_->exists(remoteDirBase.toStdString(), isDir, se);
    if (!se.empty()) { QMessageBox::critical(this, "SFTP", QString::fromStdString(se)); return; }
    if (!ex) {
      std::string me;
      if (!sftp_->mkdir(remoteDirBase.toStdString(), me, 0755)) {
        QMessageBox::critical(this, "SFTP", QString::fromStdString(me));
        return;
      }
    }
  }

  // Construir lista de archivos a subir (para progreso global)
  QStringList files;
  QDirIterator it(dir, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    if (it.fileInfo().isFile()) files << it.filePath();
  }
  if (files.isEmpty()) { statusBar()->showMessage("No hay archivos para subir.", 4000); return; }

  int ok = 0, fail = 0, skipped = 0;
  QString lastErr;

  QProgressDialog globalDlg("Subiendo carpeta...", "Cancelar", 0, files.size(), this);
  globalDlg.setWindowModality(Qt::ApplicationModal);
  globalDlg.setMinimumDuration(0);

  int index = 0;
  for (const QString& fpath : files) {
    if (globalDlg.wasCanceled()) break;
    const QFileInfo sfi(fpath);
    const QString rel = QDir(dir).relativeFilePath(sfi.absoluteFilePath());
    QString targetPath = joinRemotePath(remoteDirBase, rel);

    // Asegura directorio remoto contenedor
    const QString parentRel = QFileInfo(rel).path();
    const QString parentRemote = parentRel.isEmpty() ? remoteDirBase : joinRemotePath(remoteDirBase, parentRel);
    {
      bool isDir = false; std::string se;
      bool ex = sftp_->exists(parentRemote.toStdString(), isDir, se);
      if (!se.empty()) { ++fail; lastErr = QString::fromStdString(se); ++index; globalDlg.setValue(index); continue; }
      if (!ex) {
        std::string me;
        if (!sftp_->mkdir(parentRemote.toStdString(), me, 0755)) { ++fail; lastErr = QString::fromStdString(me); ++index; globalDlg.setValue(index); continue; }
      }
    }

    // Colisión
    bool isDir = false; std::string sErr;
    const bool exists = sftp_->exists(targetPath.toStdString(), isDir, sErr);
    if (!sErr.empty()) { ++fail; lastErr = QString::fromStdString(sErr); ++index; globalDlg.setValue(index); continue; }
    if (exists) {
      if (policy == OverwritePolicy::Ask) {
        openscp::FileInfo rinfo{}; std::string stErr; bool haveStat = sftp_->stat(targetPath.toStdString(), rinfo, stErr);
        const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
          .arg(sfi.size())
          .arg(QLocale().toString(sfi.lastModified(), QLocale::ShortFormat))
          .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
          .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));

        QMessageBox msg(this);
        msg.setWindowTitle("Conflicto remoto");
        msg.setText(QString("«%1» ya existe.\n%2").arg(rel, cmp));
        QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
        QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
        QAbstractButton* btRename       = msg.addButton("Renombrar", QMessageBox::ActionRole);
        QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
        QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
        msg.exec();

        if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
        else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;

        if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipped; ++index; globalDlg.setValue(index); continue; }
        if (msg.clickedButton() == btRename) {
          bool okName=false;
          const QString baseName = sfi.fileName();
          QString newName = QInputDialog::getText(this, "Renombrar",
               "Nuevo nombre:", QLineEdit::Normal, baseName, &okName);
          if (!okName || newName.isEmpty()) { ++skipped; ++index; globalDlg.setValue(index); continue; }
          const QString newParent = parentRemote; // mismo directorio
          targetPath = joinRemotePath(newParent, newName);
        }
      } else if (policy == OverwritePolicy::SkipAll) {
        ++skipped; ++index; globalDlg.setValue(index); continue;
      }
    }

    // Progreso por archivo
    QProgressDialog dlg(QString("Subiendo %1").arg(rel), "Cancelar", 0, 100, this);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);

    std::string perr;
    bool pres = sftp_->put(sfi.absoluteFilePath().toStdString(), targetPath.toStdString(), perr,
        [&](std::size_t done, std::size_t total){
          int pct = (total > 0) ? int((done * 100) / total) : 0;
          dlg.setValue(pct);
          qApp->processEvents();
        },
        [&](){ return dlg.wasCanceled() || globalDlg.wasCanceled(); });
    dlg.setValue(100);

    if (!pres) { ++fail; lastErr = QString::fromStdString(perr); }
    else { ++ok; }
    ++index; globalDlg.setValue(index);
  }

  // Refresca listado remoto
  QString dummy; rightRemoteModel_->setRootPath(remoteBase, &dummy);

  QString msg = QString("Subidos: %1  |  Fallidos: %2  |  Omitidos: %3").arg(ok).arg(fail).arg(skipped);
  if (fail > 0 && !lastErr.isEmpty()) msg += "\nÚltimo error: " + lastErr;
  statusBar()->showMessage(msg, 6000);
}

void MainWindow::deleteRightSelected() {
  if (!rightIsRemote_ || !sftp_ || !rightRemoteModel_) return;
  auto sel = rightView_->selectionModel();
  if (!sel) return;
  const auto rows = sel->selectedRows();
  if (rows.isEmpty()) {
    QMessageBox::information(this, "Borrar", "Nada seleccionado.");
    return;
  }

  if (QMessageBox::warning(this, "Confirmar borrado",
      "Esto eliminará permanentemente en el servidor remoto.\n¿Continuar?",
      QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;

  int ok = 0, fail = 0;
  QString lastErr;
  const QString base = rightRemoteModel_->rootPath();

  // Progreso con posibilidad de cancelar
  QProgressDialog dlg("Eliminando elementos remotos...", "Cancelar", 0, 0, this);
  dlg.setWindowModality(Qt::ApplicationModal);
  dlg.setMinimumDuration(0);

  // Borrado recursivo
  std::function<bool(const QString&)> deleteRecursive = [&](const QString& p) -> bool {
    if (dlg.wasCanceled()) return false;
    // Intenta listar: si es directorio válido, borra contenido; si falla, podría ser archivo
    std::vector<openscp::FileInfo> out; std::string lerr;
    bool listed = sftp_->list(p.toStdString(), out, lerr);
    if (listed) {
      // p es un directorio
      for (const auto& child : out) {
        const QString childPath = rJoin(p, QString::fromStdString(child.name));
        if (child.is_dir) {
          if (!deleteRecursive(childPath)) return false;
        } else {
          std::string ferr;
          if (!sftp_->removeFile(childPath.toStdString(), ferr)) { lastErr = QString::fromStdString(ferr); return false; }
        }
      }
      // ahora eliminar el propio directorio
      std::string derr;
      if (!sftp_->removeDir(p.toStdString(), derr)) { lastErr = QString::fromStdString(derr); return false; }
      return true;
    } else {
      // Si no se pudo listar, intenta borrar como archivo
      std::string ferr;
      if (!sftp_->removeFile(p.toStdString(), ferr)) { lastErr = QString::fromStdString(ferr); return false; }
      return true;
    }
  };

  for (const QModelIndex& idx : rows) {
    const QString name = rightRemoteModel_->nameAt(idx);
    const QString path = rJoin(base, name);
    if (deleteRecursive(path)) ++ok; else { ++fail; if (dlg.wasCanceled()) break; }
  }

  QString msg = QString("Borrados OK: %1  |  Fallidos: %2").arg(ok).arg(fail);
  if (fail > 0 && !lastErr.isEmpty()) msg += "\nÚltimo error: " + lastErr;
  statusBar()->showMessage(msg, 6000);

  QString dummy;
  rightRemoteModel_->setRootPath(base, &dummy); // refresca
}

void MainWindow::setRightRemoteRoot(const QString& path) {
    if (!rightIsRemote_ || !rightRemoteModel_) return;
    QString e;
    if (!rightRemoteModel_->setRootPath(path, &e)) {
        QMessageBox::warning(this, "Error remoto", e);
        return;
    }
    rightPath_->setText(path);
}

void MainWindow::rightItemActivated(const QModelIndex& idx) {
  if (!rightIsRemote_ || !rightRemoteModel_) return;

  if (rightRemoteModel_->isDir(idx)) {
    const QString name = rightRemoteModel_->nameAt(idx);
    QString next = rightRemoteModel_->rootPath();
    if (!next.endsWith('/')) next += '/';
    next += name;
    setRightRemoteRoot(next);
    return;
  }

  // Si es archivo: descargar y abrir
  const QString name = rightRemoteModel_->nameAt(idx);
  QString remotePath = rightRemoteModel_->rootPath();
  if (!remotePath.endsWith('/')) remotePath += '/';
  remotePath += name;

  const QString localPath = tempDownloadPathFor(name);

  if (!sftp_) {
    QMessageBox::warning(this, "Remoto", "No hay sesión SFTP activa.");
    return;
  }

  // Progreso
  QProgressDialog dlg("Descargando " + name, "Cancelar", 0, 100, this);
  dlg.setWindowModality(Qt::ApplicationModal);
  dlg.setMinimumDuration(0);

  std::string err;
  bool ok = sftp_->get(remotePath.toStdString(), localPath.toStdString(), err,
                       [&](std::size_t done, std::size_t total) {
                         int pct = (total > 0) ? int((done * 100) / total) : 0;
                         dlg.setValue(pct);
                         qApp->processEvents();
                       },
                       [&]() { return dlg.wasCanceled(); });
  dlg.setValue(100);

  if (!ok) {
    QMessageBox::critical(this, "Descarga fallida", QString::fromStdString(err));
    return;
  }

  // Abrir con app por defecto
  QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
  statusBar()->showMessage("Descargado: " + localPath, 5000);
}

void MainWindow::goUpLeft() {
    QString cur = leftPath_->text();
    QDir d(cur);
    if (!d.cdUp()) return;
    setLeftRoot(d.absolutePath());
}


void MainWindow::goUpRight() {
  if (rightIsRemote_) {
    if (!rightRemoteModel_) return;
    QString cur = rightRemoteModel_->rootPath();
    if (cur == "/" || cur.isEmpty()) return;
    // quitar último segmento
    QString parent = cur;
    if (parent.endsWith('/')) parent.chop(1);
    int slash = parent.lastIndexOf('/');
    parent = (slash <= 0) ? "/" : parent.left(slash);
    setRightRemoteRoot(parent);
  } else {
    QString cur = rightPath_->text();
    QDir d(cur);
    if (!d.cdUp()) return;
    setRightRoot(d.absolutePath());
  }
}

void MainWindow::downloadRightToLeft() {
    if (!rightIsRemote_) {
        QMessageBox::information(this, "Descargar", "El panel derecho no es remoto.");
        return;
    }
    if (!sftp_ || !rightRemoteModel_) {
        QMessageBox::warning(this, "SFTP", "No hay sesión SFTP activa.");
        return;
    }

    // 1) Pregunta al usuario una carpeta local de destino (recordando la última)
    const QString picked = QFileDialog::getExistingDirectory(
        this,
        "Selecciona carpeta de destino (local)",
        downloadDir_.isEmpty() ? QDir::homePath() : downloadDir_);
    if (picked.isEmpty())
        return; // canceló

    // Guardar para siguientes descargas
    downloadDir_ = picked;

    QDir dst(downloadDir_);
    if (!dst.exists()) {
        QMessageBox::warning(this, "Destino inválido", "La carpeta de destino no existe.");
        return;
    }

    // 2) Toma selección remota (panel derecho)
    auto sel = rightView_->selectionModel();
    if (!sel) { QMessageBox::warning(this, "Descargar", "No hay selección."); return; }
    const auto rows = sel->selectedRows(NAME_COL);
    if (rows.isEmpty()) { QMessageBox::information(this, "Descargar", "Nada seleccionado."); return; }

    // Construir cola (archivos) a descargar: para cada selección, si es carpeta, recorrer recursivo.
    struct Task { QString remote; QString local; }; 
    QVector<Task> queue;

    auto joinRemote = [&](const QString& base, const QString& name) {
        return (base == "/") ? ("/" + name) : (base.endsWith('/') ? base + name : base + "/" + name);
    };

    const QString remoteBase = rightRemoteModel_->rootPath();

    std::function<bool(const QString&, const QString&)> enqueueDir = [&](const QString& rdir, const QString& ldir) -> bool {
        // Asegura carpeta local
        if (!QDir().mkpath(ldir)) return false;
        // Lista remoto
        std::vector<openscp::FileInfo> out; std::string lerr;
        if (!sftp_->list(rdir.toStdString(), out, lerr)) return false;
        for (const auto& e : out) {
            const QString rchild = joinRemote(rdir, QString::fromStdString(e.name));
            const QString lchild = QDir(ldir).filePath(QString::fromStdString(e.name));
            if (e.is_dir) {
                if (!enqueueDir(rchild, lchild)) return false;
            } else {
                queue.push_back({ rchild, lchild });
            }
        }
        return true;
    };

    for (const QModelIndex& idx : rows) {
        const QString name = rightRemoteModel_->nameAt(idx);
        const QString rpath = joinRemote(remoteBase, name);
        const QString lpath = dst.filePath(name);
        if (rightRemoteModel_->isDir(idx)) {
            if (!enqueueDir(rpath, lpath)) {
                QMessageBox::warning(this, "Descargar", "No se pudo listar carpeta remota (sin permisos o error de red).");
            }
        } else {
            queue.push_back({ rpath, lpath });
        }
    }

    if (queue.isEmpty()) {
        statusBar()->showMessage("Nada para descargar.", 4000);
        return;
    }

    // Políticas de colisión para descargas
    enum class OverwritePolicy { Ask, OverwriteAll, SkipAll };
    OverwritePolicy policy = OverwritePolicy::Ask;

    int ok = 0, fail = 0, skipCount = 0;
    QString lastErr;

    QProgressDialog globalDlg("Descargando...", "Cancelar", 0, queue.size(), this);
    globalDlg.setWindowModality(Qt::ApplicationModal);
    globalDlg.setMinimumDuration(0);
    int index = 0;

    for (int i = 0; i < queue.size(); ++i) {
        Task& t = queue[i];
        if (globalDlg.wasCanceled()) break;

        // Colisión local
        QFileInfo lfi(t.local);
        if (lfi.exists()) {
            if (policy == OverwritePolicy::Ask) {
                // Stat remoto para comparar
                openscp::FileInfo rinfo{}; std::string stErr;
                bool haveStat = sftp_->stat(t.remote.toStdString(), rinfo, stErr);
    const QString cmp = QString("Local: %1 bytes, %2\nRemoto: %3 bytes, %4")
                  .arg(lfi.size())
                  .arg(QLocale().toString(lfi.lastModified(), QLocale::ShortFormat))
                  .arg(haveStat ? QString::number(rinfo.size) : QString("?"))
                  .arg(haveStat && rinfo.mtime>0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch((qint64)rinfo.mtime), QLocale::ShortFormat) : QString("?"));

                QMessageBox msg(this);
                msg.setWindowTitle("Conflicto en descarga");
                msg.setText(QString("«%1» ya existe.\n%2").arg(QFileInfo(t.local).fileName(), cmp));
                QAbstractButton* btOverwrite    = msg.addButton("Sobrescribir", QMessageBox::AcceptRole);
                QAbstractButton* btSkip         = msg.addButton("Omitir", QMessageBox::RejectRole);
                QAbstractButton* btRename       = msg.addButton("Renombrar", QMessageBox::ActionRole);
                QAbstractButton* btOverwriteAll = msg.addButton("Sobrescribir todo", QMessageBox::YesRole);
                QAbstractButton* btSkipAll      = msg.addButton("Omitir todo", QMessageBox::NoRole);
                msg.exec();

                if (msg.clickedButton() == btOverwriteAll) policy = OverwritePolicy::OverwriteAll;
                else if (msg.clickedButton() == btSkipAll) policy = OverwritePolicy::SkipAll;

                if (msg.clickedButton() == btSkip || policy == OverwritePolicy::SkipAll) { ++skipCount; ++index; globalDlg.setValue(index); continue; }
                if (msg.clickedButton() == btRename) {
                    bool okName=false;
                    QString newName = QInputDialog::getText(this, "Renombrar",
                         "Nuevo nombre:", QLineEdit::Normal, QFileInfo(t.local).fileName(), &okName);
                    if (!okName || newName.isEmpty()) { ++skipCount; ++index; globalDlg.setValue(index); continue; }
                    const QString parent = QFileInfo(t.local).dir().absolutePath();
                    QString newLocal = QDir(parent).filePath(newName);
                    lfi = QFileInfo(newLocal);
                    t.local = newLocal;
                }
                // Overwrite o OverwriteAll → continúa
            } else if (policy == OverwritePolicy::SkipAll) {
                ++skipCount; ++index; globalDlg.setValue(index); continue;
            }
        }

        globalDlg.setLabelText(QString("%1 / %2 — %3")
            .arg(index+1).arg(queue.size()).arg(QFileInfo(t.local).fileName()));

        std::string err;
        bool res = sftp_->get(
            t.remote.toStdString(),
            t.local.toStdString(),
            err,
            [&](std::size_t done, std::size_t total){
                if (globalDlg.wasCanceled()) return; // UI responsive
                int pct = (total > 0) ? int((done * 100) / total) : 0;
                globalDlg.setLabelText(QString("%1 / %2 — %3 (%4%)")
                    .arg(index+1).arg(queue.size()).arg(QFileInfo(t.local).fileName()).arg(pct));
                qApp->processEvents();
            },
            [&]() { return globalDlg.wasCanceled(); }
        );
        if (res) ++ok; else { ++fail; lastErr = QString::fromStdString(err); }
        ++index;
        globalDlg.setValue(index);
    }

    QString msg = QString("Descargados OK: %1  |  Fallidos: %2  |  Omitidos: %3")
                    .arg(ok).arg(fail).arg(skipCount);
    if (fail > 0 && !lastErr.isEmpty()) msg += "\nÚltimo error: " + lastErr;
    statusBar()->showMessage(msg, 6000);
}
