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


#ifndef PCMANFM_FOLDERVIEW_H
#define PCMANFM_FOLDERVIEW_H

#include <libfm-qt6/folderview.h>
#include <libfm-qt6/core/filepath.h>
#include <QPointer>

class QRubberBand;


namespace Fm {
class FileMenu;
class FolderMenu;
}

namespace PCManFM {

class Settings;

class View : public Fm::FolderView {
    Q_OBJECT
public:

    explicit View(Fm::FolderView::ViewMode _mode = IconMode, QWidget* parent = nullptr);
    virtual ~View();

    void updateFromSettings(Settings& settings);

    QSize  getMargins() const {
        return Fm::FolderView::getMargins();
    }
    void setMargins(QSize size) {
        Fm::FolderView::setMargins(size);
    }

protected Q_SLOTS:
    void onNewWindow();
    void onNewTab();
    void onOpenInTerminal();
    void onSearch();

protected:
    virtual void onFileClicked(int type, const std::shared_ptr<const Fm::FileInfo>& fileInfo) override;
    virtual void prepareFileMenu(Fm::FileMenu* menu) override;
    virtual void prepareFolderMenu(Fm::FolderMenu* menu) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void childDragMoveEvent(QDragMoveEvent* event) override;
    void childDropEvent(QDropEvent* event) override;

private:
    void launchFiles(Fm::FileInfoList files, bool inNewTabs = false);
    void openFolderAndSelectFile(const std::shared_ptr<const Fm::FileInfo>& fileInfo, bool inNewTab = false);

    bool leftPressAfterName_ = false;
    bool marqueeActive_ = false;
    QPoint leftPressPoint_;
    int pressScrollX_ = 0;
    int pressScrollY_ = 0;
    Qt::KeyboardModifiers leftPressModifiers_ = Qt::NoModifier;
    QItemSelection savedSelection_;
    QPointer<QRubberBand> rubberBand_;

};

}
#endif // PCMANFM_FOLDERVIEW_H
