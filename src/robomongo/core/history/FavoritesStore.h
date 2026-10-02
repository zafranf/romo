#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace Robomongo
{
    /**
     * @brief A manually curated (favorited) command.
     */
    struct FavoriteEntry
    {
        QDateTime created;
        QString connectionName;
        QString databaseName;
        QString command;
        QString label;                 // user-visible name
    };

    /**
     * @brief Manually curated commands, stored in ~/.romo/favorites.jsonl.
     *
     * Separate from HistoryStore on purpose: favorites are user-curated and
     * never trimmed, deduplicated or re-ordered automatically. Entries are
     * unique per (connection, command); order is newest-first. GUI thread only.
     */
    class FavoritesStore : public QObject
    {
        Q_OBJECT

    public:
        static FavoritesStore &instance();

        /** Adds (or updates) a favorite; no-op if the command is already favorited. */
        void add(QString const &connectionName, QString const &databaseName,
                 QString const &command, QString const &label);
        void remove(QString const &connectionName, QString const &command);
        void rename(QString const &connectionName, QString const &command,
                    QString const &label);
        /** Replaces the command text of a favorite (label/connection kept). */
        void updateCommand(QString const &connectionName, QString const &oldCommand,
                           QString const &newCommand);

        bool isFavorite(QString const &connectionName, QString const &command) const;
        /** Label of a favorite, or null QString when not favorited. */
        QString labelFor(QString const &connectionName, QString const &command) const;
        QList<FavoriteEntry> const &entries() const { return _entries; }
        QStringList connectionNames() const;

    Q_SIGNALS:
        void changed();

    private:
        FavoritesStore();
        int indexOf(QString const &connectionName, QString const &command) const;
        void load();
        void save() const;

        QList<FavoriteEntry> _entries;    // newest first
    };
}
