/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include "view.h"
#include <libfm-qt6/filemenu.h>
#include <libfm-qt6/foldermenu.h>
#include "settings.h"
#include "application.h"
#include "mainwindow.h"
#include "launcher.h"
#include <QAction>
#include <QHeaderView>
#include <QTreeView>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QApplication>
#include <QRubberBand>
#include <QScrollBar>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <libfm-qt6/fileoperation.h>
#include <libfm-qt6/utilities.h>

namespace PCManFM {

static Qt::DropAction askDropAction(Qt::DropActions possibleActions, QPoint pos, QWidget* parent) {
    QMenu menu(parent);
    QAction* copyAction = nullptr;
    QAction* moveAction = nullptr;
    QAction* linkAction = nullptr;
    if(possibleActions.testFlag(Qt::CopyAction)) {
        copyAction = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QObject::tr("Copy here"));
    }
    if(possibleActions.testFlag(Qt::MoveAction)) {
        moveAction = menu.addAction(QObject::tr("Move here"));
    }
    if(possibleActions.testFlag(Qt::LinkAction)) {
        linkAction = menu.addAction(QObject::tr("Create symlink here"));
    }
    menu.addSeparator();
    menu.addAction((copyAction || moveAction || linkAction) ? QObject::tr("Cancel") : QObject::tr("Cannot drop here"));

    QAction* chosen = menu.exec(pos);
    if(chosen) {
        if(chosen == copyAction) return Qt::CopyAction;
        if(chosen == moveAction) return Qt::MoveAction;
        if(chosen == linkAction) return Qt::LinkAction;
    }
    return Qt::IgnoreAction;
}

View::View(Fm::FolderView::ViewMode _mode, QWidget* parent):
    Fm::FolderView(_mode, parent) {

    Settings& settings = static_cast<Application*>(qApp)->settings();
    updateFromSettings(settings);
}

View::~View() {
    if(rubberBand_) {
        delete rubberBand_;
    }
}

void View::onFileClicked(int type, const std::shared_ptr<const Fm::FileInfo>& fileInfo) {
    if(type == MiddleClick) {
        if(fileInfo && fileInfo->isDir()) {
            // fileInfo->path() shouldn't be used directly because
            // it won't work in places like computer:/// or network:///
            Fm::FileInfoList files;
            files.emplace_back(fileInfo);
            launchFiles(std::move(files), true);
        }
    }
    else {
        if(type == ActivatedClick) {
            if(fileLauncher()) { // launch all selected files
                auto files = selectedFiles();
                if(!files.empty()) {
                    if(files.size() > 20) {
                        QMessageBox::StandardButton r = QMessageBox::question(window(),
                                                        tr("Many files"),
                                                        tr("Do you want to open these %1 files?", nullptr, files.size()).arg(files.size()),
                                                        QMessageBox::Yes | QMessageBox::No,
                                                        QMessageBox::No);
                        if(r == QMessageBox::No) {
                            return;
                        }
                    }
                    launchFiles(std::move(files));
                }
            }
        }
        else {
            Fm::FolderView::onFileClicked(type, fileInfo);
        }
    }
}

void View::onNewWindow() {
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    if(files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front());
    }
    else {
        Application* app = static_cast<Application*>(qApp);
        app->openFolders(std::move(files));
    }
}

void View::onNewTab() {
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    if(files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front(), true);
    }
    else {
        launchFiles(std::move(files), true);
    }
}

void View::onOpenInTerminal() {
    Application* app = static_cast<Application*>(qApp);
    Fm::FileMenu* menu = static_cast<Fm::FileMenu*>(sender()->parent());
    auto files = menu->files();
    for(auto& file: files) {
        app->openFolderInTerminal(file->path());
    }
}

void View::onSearch() {

}

