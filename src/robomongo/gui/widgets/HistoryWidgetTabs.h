#pragma once

#include <QWidget>

#include "robomongo/core/history/HistoryStore.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPoint;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
QT_END_NAMESPACE

namespace Robomongo
{
    class MainWindow;

    /**
     * @brief Content of the View > History dock - two tabs:
     *
     *  History   - executed commands (newest first) with an Action column:
     *              star (add to favorites), Paste, Run. Search, connection
     *              filter and the "Auto" toggle apply here.
     *  Favorites - manually curated commands with labels (per connection).
     *              Never trimmed or reordered automatically. Right-click for
     *              Rename / Remove.
     *
     *  Paste - replaces the active console input with the command.
     *  Run   - replaces the input and executes immediately.
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
        void onFavoriteDoubleClicked(QTreeWidgetItem *item, int column);
        void onHistoryContextMenu(QPoint const &pos);
        void onFavoritesContextMenu(QPoint const &pos);
        void onTabChanged(int index);

    private:
        void refreshConnections();
        void repopulateHistory();
        void repopulateFavorites();
        void toggleFavorite(Robomongo::HistoryEntry const &entry, bool starChecked,
                            QToolButton *starButton);
        bool applyEntry(QString const &command, bool run);
        void showEntryView(QString const &command, QString const &connection,
                           QString const &database, bool fromFavorites);

        MainWindow *_mainWindow;
        QLineEdit *_search;
        QComboBox *_connectionCombo;
        QCheckBox *_autoCheck;
        QTabWidget *_tabs;
        QTreeWidget *_historyTree;
        QTreeWidget *_favoritesTree;
        bool _refreshPending = false;
    };
}
