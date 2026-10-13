#include "robomongo/gui/widgets/HistoryWidget.h"

#include <QComboBox>
#include <QDialog>
#include <QEvent>
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
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QStringList>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "robomongo/core/history/FavoritesStore.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/gui/GuiRegistry.h"
#include "robomongo/gui/MainWindow.h"
#include "robomongo/gui/widgets/workarea/QueryWidget.h"
#include "robomongo/utils/common.h"

namespace Robomongo
{
    namespace
    {
        // Path separator between connection and db/collection in labels and
        // tooltips. Matches the thin disclosure chevron the explorer tree
        // draws as its branch indicator, so the two panes read the same way.
        QString const kCaretSeparator =
            QString::fromUtf8(" \xE2\x9D\xAF ");     // " ❯ "

        /**
         * @brief Keeps a child widget pinned to a corner of its parent.
         *
         * Watches the parent for resize/show and repositions the child. Used
         * for the prev/next arrows floating in the query textarea's top-right
         * corner: a plain layout can't overlay, and the parent has no
         * resizeEvent of its own to hook.
         */
        class ResizePin : public QObject
        {
        public:
            ResizePin(QWidget *overlay, QWidget *host, Qt::Alignment align,
                      QPoint const &margin, QObject *parent = nullptr)
                : QObject(parent), _overlay(overlay), _align(align),
                  _margin(margin)
            {
                host->installEventFilter(this);
                repin(host);
            }

        protected:
            bool eventFilter(QObject *watched, QEvent *event) override
            {
                if (event->type() == QEvent::Resize
                        || event->type() == QEvent::Show) {
                    repin(static_cast<QWidget *>(watched));
                }
                return QObject::eventFilter(watched, event);
            }

        private:
            void repin(QWidget *host)
            {
                int x = _margin.x();
                int y = _margin.y();
                if (_align & Qt::AlignRight)
                    x = host->width() - _overlay->width() + _margin.x();
                if (_align & Qt::AlignBottom)
                    y = host->height() - _overlay->height() + _margin.y();
                _overlay->move(x, y);
            }

            QWidget *_overlay;
            Qt::Alignment _align;
            QPoint _margin;
        };

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

        /**
         * @brief Name prompt for add/rename favorite.
         *
         * QInputDialog::getText gives no handle for styling, and its default
         * width clips the "Rename Favorite" title. The stylesheet also gives
         * the input the same bordered look as the View dialog's Name field.
         */
        QString askFavoriteName(QWidget *parent, QString const &title,
                                QString const &label, QString const &initial,
                                bool *ok)
        {
            QInputDialog dialog(parent);
            dialog.setWindowTitle(title);
            dialog.setInputMode(QInputDialog::TextInput);
            dialog.setTextEchoMode(QLineEdit::Normal);
            dialog.setLabelText(label);
            dialog.setTextValue(initial);
            // setMinimumWidth() is silently reset by exec() on this Qt build
            // (dialog snaps back to ~200px and the title gets clipped);
            // resize() before exec() sticks.
            dialog.setMinimumWidth(400);
            dialog.resize(420, dialog.sizeHint().height());
            dialog.setStyleSheet(
                "QLineEdit { border: 1px solid #a7a7a7; border-radius: 2px;"
                " padding: 2px 4px; }");
            *ok = dialog.exec() == QDialog::Accepted;
            return dialog.textValue();
        }

        // Index of the ')' matching the '(' at text[openPos], or -1 when the
        // group never closes. String literals are skipped so a ')' inside a
        // value doesn't end the group early.
        int matchingParen(QString const &text, int openPos)
        {
            int depth = 0;
            QChar quote;
            bool escaped = false;
            for (int i = openPos; i < text.size(); ++i) {
                QChar const c = text.at(i);
                if (!quote.isNull()) {
                    if (escaped)
                        escaped = false;
                    else if (c == '\\')
                        escaped = true;
                    else if (c == quote)
                        quote = QChar();
                    continue;
                }
                if (c == '\'' || c == '"') { quote = c; continue; }
                if (c == '(')
                    ++depth;
                else if (c == ')' && --depth == 0)
                    return i;
            }
            return -1;
        }