void View::prepareFileMenu(Fm::FileMenu* menu) {
    Application* app = static_cast<Application*>(qApp);
    menu->setConfirmDelete(app->settings().confirmDelete());
    menu->setConfirmTrash(app->settings().confirmTrash());
    menu->setUseTrash(app->settings().useTrash());

    // add some more menu items for dirs
    bool all_native = true;
    bool all_directory = true;
    auto files = menu->files();
    for(auto& fi: files) {
        if(!fi->isDir()) {
            all_directory = false;
        }
        else if(fi->isDir() && !fi->isNative()) {
            all_native = false;
        }
    }

    if(all_directory) {
        QAction* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Open in New T&ab"), menu);
        connect(action, &QAction::triggered, this, &View::onNewTab);
        menu->insertAction(menu->separator1(), action);

        action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Open in New Win&dow"), menu);
        connect(action, &QAction::triggered, this, &View::onNewWindow);
        menu->insertAction(menu->separator1(), action);

        // TODO: add search
        // action = menu->addAction(_("Search"));

        if(all_native) {
            action = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);
            connect(action, &QAction::triggered, this, &View::onOpenInTerminal);
            menu->insertAction(menu->separator1(), action);
        }
    }
    else {
        if(menu->pasteAction()) { // nullptr for trash
            menu->pasteAction()->setVisible(false);
        }
        if(menu->createAction()) {
            menu->createAction()->setVisible(false);
        }

        if(folder() && folder()->path().hasUriScheme("search")
           && files.size() == 1 && !files.front()->isDir()) {
            QAction* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Show in New T&ab"), menu);
            connect(action, &QAction::triggered, this, &View::onNewTab);
            menu->insertAction(menu->separator1(), action);

            action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Show in New Win&dow"), menu);
            connect(action, &QAction::triggered, this, &View::onNewWindow);
            menu->insertAction(menu->separator1(), action);
        }
    }
}

void View::prepareFolderMenu(Fm::FolderMenu* menu) {
    auto folder = folderInfo();
    if(folder && folder->isNative()) {
        QAction *action = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);
        connect(action, &QAction::triggered, this, [folder] {
            Application* app = static_cast<Application*>(qApp);
            app->openFolderInTerminal(folder->path());
        });
        menu->insertAction(menu->createAction(), action);
        menu->insertSeparator(menu->createAction());
    }
}

void View::updateFromSettings(Settings& settings) {

    setIconSize(Fm::FolderView::IconMode, QSize(settings.bigIconSize(), settings.bigIconSize()));
    setIconSize(Fm::FolderView::CompactMode, QSize(settings.smallIconSize(), settings.smallIconSize()));
    setIconSize(Fm::FolderView::ThumbnailMode, QSize(settings.thumbnailIconSize(), settings.thumbnailIconSize()));
    setIconSize(Fm::FolderView::DetailedListMode, QSize(settings.smallIconSize(), settings.smallIconSize()));

    setMargins(settings.folderViewCellMargins());

    setAutoSelectionDelay(settings.singleClick() ? settings.autoSelectionDelay() : 0);

    setCtrlRightClick(settings.ctrlRightClick());

    setScrollPerPixel(settings.scrollPerPixel());

    Fm::ProxyFolderModel* proxyModel = model();
    if(proxyModel) {
        proxyModel->setShowThumbnails(settings.showThumbnails());
        proxyModel->setBackupAsHidden(settings.backupAsHidden());
    }
}

void View::launchFiles(Fm::FileInfoList files, bool inNewTabs) {
    if(fileLauncher()) {
        if(auto launcher = dynamic_cast<Launcher*>(fileLauncher())) {
            // this happens on desktop
            if(!launcher->hasMainWindow()) {
                if(!inNewTabs && launcher->openWithDefaultFileManager()) {
                    launcher->launchFiles(nullptr, std::move(files));
                    return;
                }
                if(inNewTabs || static_cast<Application*>(qApp)->settings().singleWindowMode()) {
                    MainWindow* window = MainWindow::lastActive();
                    // if there is no last active window, find the last created window
                    if(window == nullptr) {
                        QWidgetList windows = qApp->topLevelWidgets();
                        for(int i = 0; i < windows.size(); ++i) {
                            auto win = windows.at(windows.size() - 1 - i);
                            if(win->inherits("PCManFM::MainWindow")) {
                                window = static_cast<MainWindow*>(win);
                                break;
                            }
                        }
                    }
                    auto tempLauncher = Launcher(window);
                    tempLauncher.openInNewTab();
                    tempLauncher.launchFiles(nullptr, std::move(files));
                    return;
                }
            }
            if(inNewTabs) {
                launcher->openInNewTab();
            }
        }
        fileLauncher()->launchFiles(nullptr, std::move(files));
    }
}

