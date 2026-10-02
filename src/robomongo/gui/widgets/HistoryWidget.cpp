#include "robomongo/gui/widgets/HistoryWidget.h"

#include <QComboBox>
#include <QDialog>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "robomongo/core/history/FavoritesStore.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/gui/MainWindow.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/utils/common.h"

namespace Robomongo
{
    namespace
    {
        QToolButton *makeActionButton(QString const &text, QString const &tooltip)
        {
            auto *button = new QToolButton;
            button->setText(text);
            button->setAutoRaise(true);
            button->setToolTip(tooltip);
            return button;
        }

        QToolButton *makeStarButton(bool favorited)
        {
            auto *button = new QToolButton;
            // Outline star when not favorited, filled star when favorited;
            // :checked gets a gold treatment so the state is unmistakable.
            button->setText(favorited ? QString::fromUtf8("\xE2\x98\x85")     // filled
                                       : QString::fromUtf8("\xE2\x98\x86"));   // outline
            button->setAutoRaise(true);
            button->setCheckable(true);
            button->setChecked(favorited);
            button->setFixedWidth(26);
            button->setToolTip(favorited
                ? QObject::tr("Remove from favorites")
                : QObject::tr("Add to favorites"));
            // Only favorited stars get the gold treatment; a not-yet-favorited
            // star stays a plain auto-raise toolbutton (like Paste/Run).
            if (favorited) {
                button->setStyleSheet(
                    "QToolButton { font-size: 13px; }"
                    "QToolButton:checked { color: #E8A200; background: #FFF1C9;"
                    " border: 1px solid #E8A200; border-radius: 3px;"
                    " font-weight: bold; }");
            }
            return button;
        }

        QString favoriteNamePreview(HistoryEntry const &entry)
        {
            QString preview = entry.command.simplified();
            if (preview.size() > 60)
                preview = preview.left(60) + "...";
            return preview;
        }
    }

    HistoryWidget::HistoryWidget(MainWindow *mainWindow, QWidget *parent)
        : QWidget(parent), _mainWindow(mainWindow)
    {
        _search = new QLineEdit;
        _search->setClearButtonEnabled(true);

        _connectionCombo = new QComboBox;
        _connectionCombo->addItem(tr("All connections"), QString());

        auto *filterRow = new QHBoxLayout;
        filterRow->setContentsMargins(4, 4, 4, 2);
        filterRow->setSpacing(4);
        filterRow->addWidget(_connectionCombo);
        filterRow->addWidget(_search, 1);

        // Top pane: history (70%)
        _historyTree = new QTreeWidget;
        _historyTree->setColumnCount(2);
        _historyTree->setHeaderLabels(QStringList() << tr("Command") << tr("Action"));
        _historyTree->setRootIsDecorated(false);
        _historyTree->setSelectionMode(QAbstractItemView::SingleSelection);
        _historyTree->setUniformRowHeights(true);
        _historyTree->setAlternatingRowColors(true);
        _historyTree->setContextMenuPolicy(Qt::CustomContextMenu);
        _historyTree->header()->setStretchLastSection(false);
        _historyTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        _historyTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

        // Bottom pane: favorites (30%)
        _favoritesTree = new QTreeWidget;
        _favoritesTree->setColumnCount(3);
        _favoritesTree->setHeaderLabels(QStringList()
            << tr("Name") << tr("Command") << tr("Action"));
        _favoritesTree->setRootIsDecorated(false);
        _favoritesTree->setSelectionMode(QAbstractItemView::SingleSelection);
        _favoritesTree->setUniformRowHeights(true);
        _favoritesTree->setAlternatingRowColors(true);
        _favoritesTree->setContextMenuPolicy(Qt::CustomContextMenu);
        _favoritesTree->header()->setStretchLastSection(false);
        _favoritesTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        _favoritesTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        _favoritesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        auto *favoritesGroup = new QGroupBox(tr("Favorites"));
        favoritesGroup->setStyleSheet(
            "QGroupBox::title { subcontrol-origin: margin;"
            " subcontrol-position: center top; margin-top: 2px; }");
        auto *favoritesGroupLayout = new QVBoxLayout(favoritesGroup);
        favoritesGroupLayout->setContentsMargins(2, 2, 2, 2);
        favoritesGroupLayout->addWidget(_favoritesTree);

        auto *splitter = new QSplitter(Qt::Vertical);
        splitter->setChildrenCollapsible(false);
        splitter->addWidget(_historyTree);
        splitter->addWidget(favoritesGroup);
        splitter->setStretchFactor(0, 7);   // history ~70%
        splitter->setStretchFactor(1, 3);   // favorites ~30%
        splitter->setSizes(QList<int>() << 700 << 300);

        auto *layout = new QVBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        layout->addLayout(filterRow);
        layout->addWidget(splitter, 1);
        setLayout(layout);

        VERIFY(connect(_search, &QLineEdit::textChanged, this, &HistoryWidget::repopulate));
        VERIFY(connect(_connectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                       this, &HistoryWidget::repopulate));
        VERIFY(connect(_historyTree, &QTreeWidget::itemDoubleClicked, this,
                       &HistoryWidget::onHistoryDoubleClicked));
        VERIFY(connect(_historyTree, &QTreeWidget::customContextMenuRequested, this,
                       &HistoryWidget::onHistoryContextMenu));
        VERIFY(connect(_favoritesTree, &QTreeWidget::itemDoubleClicked, this,
                       &HistoryWidget::onFavoritesDoubleClicked));
        VERIFY(connect(_favoritesTree, &QTreeWidget::customContextMenuRequested, this,
                       &HistoryWidget::onFavoritesContextMenu));
        VERIFY(connect(&HistoryStore::instance(), &HistoryStore::changed,
                       this, &HistoryWidget::onHistoryChanged));
        VERIFY(connect(&FavoritesStore::instance(), &FavoritesStore::changed,
                       this, &HistoryWidget::onFavoritesChanged));

        refreshConnections();
        repopulate();
    }