        // All top-level arguments inside the (...) group [openPos, closePos],
        // split on commas that sit at brace/bracket/paren depth zero.
        QStringList splitArguments(QString const &text, int openPos, int closePos)
        {
            int depth = 0;
            QChar quote;
            bool escaped = false;
            QStringList out;
            int start = openPos + 1;
            for (int i = openPos + 1; i < closePos; ++i) {
                QChar const c = text.at(i);
                if (!quote.isNull()) {
                    if (escaped)
                        escaped = false;
                    else if (c == '\\')
                        escaped = true;
                    else if (c == quote)
                        quote = QChar();
                    continue;
                }
                if (c == '\'' || c == '"') { quote = c; continue; }
                if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
                if (c == ')' || c == ']' || c == '}') { --depth; continue; }
                if (c == ',' && depth == 0) {
                    out << text.mid(start, i - start).trimmed();
                    start = i + 1;
                }
            }
            out << text.mid(start, closePos - start).trimmed();
            return out;
        }

        // Drops whitespace outside string literals so a pretty-printed filter
        // collapses onto one line. A space survives only between two word
        // characters, so "{a: 1}" becomes "{a:1}" but identifiers never merge.
        QString minifyJsonish(QString const &text)
        {
            QString out;
            out.reserve(text.size());
            QChar quote;
            bool escaped = false;
            for (int i = 0; i < text.size(); ++i) {
                QChar const c = text.at(i);
                if (!quote.isNull()) {
                    out += c;
                    if (escaped)
                        escaped = false;
                    else if (c == '\\')
                        escaped = true;
                    else if (c == quote)
                        quote = QChar();
                    continue;
                }
                if (c == '\'' || c == '"') { quote = c; out += c; continue; }
                if (c.isSpace()) {
                    int j = i;
                    while (j < text.size() && text.at(j).isSpace())
                        ++j;
                    QChar const prev = out.isEmpty() ? QChar() : out.at(out.size() - 1);
                    QChar const next = j < text.size() ? text.at(j) : QChar();
                    auto const isWord = [](QChar ch) {
                        return ch.isLetterOrNumber() || ch == '_' || ch == '$';
                    };
                    if (!prev.isNull() && !next.isNull() && isWord(prev) && isWord(next))
                        out += ' ';
                    i = j - 1;
                    continue;
                }
                out += c;
            }
            return out;
        }

        // Head/tail view for anything the chain walker can't shape: a lone
        // helper call, use('db'), raw JS, or a query with a broken string.
        QString truncatedPreview(QString const &command)
        {
            QString const preview = command.simplified();
            if (preview.size() <= 65)
                return preview;
            return preview.left(40) + QString::fromUtf8("\xE2\x80\xA6")   // "…"
                   + preview.right(22);
        }

