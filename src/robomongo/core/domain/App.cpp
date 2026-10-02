#include "robomongo/core/domain/App.h"
#include "robomongo/core/history/HistoryStore.h"

#include <unistd.h>

#include <fstream>
#include <mutex>

namespace {
// Temporary debug trail for the RS-over-SSH feature (remove after debugging).
inline void rbDebug(const std::string& line)
{
    // Opt-in diagnostics: silent unless /tmp/romo_debug.log exists (touch it to enable).
    if (::access("/tmp/romo_debug.log", F_OK) != 0)
        return;
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    std::ofstream f("/tmp/romo_debug.log", std::ios::app);
    f << line << std::endl;
}
}

#include <QHash>
#include <QInputDialog>
#include <QMessageBox>

#include "robomongo/core/domain/MongoServer.h"
#include "robomongo/core/domain/MongoShell.h"
#include "robomongo/core/domain/MongoCollection.h"
#include "robomongo/core/settings/ConnectionSettings.h"
#include "robomongo/core/settings/ReplicaSetSettings.h"
#include "robomongo/core/settings/SshSettings.h"
#include "robomongo/core/settings/SslSettings.h"
#include "robomongo/core/mongodb/SshTunnelWorker.h"
#include "robomongo/core/EventBus.h"
#include "robomongo/core/utils/QtUtils.h"
#include "robomongo/core/utils/StdUtils.h"
#include "robomongo/core/utils/Logger.h"

namespace Robomongo
{
    namespace detail
    {
        QString buildCollectionQuery(const std::string &collectionName, const QString &postfix)
        {
            QString qCollectionName = QtUtils::toQString(collectionName);

            QString pattern;

            // Use db.getCollection() to avoid having to enumerate and special case "reserved" names
            pattern = "db.getCollection(\'%1\').%2";

            // Escape '\' symbol
            qCollectionName.replace(QChar('\\'), "\\\\");

            return pattern.arg(qCollectionName).arg(postfix);
        }
    }

    App::~App()
    {}

    App::App(EventBus *const bus) : QObject(),
        _bus(bus), _lastServerHandle(0) {
        _bus->subscribe(this, EstablishSshConnectionResponse::Type);
        _bus->subscribe(this, ListenSshConnectionResponse::Type);
        _bus->subscribe(this, LogEvent::Type);
    }

    std::unique_ptr<MongoServer>
    App::continueOpenServer(int serverHandle, ConnectionSettings* connSettings, 
                            ConnectionType type, int localport)
    {
        ConnectionSettings* connSettingsClone = connSettings->clone();

        // Modify connection settings when SSH tunnel is used
        if ((type == ConnectionPrimary || type == ConnectionTest)
            && !connSettingsClone->isReplicaSet()
            && connSettingsClone->sshSettings()->enabled()
        ) {
            connSettingsClone->setServerHost("127.0.0.1");
            connSettingsClone->setServerPort(localport);
        }

        auto server { std::make_unique<MongoServer>(serverHandle, connSettingsClone, type) };
        server->runWorkerThread();

        auto replicaSetStr = QString::fromStdString(connSettings->connectionName()) + " [Replica Set]";
        replicaSetStr = (connSettings->replicaSetSettings()->members().size() > 0)
            ? replicaSetStr + QString::fromStdString(connSettings->replicaSetSettings()->members()[0])
            : replicaSetStr + "";

        QString serverAddress = connSettings->isReplicaSet()
            ? replicaSetStr
            : QString::fromStdString(connSettings->getFullAddress());

        LOG_MSG(QString("Connecting to %1...").arg(serverAddress), mongo::logger::LogSeverity::Info());
        server->tryConnect();
        return server;
    }

