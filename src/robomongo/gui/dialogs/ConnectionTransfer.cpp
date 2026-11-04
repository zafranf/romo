#include "robomongo/gui/dialogs/ConnectionTransfer.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStringList>
#include <QVariantMap>
#include <memory>

#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/QtUtils.h"

namespace
{
    QString const kConnectionsKey = QStringLiteral("connections");
}

namespace Robomongo
{
    namespace ConnectionTransfer
    {
        QString defaultExportPath()
        {
            return QDir::homePath() + QStringLiteral("/romo-connections.json");
        }

        void exportConnections(QWidget *parent, SettingsManager *settings)
        {
            QString const path = QFileDialog::getSaveFileName(
                parent, QObject::tr("Export Connections"), defaultExportPath(),
                QObject::tr("JSON files (*.json);;All files (*)"));
            if (path.isEmpty())
                return;

            QJsonArray array;
            for (ConnectionSettings const *connection : settings->connections())
                array.append(QJsonObject::fromVariantMap(connection->toVariant().toMap()));

            QJsonObject root;
            root.insert(QStringLiteral("application"), QStringLiteral("romo"));
            root.insert(QStringLiteral("version"), 1);
            root.insert(kConnectionsKey, array);

            QFile file(path);
            if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
                QMessageBox::warning(parent, QObject::tr("Export failed"),
                                     QObject::tr("Cannot write to %1.").arg(path));
                return;
            }
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();

            QMessageBox::information(parent, QObject::tr("Export Connections"),
                                     QObject::tr("Exported %1 connection(s) to\n%2")
                                         .arg(array.size()).arg(path));
        }

        std::vector<ConnectionSettings *> importConnections(QWidget *parent,
                                                            SettingsManager *settings)
        {
            std::vector<ConnectionSettings *> imported;

            QString const path = QFileDialog::getOpenFileName(
                parent, QObject::tr("Import Connections"), QDir::homePath(),
                QObject::tr("JSON files (*.json);;All files (*)"));
            if (path.isEmpty())
                return imported;

            QFile file(path);
            if (!file.open(QFile::ReadOnly)) {
                QMessageBox::warning(parent, QObject::tr("Import failed"),
                                     QObject::tr("Cannot read %1.").arg(path));
                return imported;
            }

            QJsonParseError error;
            QJsonDocument const doc = QJsonDocument::fromJson(file.readAll(), &error);
            file.close();
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                QMessageBox::warning(parent, QObject::tr("Import failed"),
                                     QObject::tr("%1 is not a Romo connections file.").arg(path));
                return imported;
            }

            QJsonArray const array = doc.object().value(kConnectionsKey).toArray();
            if (array.isEmpty()) {
                QMessageBox::warning(parent, QObject::tr("Import failed"),
                                     QObject::tr("No connections found in %1.").arg(path));
                return imported;
            }

            // Names identify a connection in the UI: keep them unique.
            QStringList existing;
            for (ConnectionSettings const *connection : settings->connections())
                existing << QtUtils::toQString(connection->connectionName());

            int skipped = 0;
            for (QJsonValue const &value : array) {
                if (!value.isObject()) {
                    ++skipped;
                    continue;
                }

                auto connection = std::make_unique<ConnectionSettings>(false);
                QVariantMap map = value.toObject().toVariantMap();
                // Fresh uuid: the file may come from another machine, where the
                // uuids would collide with the local connections.
                map.remove(QStringLiteral("uuid"));
                connection->fromVariant(map);

                QString name = QtUtils::toQString(connection->connectionName());
                if (name.isEmpty())
                    name = QObject::tr("Imported connection");
                QString unique = name;
                for (int n = 2; existing.contains(unique); ++n)
                    unique = QStringLiteral("%1 (%2)").arg(name).arg(n);
                connection->setConnectionName(QtUtils::toStdString(unique));
                connection->setImported(true);
                existing << unique;

                settings->addConnection(connection.get());
                imported.push_back(connection.release());
            }

            QMessageBox::information(
                parent, QObject::tr("Import Connections"),
                skipped > 0
                    ? QObject::tr("Imported %1 connection(s), skipped %2 invalid entr(ies).")
                          .arg(imported.size()).arg(skipped)
                    : QObject::tr("Imported %1 connection(s).").arg(imported.size()));

            return imported;
        }
    }
}
