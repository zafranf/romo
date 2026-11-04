#pragma once

#include <QString>
#include <vector>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Robomongo
{
    class ConnectionSettings;
    class SettingsManager;

    /**
     * @brief Import/export of the connection list as a JSON file.
     *
     * Shared by the Connections dialog (context menu) and the Welcome page, so
     * both write exactly the same format. Secrets stay encrypted with the
     * local machine key, i.e. the file is portable only as long as Romo's
     * encryption key is.
     */
    namespace ConnectionTransfer
    {
        /** Default file name offered by the export dialog. */
        QString defaultExportPath();

        /** Asks for a file and writes every connection to it. */
        void exportConnections(QWidget *parent, SettingsManager *settings);

        /**
         * @brief Asks for a file and appends its connections to the settings.
         *
         * Connection names are made unique (numeric suffix) and imported
         * entries get a fresh uuid, so a file produced on another machine can
         * be imported next to the local ones.
         *
         * @return Connections that were added (owned by SettingsManager).
         */
        std::vector<ConnectionSettings *> importConnections(QWidget *parent,
                                                            SettingsManager *settings);
    }
}