    void HistoryWidget::refresh()
    {
        refreshConnections();
        repopulate();
    }

    void HistoryWidget::onHistoryChanged()
    {
        // Deferred: an entry can be added from inside a button click on this
        // very tree (Run -> execute -> changed); rebuilding the tree right
        // there would delete the sender button mid-slot.
        if (_refreshPending)
            return;
        _refreshPending = true;
        QTimer::singleShot(0, this, [this]() {
            _refreshPending = false;
            refreshConnections();
            repopulate();
        });
    }

    void HistoryWidget::onFavoritesChanged()
    {
        if (_refreshPending)
            return;
        _refreshPending = true;
        QTimer::singleShot(0, this, [this]() {
            _refreshPending = false;
            refreshConnections();
            repopulate();
        });
    }

    void HistoryWidget::refreshConnections()
    {
        QString const previous = _connectionCombo->currentData().toString();

        QSet<QString> names;
        for (HistoryEntry const &entry : HistoryStore::instance().entries())
            names.insert(entry.connectionName);
        for (FavoriteEntry const &entry : FavoritesStore::instance().entries())
            names.insert(entry.connectionName);
        QStringList sorted = names.values();
        sorted.sort();

        _connectionCombo->blockSignals(true);
        _connectionCombo->clear();
        _connectionCombo->addItem(tr("All connections"), QString());
        for (QString const &name : sorted)
            _connectionCombo->addItem(name, name);

        int const index = _connectionCombo->findData(previous);
        _connectionCombo->setCurrentIndex(index >= 0 ? index : 0);
        _connectionCombo->blockSignals(false);
    }

    void HistoryWidget::repopulate()
    {
        repopulateHistory();
        repopulateFavorites();
    }