    /**
    * Creates and opens new server connection.
    * @param connection: ConnectionSettings, that will be owned by MongoServer.
    * @param visible: should this server be visible in UI (explorer) or not.
    */
    std::unique_ptr<MongoServer> 
    App::openServerInternal(ConnectionSettings* connSettings, ConnectionType type) 
    {
        ++_lastServerHandle;

        {
            std::string membersInfo;
            if (connSettings->isReplicaSet())
                for (auto const& m : connSettings->replicaSetSettings()->members())
                    membersInfo += m + ";";
            rbDebug("openServerInternal handle=" + std::to_string(_lastServerHandle) +
                    " type=" + std::to_string(static_cast<int>(type)) +
                    " sshEnabled=" + std::to_string(connSettings->sshSettings()->enabled()) +
                    " isRS=" + std::to_string(connSettings->isReplicaSet()) +
                    " members=[" + membersInfo + "]");
        }

        if (type == ConnectionPrimary)
            _bus->publish(new ConnectingEvent(this));

        // No SSH tunnel needed: secondary connections, or SSH disabled
        if (type == ConnectionSecondary || !connSettings->sshSettings()->enabled()) {
            rbDebug("branch=no-tunnel handle=" + std::to_string(_lastServerHandle));
            return continueOpenServer(_lastServerHandle, connSettings, type);
        }

        // Replica set over SSH: open one tunnel per member (member addresses are
        // usually not directly routable from this machine), register their local
        // endpoints in the connection settings, then continue once all are up.
        if (connSettings->isReplicaSet()) {
            auto const& members = connSettings->replicaSetSettings()->members();
            if (members.empty())  // UI validates this; safety net
                return continueOpenServer(_lastServerHandle, connSettings, type);

            LOG_MSG(QString("Creating SSH tunnels for %1 replica set member(s)...")
                .arg(members.size()), mongo::logger::LogSeverity::Info());

            PendingMultiSsh pending;
            pending.settings = connSettings;
            pending.type = type;
            pending.remaining = static_cast<int>(members.size());

            for (auto const& member : members) {
                auto const sepPos = member.find_last_of(':');
                std::string const host = (sepPos == std::string::npos)
                    ? member : member.substr(0, sepPos);
                int const port = (sepPos == std::string::npos)
                    ? 27017 : std::atoi(member.c_str() + sepPos + 1);

                ConnectionSettings* memberSettings = connSettings->clone();
                memberSettings->setServerHost(host);
                memberSettings->setServerPort(port);

                auto* sshWorker = new SshTunnelWorker(memberSettings);
                _pendingSshMemberByWorker[sshWorker] = member;
                _bus->send(sshWorker, new EstablishSshConnectionRequest(
                    this, _lastServerHandle, sshWorker, memberSettings, type));
            }

            _pendingMultiSsh[_lastServerHandle] = pending;
            rbDebug("multi-tunnel spawned handle=" + std::to_string(_lastServerHandle) +
                    " workers=" + std::to_string(_pendingSshMemberByWorker.size()));
            return nullptr;
        }

        // Open SSH channel and only after that open connection
        LOG_MSG(QString("Creating SSH tunnel to %1:%2...")
            .arg(QtUtils::toQString(connSettings->sshSettings()->host()))
            .arg(connSettings->sshSettings()->port()), mongo::logger::LogSeverity::Info());

        ConnectionSettings* settingsCopy = connSettings->clone();
        SshTunnelWorker* sshWorker = new SshTunnelWorker(settingsCopy);
        _bus->send(sshWorker, new EstablishSshConnectionRequest(this, _lastServerHandle, sshWorker, settingsCopy, type));
        return nullptr;
    }

    bool App::openServer(ConnectionSettings *connection, ConnectionType type) 
    {
        SshSettings *ssh = connection->sshSettings();

        if (ssh->enabled() && ssh->askPassword() &&
            (type == ConnectionPrimary || type == ConnectionTest)) {
            bool ok = false;

            bool isByKey = ssh->authMethod() == "publickey";
            std::string passText = isByKey ? "passphrase" : "password";

            std::stringstream s;
            s << "In order to continue, please provide the " << passText;

            if (isByKey)
                s << " for the key file";

            s << "." << std::endl << std::endl;

            if (ssh->authMethod() == "publickey")
                s << "Private Key:  " << ssh->privateKeyFile() << std::endl;

            s << "Server: " << ssh->host() << std::endl;
            s << "User: " << ssh->userName() << std::endl;


            s << std::endl << "Enter your " << passText << " that will never be stored:";

            QString userInput = QInputDialog::getText(NULL, tr("SSH Authentication"),
                QtUtils::toQString(s.str()),
                QLineEdit::Password, "", &ok);

            if (!ok)
                return false;

            ssh->setAskedPassword(QtUtils::toStdString(userInput));
        }

        SslSettings *sslSettings = connection->sslSettings();

        if (sslSettings->sslEnabled() && sslSettings->usePemFile() && sslSettings->askPassphrase() 
            && (type == ConnectionPrimary || type == ConnectionTest)) 
        {
            if (!askSslPassphrasePromptDialog(connection))
            {
                return false;
            }
        }

        _servers.push_back(move(openServerInternal(connection, type)));
        return true;
    }

