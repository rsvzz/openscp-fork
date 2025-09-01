#pragma once
#include <QMainWindow>
#include <QFileSystemModel>
#include <QTreeView>
#include <QLineEdit>
#include <QAction>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void chooseLeftDir();
    void chooseRightDir();
    void leftPathEntered();
    void rightPathEntered();
    void copyLeftToRight(); // F5
    void moveLeftToRight(); // F6
    void deleteFromLeft(); // Supr



private:
    void setLeftRoot(const QString& path);
    void setRightRoot(const QString& path);
    

    QFileSystemModel* leftModel_  = nullptr;
    QFileSystemModel* rightModel_ = nullptr;

    QTreeView* leftView_  = nullptr;
    QTreeView* rightView_ = nullptr;

    QLineEdit* leftPath_  = nullptr;
    QLineEdit* rightPath_ = nullptr;

    // acciones
    QAction* actChooseLeft_  = nullptr;
    QAction* actChooseRight_ = nullptr;
    QAction* actCopyF5_      = nullptr;
    QAction* actMoveF6_ = nullptr;
    QAction* actDelete_ = nullptr;
};