    void HistoryWidget::repopulateHistory()
    {
        QString const filter = _search->text().trimmed();
        QString const connection = _connectionCombo->currentData().toString();

        _historyTree->setUpdatesEnabled(false);
        _historyTree->clear();

        // Auto-generated entries stay hidden (no Auto toggle in the UI).
        for (HistoryEntry const &entry : HistoryStore::instance().entries()) {
            if (entry.autoGenerated)
                continue;
            if (!connection.isEmpty() && entry.connectionName != connection)
                continue;

            bool const favorited = FavoritesStore::instance().isFavorite(
                entry.connectionName, entry.command);
            QString const label = FavoritesStore::instance().labelFor(
                entry.connectionName, entry.command);

            if (!filter.isEmpty()
                    && !entry.command.contains(filter, Qt::CaseInsensitive)
                    && !label.contains(filter, Qt::CaseInsensitive))
                continue;

            auto *item = new QTreeWidgetItem;
            item->setText(0, entry.command.simplified());
            item->setToolTip(0, entry.command + "\n\n"
                                + entry.connectionName
                                + (entry.databaseName.isEmpty()
                                       ? QString()
                                       : " / " + entry.databaseName)
                                + (entry.timestamp.isValid()
                                       ? "\n" + entry.timestamp.toString("yyyy-MM-dd HH:mm:ss")
                                       : QString())
                                + (favorited ? "\n★ " + label : QString()));
            item->setData(0, Qt::UserRole, entry.command);
            item->setData(0, Qt::UserRole + 1, entry.connectionName);
            item->setData(0, Qt::UserRole + 2, entry.databaseName);
            _historyTree->addTopLevelItem(item);

            auto *actions = new QWidget;
            auto *actionsLayout = new QHBoxLayout(actions);
            actionsLayout->setContentsMargins(2, 0, 2, 0);
            actionsLayout->setSpacing(2);

            auto *starButton = makeStarButton(favorited);

            auto *pasteButton = makeActionButton(tr("Paste"),
                tr("Replace the console input with this command"));
            auto *runButton = makeActionButton(tr("Run"),
                tr("Put this command into the console and execute"));

            actionsLayout->addWidget(starButton);
            actionsLayout->addWidget(pasteButton);
            actionsLayout->addWidget(runButton);

            _historyTree->setItemWidget(item, 1, actions);

            HistoryEntry const captured = entry;
            QToolButton *const capturedStar = starButton;
            VERIFY(connect(starButton, &QToolButton::clicked, this,
                [this, captured, capturedStar](bool checked) {
                    toggleFavorite(captured, checked, capturedStar);
                }));
            VERIFY(connect(pasteButton, &QToolButton::clicked, this,
                [this, captured](bool) { applyEntry(captured.command, false); }));
            VERIFY(connect(runButton, &QToolButton::clicked, this,
                [this, captured](bool) { applyEntry(captured.command, true); }));
        }

        _historyTree->setUpdatesEnabled(true);
    }

    void HistoryWidget::repopulateFavorites()
    {
        QString const filter = _search->text().trimmed();
        QString const connection = _connectionCombo->currentData().toString();

        _favoritesTree->setUpdatesEnabled(false);
        _favoritesTree->clear();

        for (FavoriteEntry const &entry : FavoritesStore::instance().entries()) {
            if (!connection.isEmpty() && entry.connectionName != connection)
                continue;
            if (!filter.isEmpty()
                    && !entry.command.contains(filter, Qt::CaseInsensitive)
                    && !entry.label.contains(filter, Qt::CaseInsensitive))
                continue;

            auto *item = new QTreeWidgetItem;
            item->setText(0, entry.label);
            item->setText(1, entry.command.simplified());
            item->setToolTip(0, entry.command + "\n\n" + entry.connectionName
                                + (entry.databaseName.isEmpty()
                                       ? QString()
                                       : " / " + entry.databaseName));
            item->setToolTip(1, entry.command);
            item->setData(1, Qt::UserRole, entry.command);
            item->setData(1, Qt::UserRole + 1, entry.connectionName);
            item->setData(1, Qt::UserRole + 2, entry.databaseName);
            _favoritesTree->addTopLevelItem(item);

            auto *actions = new QWidget;
            auto *actionsLayout = new QHBoxLayout(actions);
            actionsLayout->setContentsMargins(2, 0, 2, 0);
            actionsLayout->setSpacing(2);

            auto *starButton = makeStarButton(true);

            auto *pasteButton = makeActionButton(tr("Paste"),
                tr("Replace the console input with this command"));
            auto *runButton = makeActionButton(tr("Run"),
                tr("Put this command into the console and execute"));

            actionsLayout->addWidget(starButton);
            actionsLayout->addWidget(pasteButton);
            actionsLayout->addWidget(runButton);

            _favoritesTree->setItemWidget(item, 2, actions);

            FavoriteEntry const captured = entry;
            VERIFY(connect(starButton, &QToolButton::clicked, this,
                [this, captured](bool) {
                    FavoritesStore::instance().remove(
                        captured.connectionName, captured.command);
                }));
            VERIFY(connect(pasteButton, &QToolButton::clicked, this,
                [this, captured](bool) { applyEntry(captured.command, false); }));
            VERIFY(connect(runButton, &QToolButton::clicked, this,
                [this, captured](bool) { applyEntry(captured.command, true); }));
        }

        _favoritesTree->setUpdatesEnabled(true);
    }