        /**
         * @brief Compact "coll, <parts>" line for the command column.
         *
         * db.getCollection('activities').find({\n    project:1\n}) renders as
         * "activities, {project:1}". Once a second part shows up - projection,
         * sort spec, or both - every part is named so the reader can tell them
         * apart: "activities, find:{project:1}, project:{user:1},
         * sort:{created_at:-1}". A lone projection or sort is named too.
         * A trailing op rides along as a bare method name: "activities, {},
         * count".
         *
         * The walker reads whatever chain follows the collection, so .sort()
         * .count() .limit() and friends are picked up wherever they sit. Commands outside the
         * db.<coll>.<method>(...) shape - db.hostInfo(), use('db'), a query
         * whose string never closes - fall back to the plain preview. The
         * tooltip still carries the raw command text.
         */
        QString commandPreview(QString const &command)
        {
            QString const text = command.trimmed();

            // db.getCollection('X') or db.X
            static QRegularExpression const collRe(
                QStringLiteral(R"(^db\.(?:getCollection\s*\(\s*(['"])([^'"]*)\1\s*\)|([A-Za-z_$][\w$]*)))"));
            QRegularExpressionMatch const collMatch = collRe.match(text);
            if (!collMatch.hasMatch())
                return truncatedPreview(command);

            QString const coll = collMatch.captured(2).isEmpty()
                                     ? collMatch.captured(3)
                                     : collMatch.captured(2);

            // Walk the whole .<method>(...) chain after the collection.
            QString filter, projection, sortSpec;
            QStringList tail;              // count, limit, skip, ... after the find
            bool known = false;
            int i = collMatch.capturedEnd();
            for (;;) {
                while (i < text.size() && text.at(i).isSpace())
                    ++i;
                if (i >= text.size() || text.at(i) != '.')
                    break;
                ++i;
                int const nameStart = i;
                while (i < text.size() && (text.at(i).isLetterOrNumber()
                                           || text.at(i) == '_' || text.at(i) == '$'))
                    ++i;
                QString const method = text.mid(nameStart, i - nameStart);
                while (i < text.size() && text.at(i).isSpace())
                    ++i;
                if (i >= text.size() || text.at(i) != '(')
                    break;
                int const close = matchingParen(text, i);
                if (close < 0)
                    break;                       // unterminated: not ours
                QStringList const args = splitArguments(text, i, close);
                i = close + 1;

                if (method == QLatin1String("find")
                    || method == QLatin1String("findOne")) {
                    filter = args.value(0);
                    projection = args.value(1);   // absent -> empty QString
                    known = true;
                }
                else if (method == QLatin1String("aggregate")) {
                    filter = args.value(0);       // the pipeline, minified
                    known = true;
                }
                else if (method == QLatin1String("sort")) {
                    sortSpec = args.value(0);
                    known = true;
                }
                else if (method == QLatin1String("count")
                         || method == QLatin1String("countDocuments")) {
                    // "find({}).count()" tallies the rows the find would return
                    // - it brings no filter of its own, and overwriting the one
                    // already collected would erase it. Only when nothing
                    // filtered ahead of it does its argument count as a filter.
                    if (filter.isEmpty() && projection.isEmpty())
                        filter = args.value(0);
                    tail << method;
                    known = true;
                }
                else if (method == QLatin1String("limit")
                         || method == QLatin1String("skip")
                         || method == QLatin1String("batchSize")
                         || method == QLatin1String("distinct")
                         || method == QLatin1String("hint")
                         || method == QLatin1String("max")
                         || method == QLatin1String("min")) {
                    QString const arg = minifyJsonish(args.value(0));
                    tail << (arg.isEmpty() ? method : method + ':' + arg);
                    known = true;
                }
                else {
                    known = false;                // not our shape
                    break;
                }
            }

            if (!known)
                return truncatedPreview(command);

            QStringList labels, values;
            if (!filter.isEmpty()) {
                labels << QStringLiteral("find");
                values << minifyJsonish(filter);
            }
            if (!projection.isEmpty()) {
                labels << QStringLiteral("project");
                values << minifyJsonish(projection);
            }
            if (!sortSpec.isEmpty()) {
                labels << QStringLiteral("sort");
                values << minifyJsonish(sortSpec);
            }

            if (values.isEmpty() && tail.isEmpty())
                return coll;                      // count() with no filter

            // "find({},{b:1})" carries no filter worth naming - drop the {} so
            // the projection stands alone. Kept when a later op is all that
            // follows: "activities, {}, count" says the count was unfiltered.
            if (values.size() > 1 && values.first() == QLatin1String("{}")
                && (labels.contains(QLatin1String("project"))
                    || labels.contains(QLatin1String("sort")))) {
                labels.removeAt(0);
                values.removeAt(0);
            }

            // A lone find filter stays unlabelled; anything else is named so a
            // projection is never mistaken for the filter itself. A trailing op
            // is a bare method name, so it never forces the filter to be named.
            bool const named = values.size() > 1
                               || (values.size() == 1
                                   && labels.first() != QLatin1String("find"));
            QStringList rendered;
            for (int k = 0; k < values.size(); ++k)
                rendered << (named ? labels.at(k) + ':' + values.at(k)
                                   : values.at(k));
            rendered << tail;
            return coll + ", " + rendered.join(QStringLiteral(", "));
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
        _favoritesTree->setColumnCount(2);
        _favoritesTree->setHeaderLabels(QStringList()
            << tr("Name") << tr("Action"));
        _favoritesTree->setRootIsDecorated(false);
        _favoritesTree->setSelectionMode(QAbstractItemView::SingleSelection);
        _favoritesTree->setUniformRowHeights(true);
        _favoritesTree->setAlternatingRowColors(true);
        _favoritesTree->setContextMenuPolicy(Qt::CustomContextMenu);
        _favoritesTree->header()->setStretchLastSection(false);
        _favoritesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        _favoritesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

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
            item->setText(0, commandPreview(entry.command));
            item->setToolTip(0, entry.command + "\n\n"
                                + entry.connectionName
                                + (entry.databaseName.isEmpty()
                                       ? QString()
                                       : kCaretSeparator + entry.databaseName)
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
            item->setToolTip(0, entry.command + "\n\n" + entry.connectionName
                                + (entry.databaseName.isEmpty()
                                       ? QString()
                                       : kCaretSeparator + entry.databaseName));
            item->setData(0, Qt::UserRole, entry.command);
            item->setData(0, Qt::UserRole + 1, entry.connectionName);
            item->setData(0, Qt::UserRole + 2, entry.databaseName);
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

            _favoritesTree->setItemWidget(item, 1, actions);

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
        QString const label = askFavoriteName(
            this, tr("Add to Favorites"), tr("Name:"),
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
        if (column == 1)
            return;   // button clicks are handled by their own slots
        applyEntry(item->data(0, Qt::UserRole).toString(), false);
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
                QString const label = askFavoriteName(
                    this, tr("Rename Favorite"), tr("Name:"),
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

        QString const command = item->data(0, Qt::UserRole).toString();
        QString const connection = item->data(0, Qt::UserRole + 1).toString();
        QString const database = item->data(0, Qt::UserRole + 2).toString();

        QMenu menu(this);
        QAction *viewAction = menu.addAction(tr("View && Edit"));
        QAction *renameAction = menu.addAction(tr("Rename..."));
        menu.addSeparator();
        QAction *removeAction = menu.addAction(tr("Remove from Favorites"));

        QAction *chosen = menu.exec(_favoritesTree->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;

        if (chosen == viewAction) {
            showEntryView(command, connection, database);
            return;
        }
        if (chosen == removeAction) {
            FavoritesStore::instance().remove(connection, command);
            return;
        }
        if (chosen == renameAction) {
            bool ok = false;
            QString const label = askFavoriteName(
                this, tr("Rename Favorite"), tr("Name:"),
                FavoritesStore::instance().labelFor(connection, command), &ok);
            if (ok && !label.trimmed().isEmpty())
                FavoritesStore::instance().rename(connection, command, label);
            return;
        }
    }

    void HistoryWidget::showEntryView(QString const &command, QString const &connection,
                                      QString const &database)
    {
        // Live state: edits are applied when the dialog closes or when the
        // entry changes - there is no Save button.
        QString currentConnection = connection;
        QString currentDatabase = database;
        QString loadedCommand = command;        // identity of the entry on screen

        // Rows of the pane this entry was opened from (top = newest). Captured
        // once at open; re-matched by item data on every hop, because both
        // panes are rebuilt whenever history/favorites change.
        struct EntryRef { QString connection; QString database; QString command; };
        QList<EntryRef> navList;
        int navIndex = -1;
        int dataColumn = 0;
        QTreeWidget *navTree = nullptr;
        QTreeWidget *const panes[2] = { _historyTree, _favoritesTree };
        for (QTreeWidget *pane : panes) {
            // Both panes keep the entry payload in column 0.
            int const col = 0;
            for (int i = 0; i < pane->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = pane->topLevelItem(i);
                if (item->data(col, Qt::UserRole).toString() == command
                        && item->data(col, Qt::UserRole + 1).toString() == connection) {
                    navTree = pane;
                    dataColumn = col;
                    navIndex = i;
                    break;
                }
            }
            if (navTree)
                break;
        }
        if (navTree) {
            for (int i = 0; i < navTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = navTree->topLevelItem(i);
                navList.append({ item->data(dataColumn, Qt::UserRole + 1).toString(),
                                 item->data(dataColumn, Qt::UserRole + 2).toString(),
                                 item->data(dataColumn, Qt::UserRole).toString() });
            }
        }

        QDialog dialog(this);
        dialog.resize(620, 420);

        auto *metaLabel = new QLabel;
        metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

        // Prev/next hop through the pane rows so the entry can be browsed
        // without closing and reopening the dialog. Style matches the paging
        // arrows in the result panel header (PagingWidget): flat QPushButton
        // with the shared left/right triangle icons, 24x24.
        auto *prevButton = new QPushButton;
        prevButton->setIcon(GuiRegistry::instance().leftIcon());
        prevButton->setFixedSize(24, 24);
        prevButton->setFlat(true);
        prevButton->setToolTip(tr("Previous entry (the row above in the list)"));
        auto *nextButton = new QPushButton;
        nextButton->setIcon(GuiRegistry::instance().rightIcon());
        nextButton->setFixedSize(24, 24);
        nextButton->setFlat(true);
        nextButton->setToolTip(tr("Next entry (the row below in the list)"));

        auto *metaRow = new QHBoxLayout;
        metaRow->setContentsMargins(0, 0, 0, 0);
        metaRow->addWidget(metaLabel, 1);

        // Favorite name: under the connection/db line, above the query text.
        // The Save button lives at the right end of this row (View & Edit only,
        // the row is hidden for plain history entries). Parented to the dialog
        // up front: refreshHeader() toggles visibility before the layout runs.
        auto *nameWidget = new QWidget(&dialog);
        auto *nameRow = new QHBoxLayout(nameWidget);
        nameRow->setContentsMargins(0, 0, 0, 0);
        auto *nameCaption = new QLabel(tr("Name:"));
        auto *nameEdit = new QLineEdit;
        nameEdit->setPlaceholderText(tr("Favorite name"));
        nameEdit->setStyleSheet(
            "QLineEdit { border: 1px solid #a7a7a7; border-radius: 2px;"
            " padding: 2px 4px; }");
        auto *saveButton = new QPushButton(tr("Save"));
        saveButton->setToolTip(tr("Save changes to this favorite"));
        // QPushButton is 32px vs the field's 24px, and its grey fill sits
        // inset inside that box, so it reads a couple of px low. Wrap it and
        // reserve space at the bottom only - that shifts the button up without
        // touching its chrome (a negative margin or a stylesheet makes Qt
        // redraw the button and wreck its native look).
        auto *saveWrap = new QWidget;
        auto *saveWrapLayout = new QVBoxLayout(saveWrap);
        saveWrapLayout->setContentsMargins(0, 0, 0, 4);
        saveWrapLayout->setSpacing(0);
        saveWrapLayout->addWidget(saveButton);
        nameRow->addWidget(nameCaption);
        nameRow->addWidget(nameEdit, 1);
        nameRow->addWidget(saveWrap);

        auto *queryText = new QPlainTextEdit;
        queryText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        queryText->setLineWrapMode(QPlainTextEdit::NoWrap);
        queryText->setPlainText(loadedCommand);

        auto *favoriteButton = new QPushButton;
        auto *pasteButton = new QPushButton(tr("Paste"));
        auto *runButton = new QPushButton(tr("Run"));
        auto *closeButton = new QPushButton(tr("Close"));

        // Arrows float in the textarea's top-right corner (opaque so text
        // scrolling underneath doesn't bleed through). Child of queryText, so
        // it tracks the text edit's geometry; kept top-right via an event
        // filter (cursorPositionChanged would only fire on caret moves, not
        // on resize).
        auto *navOverlay = new QWidget(queryText);
        navOverlay->setAutoFillBackground(true);
        auto *navRow = new QHBoxLayout(navOverlay);
        navRow->setContentsMargins(2, 2, 2, 2);
        navRow->setSpacing(0);
        navRow->addWidget(prevButton);
        navRow->addWidget(nextButton);
        navOverlay->installEventFilter(new ResizePin(
            navOverlay, queryText, Qt::AlignTop | Qt::AlignRight,
            QPoint(-4, 4), navOverlay));
        navOverlay->move(queryText->width() - navOverlay->width() - 4, 4);
        navOverlay->show();

        // Save is enabled only while the favorite has unsaved changes.
        auto updateSaveState = [&]() {
            bool const favorited = FavoritesStore::instance().isFavorite(
                currentConnection, loadedCommand);
            bool const commandDirty =
                queryText->toPlainText().trimmed() != loadedCommand;
            bool const nameDirty =
                nameEdit->text().trimmed()
                != FavoritesStore::instance().labelFor(currentConnection,
                                                       loadedCommand);
            saveButton->setEnabled(favorited && (commandDirty || nameDirty));
        };

        auto refreshHeader = [&]() {
            bool const favorited = FavoritesStore::instance().isFavorite(
                currentConnection, loadedCommand);
            metaLabel->setText(currentConnection
                               + (currentDatabase.isEmpty()
                                      ? QString() : kCaretSeparator + currentDatabase));
            dialog.setWindowTitle(favorited ? tr("Favorite") : tr("History Entry"));
            favoriteButton->setText(favorited ? tr("Remove from Favorites")
                                              : tr("Add to Favorites"));
            favoriteButton->setToolTip(favorited
                ? tr("Remove this command from favorites")
                : tr("Add this command to favorites"));
            nameWidget->setVisible(favorited);
            if (favorited)
                nameEdit->setText(FavoritesStore::instance().labelFor(
                    currentConnection, loadedCommand));
            queryText->setReadOnly(!favorited);
            prevButton->setEnabled(navIndex > 0);
            nextButton->setEnabled(navIndex >= 0 && navIndex + 1 < navList.size());
            updateSaveState();
        };
        refreshHeader();

        auto persistEdits = [&]() {
            if (!FavoritesStore::instance().isFavorite(currentConnection, loadedCommand))
                return;
            QString const newText = queryText->toPlainText().trimmed();
            if (newText.isEmpty() || newText == loadedCommand)
                return;
            FavoritesStore::instance().updateCommand(currentConnection,
                                                     loadedCommand, newText);
            loadedCommand = newText;
            if (navIndex >= 0 && navIndex < navList.size())
                navList[navIndex].command = newText;
        };

        auto persistName = [&]() {
            if (!FavoritesStore::instance().isFavorite(currentConnection, loadedCommand))
                return;
            QString const name = nameEdit->text().trimmed();
            if (name.isEmpty()
                    || name == FavoritesStore::instance().labelFor(
                                   currentConnection, loadedCommand))
                return;
            FavoritesStore::instance().rename(currentConnection, loadedCommand, name);
        };

        auto syncPaneSelection = [&]() {
            if (!navTree)
                return;
            for (int i = 0; i < navTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem *item = navTree->topLevelItem(i);
                if (item->data(dataColumn, Qt::UserRole).toString() == loadedCommand
                        && item->data(dataColumn, Qt::UserRole + 1).toString()
                               == currentConnection) {
                    navTree->setCurrentItem(item);
                    navTree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
                    return;
                }
            }
        };

        auto gotoEntry = [&](int index) {
            if (index < 0 || index >= navList.size() || index == navIndex)
                return;
            navIndex = index;      // pending edits are dropped, not saved
            EntryRef const ref = navList[index];
            currentConnection = ref.connection;
            currentDatabase = ref.database;
            loadedCommand = ref.command;
            queryText->setPlainText(ref.command);
            refreshHeader();
            syncPaneSelection();
        };

        auto *buttonRow = new QHBoxLayout;
        buttonRow->addWidget(favoriteButton);
        buttonRow->addStretch();
        buttonRow->addWidget(pasteButton);
        buttonRow->addSpacing(20);  // see metaRow: Qt eats 8px, 20 -> gap 12
        buttonRow->addWidget(runButton);
        buttonRow->addSpacing(20);
        buttonRow->addWidget(closeButton);

        auto *mainLayout = new QVBoxLayout(&dialog);
        // Tight vertical rhythm between the header rows and the query text;
        // outer margins stay at the style default (8px made the buttons
        // collide with the dialog edge).
        mainLayout->setSpacing(4);
        mainLayout->addLayout(metaRow);
        mainLayout->addWidget(nameWidget);
        mainLayout->addWidget(queryText, 1);
        mainLayout->addSpacing(4);
        mainLayout->addLayout(buttonRow);

        // No autosave anywhere: the Save button below is the only way to
        // commit. Closing, blurring the name field, or hopping to another
        // entry all discard pending edits.
        VERIFY(connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept));
        VERIFY(connect(pasteButton, &QPushButton::clicked, &dialog, [this, queryText, &dialog]() {
            if (applyEntry(queryText->toPlainText().trimmed(), false))
                dialog.accept();
        }));
        VERIFY(connect(runButton, &QPushButton::clicked, &dialog, [this, queryText, &dialog]() {
            if (applyEntry(queryText->toPlainText().trimmed(), true))
                dialog.accept();
        }));
        VERIFY(connect(favoriteButton, &QPushButton::clicked, &dialog, [&]() {
            if (FavoritesStore::instance().isFavorite(currentConnection, loadedCommand)) {
                FavoritesStore::instance().remove(currentConnection, loadedCommand);
                refreshHeader();
                return;
            }
            HistoryEntry entry;
            QString const text = queryText->toPlainText().trimmed();
            entry.command = text.isEmpty() ? loadedCommand : text;
            entry.connectionName = currentConnection;
            entry.databaseName = currentDatabase;
            toggleFavorite(entry, true, nullptr);   // prompts for the name
            if (FavoritesStore::instance().isFavorite(currentConnection, entry.command)) {
                loadedCommand = entry.command;
                if (navIndex >= 0 && navIndex < navList.size())
                    navList[navIndex].command = loadedCommand;
            }
            refreshHeader();
        }));
        VERIFY(connect(nameEdit, &QLineEdit::editingFinished, &dialog,
                       updateSaveState));   // no autosave - just refresh Save
        VERIFY(connect(saveButton, &QPushButton::clicked, &dialog, [&]() {
            persistName();
            persistEdits();
            updateSaveState();
        }));
        VERIFY(connect(queryText, &QPlainTextEdit::textChanged,
                       &dialog, updateSaveState));
        VERIFY(connect(nameEdit, &QLineEdit::textChanged,
                       &dialog, updateSaveState));
        VERIFY(connect(prevButton, &QPushButton::clicked, &dialog,
                       [&]() { gotoEntry(navIndex - 1); }));
        VERIFY(connect(nextButton, &QPushButton::clicked, &dialog,
                       [&]() { gotoEntry(navIndex + 1); }));

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