    /**
     * @brief Closes MongoServer connection and frees all resources, owned
     * by MongoServer. Finally, specified MongoServer will also be deleted.
     */
    void App::closeServer(MongoServer *server)
    {
        _servers.erase(std::remove_if(_servers.begin(), _servers.end(), 
            [&](auto const& el) { return el.get() == server; }), _servers.end());
    }

    void App::openShell(MongoCollection *collection, const QString &filePathToSave)
    {
        ConnectionSettings *connection = collection->database()->server()->connectionRecord();
        auto const& dbname = collection->database()->name();
        connection->setDefaultDatabase(dbname);
        QString const& script = detail::buildCollectionQuery(collection->name(), "find({})");
        openShell(collection->database()->server(), connection, 
            ScriptInfo(script, true, dbname, CursorPosition(0, -2), QtUtils::toQString(dbname), filePathToSave)
        );
    }

    void App::openShell(MongoServer *server, const QString &script, const std::string &dbName,
                        bool execute, const QString &shellName,
                        const CursorPosition &cursorPosition, const QString &filePathToSave)
    {
        ConnectionSettings *connection = server->connectionRecord();

        if (!dbName.empty())
            connection->setDefaultDatabase(dbName);

        openShell(server, connection, 
            ScriptInfo(script, execute, dbName, cursorPosition, shellName, filePathToSave)
        );
    }

    void App::openShell(MongoDatabase *database, const QString &script,
                               bool execute, const QString &shellName,
                               const CursorPosition &cursorPosition, const QString &filePathToSave)
    {
        ConnectionSettings *connection = database->server()->connectionRecord();
        connection->setDefaultDatabase(database->name());
        openShell(database->server(), connection, ScriptInfo(script, execute, database->name(), 
                                                             cursorPosition, shellName, filePathToSave));
    }

    void App::openShell(MongoServer* server, ConnectionSettings* connection, const ScriptInfo &scriptInfo)
    {
        auto serverClone{ openServerInternal(connection, ConnectionSecondary) };
        if (!serverClone || !server)
            return;

        auto shell{ std::make_unique<MongoShell>(serverClone.get(), scriptInfo) };
        _servers.push_back(move(serverClone));
        // Connection between explorer's server and tab's MongoShells
        _bus->subscribe(server, ReplicaSetRefreshed::Type, shell.get()); 
        _bus->publish(new OpeningShellEvent(this, shell.get()));
        // Explorer-generated shell: record as an auto-generated history entry
        // (hidden by default in the View > History panel).
        HistoryStore::instance().add(
            QtUtils::toQString(connection->connectionName()),
            QtUtils::toQString(scriptInfo.dbname()),
            scriptInfo.script(), true);
        shell->execute();
        _shells.push_back(move(shell));
        return;
    }

    /**
     * @brief Closes MongoShell and frees all resources, owned by specified MongoShell.
     * Finally, specified MongoShell will also be deleted.
     */
    void App::closeShell(MongoShell *shell)
    {
        auto const itr = std::find_if(_shells.begin(), _shells.end(), 
            [&](auto const& el) { return el.get() == shell; }
        );
        
        // Do nothing, if this shell not owned by this App.
        if (itr == _shells.end())
            return;

        closeServer(shell->server());
        _shells.erase(itr);
    }