    void HistoryWidget::toggleFavorite(HistoryEntry const &entry, bool starChecked,
                                       QToolButton *starButton)
    {
        if (!starChecked) {
            FavoritesStore::instance().remove(entry.connectionName, entry.command);
            return;
        }

        bool ok = false;
        QString const label = QInputDialog::getText(
            this, tr("Add to Favorites"), tr("Name:"), QLineEdit::Normal,
            favoriteNamePreview(entry), &ok);
        if (!ok || label.trimmed().isEmpty()) {
            if (starButton)
                starButton->setChecked(false);
            return;
        }

        FavoritesStore::instance().add(entry.connectionName, entry.databaseName,
                                       entry.command, label);
    }

    void HistoryWidget::onHistoryDoubleClicked(QTreeWidgetItem *item, int column)
    {
        if (column == 1)
            return;   // button clicks are handled by their own slots
        applyEntry(item->data(0, Qt::UserRole).toString(), false);
    }

    void HistoryWidget::onFavoritesDoubleClicked(QTreeWidgetItem *item, int column)
    {
        if (column == 2)
            return;
        applyEntry(item->data(1, Qt::UserRole).toString(), false);
    }

    void HistoryWidget::onHistoryContextMenu(QPoint const &pos)
    {
        QTreeWidgetItem *item = _historyTree->itemAt(pos);
        if (!item)
            return;

        QString const command = item->data(0, Qt::UserRole).toString();
        QString const connection = item->data(0, Qt::UserRole + 1).toString();
        QString const database = item->data(0, Qt::UserRole + 2).toString();

        bool const favorited = FavoritesStore::instance().isFavorite(connection, command);

        QMenu menu(this);
        // Favorites can be edited (they live in the favorites store); plain
        // history entries are a read-only log.
        QAction *viewAction = menu.addAction(favorited ? tr("View && Edit") : tr("View"));
        QAction *addOrRenameAction = favorited
            ? menu.addAction(tr("Rename Favorite..."))
            : menu.addAction(tr("Add to Favorites..."));
        QAction *removeFavoriteAction =
            favorited ? menu.addAction(tr("Remove from Favorites")) : nullptr;
        menu.addSeparator();
        QAction *removeHistoryAction = menu.addAction(tr("Remove from History"));

        QAction *chosen = menu.exec(_historyTree->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == viewAction) {
            showEntryView(command, connection, database, favorited);
            return;
        }
        if (chosen == removeHistoryAction) {
            HistoryStore::instance().remove(connection, command);
            return;
        }
        if (removeFavoriteAction && chosen == removeFavoriteAction) {
            FavoritesStore::instance().remove(connection, command);
            return;
        }
        if (chosen == addOrRenameAction) {
            if (favorited) {
                bool ok = false;
                QString const label = QInputDialog::getText(
                    this, tr("Rename Favorite"), tr("Name:"), QLineEdit::Normal,
                    FavoritesStore::instance().labelFor(connection, command), &ok);
                if (ok && !label.trimmed().isEmpty())
                    FavoritesStore::instance().rename(connection, command, label);
            }
            else {
                HistoryEntry entry;
                entry.command = command;
                entry.connectionName = connection;
                entry.databaseName = database;
                toggleFavorite(entry, true, nullptr);
            }
            return;
        }
    }

    void HistoryWidget::onFavoritesContextMenu(QPoint const &pos)
    {
        QTreeWidgetItem *item = _favoritesTree->itemAt(pos);
        if (!item)
            return;

        QString const command = item->data(1, Qt::UserRole).toString();
        QString const connection = item->data(1, Qt::UserRole + 1).toString();
        QString const database = item->data(1, Qt::UserRole + 2).toString();

        QMenu menu(this);
        QAction *viewAction = menu.addAction(tr("View && Edit"));
        QAction *renameAction = menu.addAction(tr("Rename..."));
        menu.addSeparator();
        QAction *removeAction = menu.addAction(tr("Remove from Favorites"));

        QAction *chosen = menu.exec(_favoritesTree->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == viewAction) {
            showEntryView(command, connection, database, true);
            return;
        }
        if (chosen == removeAction) {
            FavoritesStore::instance().remove(connection, command);
            return;
        }
        if (chosen == renameAction) {
            bool ok = false;
            QString const label = QInputDialog::getText(
                this, tr("Rename Favorite"), tr("Name:"), QLineEdit::Normal,
                FavoritesStore::instance().labelFor(connection, command), &ok);
            if (ok && !label.trimmed().isEmpty())
                FavoritesStore::instance().rename(connection, command, label);
            return;
        }
    }

