#pragma once

#include <QWidget>

#include "robomongo/core/history/HistoryStore.h"

QT_BEGIN_NAMESPACE
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
     * @brief Content of the View > History dock - model C: two stacked
     *  panes in a vertical splitter.
     *
     *  Top (70%): executed commands - Command | Action (star, Paste, Run).
     *    Right-click: View / favorite management / Remove from History.
     *  Bottom (30%): favorites - Name | Command | Action (unstar, Paste, Run).
     *    Right-click: View / Rename / Remove from Favorites.
     *
     *  Shared filter row (search + connection) applies to both panes.
     *  Double-click pastes into the active console. The View dialog offers
     *  dynamic Favorit/Unfavorit + Paste/Run.
     *
     *  Earlier models are kept as snapshots (not built):
     *  HistoryWidgetA.{h,cpp} = model A (single integrated list),
     *  HistoryWidgetTabs.{h,cpp} = model B (tabbed).
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
        void onHistoryDoubleClicked(QTreeWidgetItem *item, int column);
        void onFavoritesDoubleClicked(QTreeWidgetItem *item, int column);
        void onHistoryContextMenu(QPoint const &pos);
        void onFavoritesContextMenu(QPoint const &pos);

    private:
        void refreshConnections();
        void repopulateHistory();
        void repopulateFavorites();
        void toggleFavorite(Robomongo::HistoryEntry const &entry, bool starChecked,
                            QToolButton *starButton);
        bool applyEntry(QString const &command, bool run);
        void showEntryView(QString const &command, QString const &connection,
                           QString const &database, bool editable);

        MainWindow *_mainWindow;
        QLineEdit *_search;
        QComboBox *_connectionCombo;
        QTreeWidget *_historyTree;
        QTreeWidget *_favoritesTree;
        bool _refreshPending = false;
    };
}
