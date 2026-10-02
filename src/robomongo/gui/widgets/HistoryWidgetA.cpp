#include "robomongo/gui/widgets/HistoryWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
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
        _search->setPlaceholderText(tr("Filter commands..."));
        _search->setClearButtonEnabled(true);

        _connectionCombo = new QComboBox;
        _connectionCombo->addItem(tr("All connections"), QString());

        _favoritesOnlyCheck = new QCheckBox;
        _favoritesOnlyCheck->setText(QString::fromUtf8("\xE2\x98\x85 ")
                                     + tr("Favorites only"));
        _favoritesOnlyCheck->setToolTip(tr("Show only favorited commands"));

        auto *filterRow = new QHBoxLayout;
        filterRow->setContentsMargins(4, 4, 4, 2);
        filterRow->setSpacing(4);
        filterRow->addWidget(_search, 1);
        filterRow->addWidget(_connectionCombo);
        filterRow->addWidget(_favoritesOnlyCheck);

        _tree = new QTreeWidget;
        _tree->setColumnCount(3);
        _tree->setHeaderLabels(QStringList()
            << tr("Name") << tr("Command") << tr("Action"));
        _tree->setRootIsDecorated(false);
        _tree->setSelectionMode(QAbstractItemView::SingleSelection);
        _tree->setUniformRowHeights(true);
        _tree->setAlternatingRowColors(true);
        _tree->setContextMenuPolicy(Qt::CustomContextMenu);
        _tree->header()->setStretchLastSection(false);
        _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        _tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        _tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

        auto *layout = new QVBoxLayout;
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        layout->addLayout(filterRow);
        layout->addWidget(_tree, 1);
        setLayout(layout);

        VERIFY(connect(_search, &QLineEdit::textChanged, this, &HistoryWidget::repopulate));
        VERIFY(connect(_connectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                       this, &HistoryWidget::repopulate));
        VERIFY(connect(_favoritesOnlyCheck, &QCheckBox::toggled, this, &HistoryWidget::repopulate));
        VERIFY(connect(_tree, &QTreeWidget::itemDoubleClicked, this,
                       &HistoryWidget::onItemDoubleClicked));
        VERIFY(connect(_tree, &QTreeWidget::customContextMenuRequested, this,
                       &HistoryWidget::onContextMenu));
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
        QString const filter = _search->text().trimmed();
        QString const connection = _connectionCombo->currentData().toString();
        bool const favoritesOnly = _favoritesOnlyCheck->isChecked();

        _tree->setUpdatesEnabled(false);
        _tree->clear();

        // Auto-generated entries stay hidden for now (the Auto checkbox is
        // out of the UI); see HistoryWidgetTabs / earlier revisions to re-add.
        for (HistoryEntry const &entry : HistoryStore::instance().entries()) {
            if (entry.autoGenerated)
                continue;
            if (!connection.isEmpty() && entry.connectionName != connection)
                continue;

            bool const favorited = FavoritesStore::instance().isFavorite(
                entry.connectionName, entry.command);
            QString const label = FavoritesStore::instance().labelFor(
                entry.connectionName, entry.command);

            if (favoritesOnly && !favorited)
                continue;
            if (!filter.isEmpty()
                    && !entry.command.contains(filter, Qt::CaseInsensitive)
                    && !label.contains(filter, Qt::CaseInsensitive))
                continue;

            auto *item = new QTreeWidgetItem;
            item->setText(0, favorited ? label : QString());
            item->setText(1, entry.command.simplified());
            item->setToolTip(1, entry.command + "\n\n"
                                + entry.connectionName
                                + (entry.databaseName.isEmpty()
                                       ? QString()
                                       : " / " + entry.databaseName)
                                + (entry.timestamp.isValid()
                                       ? "\n" + entry.timestamp.toString("yyyy-MM-dd HH:mm:ss")
                                       : QString()));
            item->setData(1, Qt::UserRole, entry.command);
            item->setData(1, Qt::UserRole + 1, entry.connectionName);
            item->setData(1, Qt::UserRole + 2, entry.databaseName);
            _tree->addTopLevelItem(item);

            auto *actions = new QWidget;
            auto *actionsLayout = new QHBoxLayout(actions);
            actionsLayout->setContentsMargins(2, 0, 2, 0);
            actionsLayout->setSpacing(2);

            auto *starButton = makeActionButton(
                QString::fromUtf8("\xE2\x98\x85"),
                favorited ? tr("Remove from favorites") : tr("Add to favorites"));
            starButton->setCheckable(true);
            starButton->setChecked(favorited);

            auto *pasteButton = makeActionButton(tr("Paste"),
                tr("Replace the console input with this command"));
            auto *runButton = makeActionButton(tr("Run"),
                tr("Put this command into the console and execute"));

            actionsLayout->addWidget(starButton);
            actionsLayout->addWidget(pasteButton);
            actionsLayout->addWidget(runButton);

            _tree->setItemWidget(item, 2, actions);

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

        _tree->setUpdatesEnabled(true);
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

    void HistoryWidget::onItemDoubleClicked(QTreeWidgetItem *item, int column)
    {
        if (column == 2)
            return;   // button clicks are handled by their own slots
        applyEntry(item->data(1, Qt::UserRole).toString(), false);
    }

    void HistoryWidget::onContextMenu(QPoint const &pos)
    {
        QTreeWidgetItem *item = _tree->itemAt(pos);
        if (!item)
            return;

        QString const command = item->data(1, Qt::UserRole).toString();
        QString const connection = item->data(1, Qt::UserRole + 1).toString();
        QString const database = item->data(1, Qt::UserRole + 2).toString();

        bool const favorited = FavoritesStore::instance().isFavorite(connection, command);

        QMenu menu(this);
        QAction *viewAction = menu.addAction(tr("View"));
        QAction *addOrRenameAction = favorited
            ? menu.addAction(tr("Rename Favorite..."))
            : menu.addAction(tr("Add to Favorites..."));
        QAction *removeFavoriteAction =
            favorited ? menu.addAction(tr("Remove from Favorites")) : nullptr;
        menu.addSeparator();
        QAction *removeHistoryAction = menu.addAction(tr("Remove from History"));

        QAction *chosen = menu.exec(_tree->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == viewAction) {
            showEntryView(command, connection, database);
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

    void HistoryWidget::showEntryView(QString const &command, QString const &connection,
                                      QString const &database)
    {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("History Entry"));
        dialog.resize(620, 420);

        auto *metaLabel = new QLabel(
            connection + (database.isEmpty() ? QString() : " / " + database));
        metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        auto *queryText = new QPlainTextEdit;
        queryText->setReadOnly(true);
        queryText->setPlainText(command);
        queryText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        queryText->setLineWrapMode(QPlainTextEdit::NoWrap);

        auto *favoriteButton = new QPushButton;
        auto refreshFavoriteButton = [this, favoriteButton, &connection, &command]() {
            bool const favorited =
                FavoritesStore::instance().isFavorite(connection, command);
            favoriteButton->setText(favorited ? tr("Unfavorit") : tr("Favorit"));
            favoriteButton->setToolTip(favorited
                ? tr("Remove this command from favorites")
                : tr("Add this command to favorites"));
        };
        refreshFavoriteButton();

        auto *pasteButton = new QPushButton(tr("Paste"));
        auto *runButton = new QPushButton(tr("Run"));
        auto *closeButton = new QPushButton(tr("Close"));

        auto *buttonRow = new QHBoxLayout;
        buttonRow->addWidget(favoriteButton);
        buttonRow->addStretch();
        buttonRow->addWidget(pasteButton);
        buttonRow->addWidget(runButton);
        buttonRow->addWidget(closeButton);

        auto *mainLayout = new QVBoxLayout(&dialog);
        mainLayout->addWidget(metaLabel);
        mainLayout->addWidget(queryText, 1);
        mainLayout->addLayout(buttonRow);

        VERIFY(connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept));
        VERIFY(connect(pasteButton, &QPushButton::clicked, &dialog, [this, command, &dialog]() {
            if (applyEntry(command, false))
                dialog.accept();
        }));
        VERIFY(connect(runButton, &QPushButton::clicked, &dialog, [this, command, &dialog]() {
            if (applyEntry(command, true))
                dialog.accept();
        }));
        VERIFY(connect(favoriteButton, &QPushButton::clicked, &dialog,
            [this, command, connection, database, &refreshFavoriteButton]() {
                if (FavoritesStore::instance().isFavorite(connection, command)) {
                    FavoritesStore::instance().remove(connection, command);
                    refreshFavoriteButton();
                    return;
                }
                HistoryEntry entry;
                entry.command = command;
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