    void HistoryWidget::showEntryView(QString const &command, QString const &connection,
                                      QString const &database, bool editable)
    {
        // Live copy: after Save every lookup below follows the edited text.
        QString currentCommand = command;

        QDialog dialog(this);
        dialog.setWindowTitle(editable ? tr("Favorite") : tr("History Entry"));
        dialog.resize(620, 420);

        auto *metaLabel = new QLabel(
            connection + (database.isEmpty() ? QString() : " / " + database));
        metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto *queryText = new QPlainTextEdit;
        queryText->setReadOnly(!editable);
        queryText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        queryText->setLineWrapMode(QPlainTextEdit::NoWrap);
        queryText->setPlainText(currentCommand);

        auto *favoriteButton = new QPushButton;
        auto refreshFavoriteButton = [this, favoriteButton, &connection, &currentCommand]() {
            bool const favorited =
                FavoritesStore::instance().isFavorite(connection, currentCommand);
            favoriteButton->setText(favorited ? tr("Unfavorite") : tr("Favorite"));
            favoriteButton->setToolTip(favorited
                ? tr("Remove this command from favorites")
                : tr("Add this command to favorites"));
        };
        refreshFavoriteButton();

        auto *saveButton = new QPushButton(tr("Save"), &dialog);
        saveButton->setToolTip(tr("Save changes to this favorite"));

        // Wired AFTER the initial content set, so merely opening the dialog
        // doesn't count as an edit.
        VERIFY(connect(queryText, &QPlainTextEdit::textChanged, saveButton,
                       [saveButton]() { saveButton->setEnabled(true); }));
        saveButton->setEnabled(false);

        auto *pasteButton = new QPushButton(tr("Paste"));
        auto *runButton = new QPushButton(tr("Run"));
        auto *closeButton = new QPushButton(tr("Close"));

        auto *buttonRow = new QHBoxLayout;
        buttonRow->addWidget(favoriteButton);
        if (editable)
            buttonRow->addWidget(saveButton);
        buttonRow->addStretch();
        buttonRow->addWidget(pasteButton);
        buttonRow->addWidget(runButton);
        buttonRow->addWidget(closeButton);

        auto *mainLayout = new QVBoxLayout(&dialog);
        mainLayout->addWidget(metaLabel);
        mainLayout->addWidget(queryText, 1);
        mainLayout->addLayout(buttonRow);

        VERIFY(connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept));
        VERIFY(connect(pasteButton, &QPushButton::clicked, &dialog,
            [this, &currentCommand, &dialog]() {
                if (applyEntry(currentCommand, false))
                    dialog.accept();
            }));
        VERIFY(connect(runButton, &QPushButton::clicked, &dialog,
            [this, &currentCommand, &dialog]() {
                if (applyEntry(currentCommand, true))
                    dialog.accept();
            }));
        VERIFY(connect(saveButton, &QPushButton::clicked, &dialog,
            [this, &connection, &currentCommand, queryText, saveButton,
             &refreshFavoriteButton]() {
                QString const newText = queryText->toPlainText();
                FavoritesStore::instance().updateCommand(connection, currentCommand, newText);
                if (FavoritesStore::instance().isFavorite(connection, newText.trimmed())) {
                    currentCommand = newText.trimmed();
                    saveButton->setEnabled(false);
                    refreshFavoriteButton();
                }
            }));
        VERIFY(connect(favoriteButton, &QPushButton::clicked, &dialog,
            [this, &connection, &database, &currentCommand, &refreshFavoriteButton]() {
                if (FavoritesStore::instance().isFavorite(connection, currentCommand)) {
                    FavoritesStore::instance().remove(connection, currentCommand);
                    refreshFavoriteButton();
                    return;
                }
                HistoryEntry entry;
                entry.command = currentCommand;
                entry.connectionName = connection;
                entry.databaseName = database;
                toggleFavorite(entry, true, nullptr);
                refreshFavoriteButton();
            }));

        dialog.exec();
    }

    bool HistoryWidget::applyEntry(QString const &command, bool run)
    {
        if (command.isEmpty())
            return false;

        QueryWidget *queryWidget = _mainWindow ? _mainWindow->activeQueryWidget() : nullptr;
        if (!queryWidget) {
            if (_mainWindow && _mainWindow->statusBar())
                _mainWindow->statusBar()->showMessage(
                    tr("Open a console tab first (File > New Shell)."), 4000);
            return false;
        }

        queryWidget->insertFromHistory(command);
        if (run)
            queryWidget->execute();
        return true;
    }
}
