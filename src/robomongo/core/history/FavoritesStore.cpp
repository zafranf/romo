#include "robomongo/core/history/FavoritesStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace Robomongo
{
    namespace
    {
        QString favoritesFilePath()
        {
            return QDir::homePath() + "/.romo/favorites.jsonl";
        }
    }

    FavoritesStore &FavoritesStore::instance()
    {
        static FavoritesStore store;
        return store;
    }

    FavoritesStore::FavoritesStore()
    {
        load();
    }

    int FavoritesStore::indexOf(QString const &connectionName, QString const &command) const
    {
        for (int i = 0; i < _entries.size(); ++i) {
            if (_entries[i].connectionName == connectionName
                    && _entries[i].command == command)
                return i;
        }
        return -1;
    }

    void FavoritesStore::add(QString const &connectionName, QString const &databaseName,
                             QString const &command, QString const &label)
    {
        if (connectionName.isEmpty() || command.trimmed().isEmpty())
            return;

        // Already favorited: keep the existing entry (including its label).
        if (indexOf(connectionName, command) >= 0)
            return;

        FavoriteEntry entry;
        entry.created = QDateTime::currentDateTime();
        entry.connectionName = connectionName;
        entry.databaseName = databaseName;
        entry.command = command;
        entry.label = label.trimmed().isEmpty() ? command.simplified() : label.trimmed();
        _entries.prepend(entry);

        save();
        Q_EMIT changed();
    }

    void FavoritesStore::remove(QString const &connectionName, QString const &command)
    {
        int const index = indexOf(connectionName, command);
        if (index < 0)
            return;
        _entries.removeAt(index);
        save();
        Q_EMIT changed();
    }

    void FavoritesStore::rename(QString const &connectionName, QString const &command,
                                QString const &label)
    {
        int const index = indexOf(connectionName, command);
        if (index < 0 || label.trimmed().isEmpty())
            return;
        _entries[index].label = label.trimmed();
        save();
        Q_EMIT changed();
    }

    void FavoritesStore::updateCommand(QString const &connectionName,
                                       QString const &oldCommand,
                                       QString const &newCommand)
    {
        QString const trimmed = newCommand.trimmed();
        if (trimmed.isEmpty() || trimmed == oldCommand)
            return;

        int index = indexOf(connectionName, oldCommand);
        if (index < 0)
            return;

        // Avoid ending up with two favorites for the same command.
        int const other = indexOf(connectionName, trimmed);
        if (other >= 0 && other != index) {
            _entries.removeAt(other);
            if (other < index)
                --index;
        }

        _entries[index].command = trimmed;
        save();
        Q_EMIT changed();
    }

    bool FavoritesStore::isFavorite(QString const &connectionName, QString const &command) const
    {
        return indexOf(connectionName, command) >= 0;
    }

    QString FavoritesStore::labelFor(QString const &connectionName,
                                     QString const &command) const
    {
        int const index = indexOf(connectionName, command);
        return index < 0 ? QString() : _entries[index].label;
    }

    QStringList FavoritesStore::connectionNames() const
    {
        QStringList result;
        QSet<QString> seen;
        for (FavoriteEntry const &entry : _entries) {
            if (seen.contains(entry.connectionName))
                continue;
            seen.insert(entry.connectionName);
            result.append(entry.connectionName);
        }
        return result;
    }

    void FavoritesStore::load()
    {
        QFile file(favoritesFilePath());
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return;

        QTextStream in(&file);
        while (!in.atEnd()) {
            QJsonParseError error;
            QJsonDocument const doc =
                QJsonDocument::fromJson(in.readLine().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject())
                continue;

            QJsonObject const obj = doc.object();
            FavoriteEntry entry;
            entry.created = QDateTime::fromString(obj.value("t").toString(), Qt::ISODate);
            entry.connectionName = obj.value("c").toString();
            entry.databaseName = obj.value("d").toString();
            entry.command = obj.value("q").toString();
            entry.label = obj.value("n").toString();
            if (entry.connectionName.isEmpty() || entry.command.isEmpty())
                continue;
            if (entry.label.isEmpty())
                entry.label = entry.command.simplified();
            _entries.append(entry);   // file is written oldest-first
        }

        std::reverse(_entries.begin(), _entries.end());
    }

    void FavoritesStore::save() const
    {
        QDir().mkpath(QDir::homePath() + "/.romo");

        QFile file(favoritesFilePath());
        if (!file.open(QFile::WriteOnly | QFile::Truncate | QFile::Text))
            return;

        QTextStream out(&file);
        for (auto it = _entries.crbegin(); it != _entries.crend(); ++it) {
            QJsonObject obj;
            obj.insert("t", it->created.toString(Qt::ISODate));
            obj.insert("c", it->connectionName);
            if (!it->databaseName.isEmpty())
                obj.insert("d", it->databaseName);
            obj.insert("q", it->command);
            obj.insert("n", it->label);
            out << QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)) << "\n";
        }
    }
}