void View::openFolderAndSelectFile(const std::shared_ptr<const Fm::FileInfo>& fileInfo, bool inNewTab) {
    if(auto win = qobject_cast<MainWindow*>(window())) {
        Fm::FilePathList paths;
        paths.emplace_back(fileInfo->path());
        win->openFolderAndSelectFiles(std::move(paths), inNewTab);
    }
}

bool View::eventFilter(QObject* watched, QEvent* event) {
    if(childView() && watched == childView()->viewport() && viewMode() == DetailedListMode) {
        if(QTreeView* treeView = qobject_cast<QTreeView*>(childView())) {
            switch(event->type()) {
            case QEvent::MouseButtonPress: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if(me->button() == Qt::LeftButton) {
                    int logicalCol = treeView->header()->logicalIndexAt(me->position().toPoint().x());
                    bool onEmptySpace = !treeView->indexAt(me->position().toPoint()).isValid();
                    if(logicalCol != Fm::FolderModel::ColumnFileName || onEmptySpace) {
                        leftPressAfterName_ = true;
                        marqueeActive_ = false;
                        leftPressPoint_ = me->position().toPoint();
                        pressScrollX_ = treeView->horizontalScrollBar() ? treeView->horizontalScrollBar()->value() : 0;
                        pressScrollY_ = treeView->verticalScrollBar() ? treeView->verticalScrollBar()->value() : 0;
                        leftPressModifiers_ = me->modifiers();
                        if(treeView->selectionModel()) {
                            savedSelection_ = treeView->selectionModel()->selection();
                        }
                        else {
                            savedSelection_ = QItemSelection();
                        }
                        if(me->modifiers() == Qt::NoModifier) {
                            treeView->clearSelection();
                            treeView->setCurrentIndex(QModelIndex());
                        }
                        return true; // Consume event to prevent QAbstractItemView from starting drag & drop
                    }
                    else {
                        leftPressAfterName_ = false;
                        marqueeActive_ = false;
                    }
                }
                break;
            }
            case QEvent::MouseMove: {
                if(leftPressAfterName_) {
                    QMouseEvent* me = static_cast<QMouseEvent*>(event);
                    QPoint curPos = me->position().toPoint();
                    if(!marqueeActive_) {
                        if((curPos - leftPressPoint_).manhattanLength() > QApplication::startDragDistance()) {
                            marqueeActive_ = true;
                            if(!rubberBand_) {
                                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, treeView->viewport());
                            }
                        }
                    }
                    if(marqueeActive_) {
                        int curScrollX = treeView->horizontalScrollBar() ? treeView->horizontalScrollBar()->value() : 0;
                        int curScrollY = treeView->verticalScrollBar() ? treeView->verticalScrollBar()->value() : 0;
                        int originX = leftPressPoint_.x() + pressScrollX_ - curScrollX;
                        int originY = leftPressPoint_.y() + pressScrollY_ - curScrollY;
                        QRect rect = QRect(QPoint(originX, originY), curPos).normalized();

                        if(rubberBand_) {
                            rubberBand_->setGeometry(rect);
                            if(!rubberBand_->isVisible()) {
                                rubberBand_->show();
                            }
                        }

                        // Auto-scroll if dragging near/beyond vertical edges
                        if(curPos.y() < 10) {
                            int v = treeView->verticalScrollBar()->value();
                            treeView->verticalScrollBar()->setValue(v - 20);
                        }
                        else if(curPos.y() > treeView->viewport()->height() - 10) {
                            int v = treeView->verticalScrollBar()->value();
                            treeView->verticalScrollBar()->setValue(v + 20);
                        }

                        // Update selection for rows intersecting rect vertically
                        if(treeView->model() && treeView->selectionModel()) {
                            int totalRows = treeView->model()->rowCount();
                            int col0Pos = treeView->header()->sectionViewportPosition(Fm::FolderModel::ColumnFileName);
                            int col0Size = treeView->header()->sectionSize(Fm::FolderModel::ColumnFileName);
                            int safeX = qBound(5, col0Pos + col0Size / 2, qMax(5, treeView->viewport()->width() - 5));

                            int startRow = 0;
                            if(rect.top() > 0) {
                                QModelIndex idxAtTop = treeView->indexAt(QPoint(safeX, rect.top()));
                                if(idxAtTop.isValid()) {
                                    startRow = idxAtTop.row();
                                }
                            }

                            int minRow = -1;
                            int maxRow = -1;
                            for(int r = startRow; r < totalRows; ++r) {
                                QModelIndex idx = treeView->model()->index(r, 0);
                                QRect vr = treeView->visualRect(idx);
                                if(vr.top() > rect.bottom()) {
                                    break;
                                }
                                if(vr.bottom() >= rect.top() && vr.top() <= rect.bottom()) {
                                    if(minRow == -1) {
                                        minRow = r;
                                    }
                                    maxRow = r;
                                }
                                if(vr.top() > treeView->viewport()->height()) {
                                    break;
                                }
                            }

                            QItemSelection marqueeSelection;
                            if(minRow != -1 && maxRow != -1) {
                                QModelIndex topIdx = treeView->model()->index(minRow, 0);
                                QModelIndex bottomIdx = treeView->model()->index(maxRow, 0);
                                marqueeSelection = QItemSelection(topIdx, bottomIdx);
                            }

                            if(leftPressModifiers_ & Qt::ControlModifier) {
                                QItemSelection current = savedSelection_;
                                current.merge(marqueeSelection, QItemSelectionModel::Toggle);
                                treeView->selectionModel()->select(current, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            }
                            else if(leftPressModifiers_ & Qt::ShiftModifier) {
                                QItemSelection current = savedSelection_;
                                current.merge(marqueeSelection, QItemSelectionModel::Select);
                                treeView->selectionModel()->select(current, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            }
                            else {
                                treeView->selectionModel()->select(marqueeSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            }
                        }
                        return true; // Consume event while dragging marquee
                    }
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if(me->button() == Qt::LeftButton && leftPressAfterName_) {
                    leftPressAfterName_ = false;
                    if(marqueeActive_) {
                        marqueeActive_ = false;
                        if(rubberBand_) {
                            rubberBand_->hide();
                        }
                    }
                    else {
                        // Click without drag
                        if(me->modifiers() == Qt::NoModifier) {
                            treeView->clearSelection();
                            treeView->setCurrentIndex(QModelIndex());
                        }
                        else if((me->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) && treeView->selectionModel()) {
                            treeView->selectionModel()->select(savedSelection_, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                        }
                    }
                    return true; // Consume event to prevent activation and keep selection state
                }
                break;
            }
            case QEvent::MouseButtonDblClick: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if(me->button() == Qt::LeftButton) {
                    int logicalCol = treeView->header()->logicalIndexAt(me->position().toPoint().x());
                    bool onEmptySpace = !treeView->indexAt(me->position().toPoint()).isValid();
                    if(logicalCol != Fm::FolderModel::ColumnFileName || onEmptySpace) {
                        return true; // Consume double click beyond Name category
                    }
                }
                break;
            }
            case QEvent::KeyPress: {
                QKeyEvent* ke = static_cast<QKeyEvent*>(event);
                if(ke->key() == Qt::Key_Escape && marqueeActive_) {
                    marqueeActive_ = false;
                    leftPressAfterName_ = false;
                    if(rubberBand_) {
                        rubberBand_->hide();
                    }
                    if(treeView->selectionModel()) {
                        treeView->selectionModel()->select(savedSelection_, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    }
                    return true;
                }
                break;
            }
            default:
                break;
            }
        }
    }
    return Fm::FolderView::eventFilter(watched, event);
}

void View::contextMenuEvent(QContextMenuEvent* event) {
    if(event->reason() == QContextMenuEvent::Mouse && viewMode() == DetailedListMode) {
        if(QTreeView* treeView = qobject_cast<QTreeView*>(childView())) {
            QPoint viewport_pos = treeView->viewport()->mapFromGlobal(event->globalPos());
            int logicalCol = treeView->header()->logicalIndexAt(viewport_pos.x());
            bool onEmptySpace = !treeView->indexAt(viewport_pos).isValid();
            if(logicalCol != Fm::FolderModel::ColumnFileName || onEmptySpace) {
                // Clicked to the right after the name category or on empty space: show context menu for current directory
                treeView->clearSelection();
                treeView->setCurrentIndex(QModelIndex());
                if(folderInfo()) {
                    Fm::FolderMenu* folderMenu = new Fm::FolderMenu(this, this);
                    prepareFolderMenu(folderMenu);
                    folderMenu->exec(event->globalPos());
                    delete folderMenu;
                }
                return;
            }
        }
    }
    Fm::FolderView::contextMenuEvent(event);
}

void View::childDragMoveEvent(QDragMoveEvent* e) {
    if(viewMode() == DetailedListMode) {
        if(QTreeView* treeView = qobject_cast<QTreeView*>(childView())) {
            int logicalCol = treeView->header()->logicalIndexAt(e->position().toPoint().x());
            if(logicalCol != Fm::FolderModel::ColumnFileName) {
                // Beyond the Name category: disable drop indicator on subfolders
                treeView->setDropIndicatorShown(false);
                e->acceptProposedAction();
                return;
            }
        }
    }
    Fm::FolderView::childDragMoveEvent(e);
}

void View::childDropEvent(QDropEvent* e) {
    if(viewMode() == DetailedListMode) {
        if(QTreeView* treeView = qobject_cast<QTreeView*>(childView())) {
            int logicalCol = treeView->header()->logicalIndexAt(e->position().toPoint().x());
            if(logicalCol != Fm::FolderModel::ColumnFileName) {
                // Drop occurred beyond the Name column: drop into current directory instead of subfolder
                Fm::FilePath destPath = path();
                auto info = folderInfo();
                Fm::FilePathList srcPaths;
                if(e->mimeData()->hasFormat(QStringLiteral("libfm/files"))) {
                    QByteArray _data = e->mimeData()->data(QStringLiteral("libfm/files"));
                    srcPaths = Fm::pathListFromUriList(_data.data());
                }
                if(srcPaths.empty() && e->mimeData()->hasUrls()) {
                    srcPaths = Fm::pathListFromQUrls(e->mimeData()->urls());
                }

                if(!srcPaths.empty()) {
                    Qt::DropActions actions = Qt::IgnoreAction;
                    if(info && info->isWritableDirectory() && info->isWritable()) {
                        actions = e->possibleActions();
                    }
                    auto curPos = treeView->viewport()->mapToGlobal(e->position().toPoint());
                    QTimer::singleShot(0, treeView, [this, curPos, actions, srcPaths, destPath] {
                        Qt::DropAction action;
                        switch(QApplication::keyboardModifiers()) {
                        case Qt::ControlModifier:
                            action = Qt::CopyAction;
                            break;
                        case Qt::ShiftModifier:
                            action = Qt::MoveAction;
                            break;
                        case Qt::ControlModifier | Qt::ShiftModifier:
                            action = Qt::LinkAction;
                            break;
                        default:
                            action = askDropAction(actions, curPos, childView());
                            break;
                        }

                        Q_EMIT dropIsDecided(action != Qt::IgnoreAction);

                        switch(action) {
                        case Qt::CopyAction:
                            Fm::FileOperation::copyFiles(srcPaths, destPath);
                            break;
                        case Qt::MoveAction:
                            Fm::FileOperation::moveFiles(srcPaths, destPath);
                            break;
                        case Qt::LinkAction:
                            Fm::FileOperation::symlinkFiles(srcPaths, destPath);
                            break;
                        default:
                            break;
                        }
                    });
                    e->accept();
                    return;
                }
            }
        }
    }
    Fm::FolderView::childDropEvent(e);
}

} // namespace PCManFM