    void App::handle(EstablishSshConnectionResponse *event) {
        if (event->isError()) {
            _pendingMultiSsh.erase(event->serverHandle);
            _bus->publish(new ConnectionFailedEvent(
                this, event->serverHandle, event->connectionType, event->error().errorMessage(),
                ConnectionFailedEvent::SshConnection));
            return;
        }

        // Replica set flow: aggregate per-member tunnels
        auto pendingIt = _pendingMultiSsh.find(event->serverHandle);
        if (pendingIt != _pendingMultiSsh.end()) {
            auto memberIt = _pendingSshMemberByWorker.find(event->worker);
            if (memberIt != _pendingSshMemberByWorker.end()) {
                pendingIt->second.memberLocalPorts[memberIt->second] = event->localport;
                rbDebug("tunnel established member=" + memberIt->second +
                        " localport=" + std::to_string(event->localport) +
                        " remaining=" + std::to_string(pendingIt->second.remaining - 1));
                _pendingSshMemberByWorker.erase(memberIt);
            }
            _bus->send(event->worker, new ListenSshConnectionRequest(
                this, event->serverHandle, event->connectionType));

            if (--pendingIt->second.remaining > 0)
                return;

            // All member tunnels are up: register local endpoints and continue
            LOG_MSG(QString("SSH tunnels for all replica set members created successfully"),
                    mongo::logger::LogSeverity::Info());

            rbDebug("all tunnels up -> continueOpenServer handle=" + std::to_string(event->serverHandle));
            ConnectionSettings* mappedSettings = pendingIt->second.settings->clone();
            for (auto const& entry : pendingIt->second.memberLocalPorts)
                mappedSettings->addSshTunnelEndpoint(entry.first, "127.0.0.1", entry.second);

            int const serverHandle = event->serverHandle;
            ConnectionType const connectionType = event->connectionType;
            _pendingMultiSsh.erase(pendingIt);

            auto server = continueOpenServer(serverHandle, mappedSettings, connectionType);
            delete mappedSettings;  // continueOpenServer clones what it needs
            _servers.push_back(move(server));
            return;
        }

        // Orphaned member tunnel of an already failed replica set connection
        auto orphanIt = _pendingSshMemberByWorker.find(event->worker);
        if (orphanIt != _pendingSshMemberByWorker.end()) {
            _pendingSshMemberByWorker.erase(orphanIt);
            _bus->send(event->worker, new ListenSshConnectionRequest(
                this, event->serverHandle, event->connectionType));
            return;
        }

        LOG_MSG(QString("SSH tunnel created successfully"), mongo::logger::LogSeverity::Info());

        _servers.push_back(move(
            continueOpenServer(event->serverHandle, event->settings, event->connectionType, event->localport)
        ));
        _bus->send(event->worker, new ListenSshConnectionRequest(this, event->serverHandle, event->connectionType));
    }

    void App::handle(LogEvent *event) {
        LOG_MSG(event->message, event->mongoLogSeverity());

        if (!event->informUser)
            return;

        QMessageBox(
            event->qMessageBoxIcon(),
            QString::fromStdString(event->severity()),
            QtUtils::toQString(event->severity() + ": " + event->message)
        ).exec();
    }

    void App::handle(ListenSshConnectionResponse *event) {
        if (event->isError()) {
            _bus->publish(
                new ConnectionFailedEvent(this, event->serverHandle, event->connectionType, 
                    event->error().errorMessage(), ConnectionFailedEvent::SshChannel)
            );
            return;
        }

        LOG_MSG(QString("SSH tunnel closed."), mongo::logger::LogSeverity::Error());
    }

    void App::fireConnectionFailedEvent(int serverHandle, ConnectionType type, std::string errormsg,
                                        ConnectionFailedEvent::Reason reason) {
        _bus->publish(new ConnectionFailedEvent(this, serverHandle, type, errormsg, reason));
    }

    bool App::askSslPassphrasePromptDialog(ConnectionSettings *connSettings) const
    {
        auto sslSettings = connSettings->sslSettings();
        bool ok = false;

        std::stringstream s;
        s << "In order to continue, please provide the passphrase";
        s << "." << std::endl << std::endl;

        s << "Server: " << connSettings->serverHost() << ":" << connSettings->serverPort() << std::endl;
        s << "PEM file: " << sslSettings->pemKeyFile() << std::endl;

        s << std::endl << "Enter your PEM key passphrase (will never be stored):";

        QString userInput = QInputDialog::getText(NULL, tr("TLS Authentication"),
            QtUtils::toQString(s.str()),
            QLineEdit::Password, "", &ok);

        if (!ok)
        {
            return false;
        }

        sslSettings->setPemPassPhrase(QtUtils::toStdString(userInput));
        return ok;
    }

}
