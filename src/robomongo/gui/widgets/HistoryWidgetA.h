#pragma once

#include <QWidget>

#include "robomongo/core/history/HistoryStore.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPoint;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace Robomongo
{
    class MainWindow;

    /**
     * @brief Content of the View > History dock - a single integrated list
     *  (model A) of executed commands with favorites built in:
     *
     *  Columns: Name (favorite label) | Command | Action (star, Paste, Run).
     *  "Favorites only" filter shows just the starred entries; search and the
     *  connection filter apply on top of it. Right-click a row: View (modal
     *  with Favorit/Unfavorit + Paste/Run), favorite management, and
     *  Remove from History. Double-click pastes into the active console.
     *
     *  A tabbed variant (model B) is kept in HistoryWidgetTabs.{h,cpp}
     *  (not built - swap the files + CMake entry to re-enable it).
     */
    class HistoryWidget : public QWidget
    {
        Q_OBJECT

    public:
        explicit HistoryWidget(MainWindow *mainWindow, QWidget *parent = nullptr);

    public Q_SLOTS:
        void refresh();

    private Q_SLOTS:
        void repopulate();
        void onHistoryChanged();
        void onFavoritesChanged();
        void onItemDoubleClicked(QTreeWidgetItem *item, int column);
        void onContextMenu(QPoint const &pos);

    private:
        void refreshConnections();
        void toggleFavorite(Robomongo::HistoryEntry const &entry, bool starChecked,
                            QToolButton *starButton);
        bool applyEntry(QString const &command, bool run);
        void showEntryView(QString const &command, QString const &connection,
                           QString const &database);

        MainWindow *_mainWindow;
        QLineEdit *_search;
        QComboBox *_connectionCombo;
        QCheckBox *_favoritesOnlyCheck;
        QTreeWidget *_tree;
        bool _refreshPending = false;
    };
}
