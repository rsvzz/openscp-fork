// Declaration of the main window and its state/actions.
#pragma once
#include <QMainWindow>
#include <QFileSystemModel>
#include <QTreeView>
#include <QLineEdit>
#include <QAction>
#include <string>
#include <memory>

class RemoteModel;              // fwd
class QModelIndex;              // fwd for slot signatures
class QToolBar;                 // fwd
class QMenu;                    // fwd
class QEvent;                   // fwd for eventFilter
class QDialog;                  // fwd
namespace openscp { class SftpClient; struct SessionOptions; } // fwd

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void showEvent(QShowEvent* e) override;

private slots:
    void chooseLeftDir();
    void chooseRightDir();
    void leftPathEntered();
    void rightPathEntered();
    void copyLeftToRight(); // F5
    void copyRightToLeft(); // remote -> left (no dialog)
    void moveRightToLeft(); // move selection from right panel to left
    void moveLeftToRight(); // F6
    void deleteFromLeft();  // Delete
    void goUpRight(); // Go up one level (right)
    void goUpLeft();  // Go up one level (left)

    void connectSftp();
    void disconnectSftp();
    void rightItemActivated(const QModelIndex& idx); // double click on remote
    void leftItemActivated(const QModelIndex& idx);  // double click on local (left)
    void downloadRightToLeft(); // remote -> local
    void uploadViaDialog();     // local -> remote (dialog: files or folder)
    void newDirRight();
    void newFileRight();
    void renameRightSelected();
    void deleteRightSelected();
    void showRightContextMenu(const QPoint& pos);
    void changeRemotePermissions();
    void showLeftContextMenu(const QPoint& pos);
    void newDirLeft();
    void newFileLeft();
    void renameLeftSelected();

    // Application menu
    void showAboutDialog();
    void showSettingsDialog();

private:
    void updateDeleteShortcutEnables();
    void applyPreferences();
    // Remote state (a single active session)
    std::unique_ptr<openscp::SftpClient> sftp_;
    bool rightIsRemote_ = false;

    void setLeftRoot(const QString& path);
    void setRightRoot(const QString& path);       // local
    void setRightRemoteRoot(const QString& path); // remote

    // Models
    QFileSystemModel* leftModel_        = nullptr;
    QFileSystemModel* rightLocalModel_  = nullptr;
    RemoteModel*      rightRemoteModel_ = nullptr;

    // Views and path inputs
    QTreeView* leftView_  = nullptr;
    QTreeView* rightView_ = nullptr;

    QLineEdit* leftPath_  = nullptr;
    QLineEdit* rightPath_ = nullptr;

    // Actions
    QAction* actChooseLeft_  = nullptr;
    QAction* actChooseRight_ = nullptr;
    QAction* actCopyF5_      = nullptr;
    QAction* actCopyRight_   = nullptr; // Copy from right (remote) panel to left
    QAction* actMoveRight_   = nullptr; // Move from right panel to left
    QAction* actMoveF6_      = nullptr;
    QAction* actDelete_      = nullptr;
    QAction* actConnect_     = nullptr;
    QAction* actDisconnect_  = nullptr;
    QAction* actDownloadF7_ = nullptr;
    QAction* actUploadRight_ = nullptr;
    QAction* actNewDirRight_  = nullptr;
    QAction* actNewFileRight_ = nullptr;
    QAction* actRenameRight_  = nullptr;
    QAction* actDeleteRight_  = nullptr; // remote
    QAction* actNewDirLeft_   = nullptr; // local (left)
    QAction* actNewFileLeft_  = nullptr; // local (left)
    QAction* actRenameLeft_   = nullptr; // local (left)
    QAction* actCopyRightTb_  = nullptr; // right toolbar: Copy (generic text)
    QAction* actMoveRightTb_  = nullptr; // right toolbar: Move (generic text)

    // Sub-toolbar actions
    QAction* actUpLeft_  = nullptr; // back left
    QAction* actUpRight_ = nullptr; // back right

    // Sub-toolbars
    QToolBar* leftPaneBar_  = nullptr;
    QToolBar* rightPaneBar_ = nullptr;
    QMenu*     rightContextMenu_ = nullptr;
    QMenu*     leftContextMenu_  = nullptr;

    // Transfer queue
    class TransferManager* transferMgr_ = nullptr;
    class TransferQueueDialog* transferDlg_ = nullptr;
    QAction* actShowQueue_ = nullptr;
    QAction* actSites_     = nullptr; // site manager
    QAction* actPrefsToolbar_ = nullptr; // settings button (right toolbar)
    QAction* actAboutToolbar_ = nullptr; // about button (right toolbar)

    // Top menu
    QMenu* appMenu_    = nullptr; // OpenSCP
    QMenu* fileMenu_   = nullptr; // File
    QAction* actAbout_ = nullptr;
    QAction* actPrefs_ = nullptr;
    QAction* actQuit_  = nullptr;

    // Downloads
    QString downloadDir_; // last local folder chosen for downloads
    QString uploadDir_;   // last local folder chosen for uploads

    // Host key confirmation (TOFU)
    bool confirmHostKeyUI(const QString& host, quint16 port, const QString& algorithm, const QString& fingerprint);

    // Helpers for connecting and wiring up the remote UI
    bool establishSftpAsync(openscp::SessionOptions opt, std::string& err);
    void applyRemoteConnectedUI(const openscp::SessionOptions& opt);

    // Writable state of the current remote directory
    bool rightRemoteWritable_ = false;
    // Recompute if the current remote directory is writable (create/remove a temporary folder)
    void updateRemoteWriteability();

    bool firstShow_ = true;

    // User preferences
    bool prefShowHidden_ = false;
    bool prefSingleClick_ = false;
    bool prefOpenRevealInFolder_ = false; // if true, reveal downloaded/opened files in folder instead of opening directly
    QMetaObject::Connection leftClickConn_;
    QMetaObject::Connection rightClickConn_;
};
