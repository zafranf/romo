#include "RoboCrypt.h"

#include "robomongo/core/utils/Logger.h"       

#include <cmath>
#include <iostream>
#include <random>

#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <QTextStream>

namespace Robomongo {

    long long RoboCrypt::_KEY = 0;
    std::vector<RoboCrypt::LogAndSeverity> RoboCrypt::_roboCryptLogs;

    void RoboCrypt::initKey() 
    {
        using MongoSeverity = mongo::logger::LogSeverity;
        auto addToRoboCryptLogs = [](std::string msg, MongoSeverity severity) {
            _roboCryptLogs.push_back({ msg, severity });
        };

        const auto KEY_FILE = QString("%1/.romo/romo.key").arg(QDir::homePath()).toStdString();
        // Migrate the key from the old Robo 3T location when needed: passwords saved
        // as "userPasswordEncrypted" in an imported config only decrypt with the
        // original key, so never generate a fresh one while the legacy key exists.
        const auto LEGACY_KEY_FILE = QString("%1/.3T/robo-3t/robo3t.key").arg(QDir::homePath()).toStdString();
        QString fileContent;
        QFileInfo const fileInfo{ QString::fromStdString(KEY_FILE) };
        QFileInfo const legacyFileInfo{ QString::fromStdString(LEGACY_KEY_FILE) };
        bool const useLegacyKey = !fileInfo.exists() && legacyFileInfo.exists() && legacyFileInfo.isFile();
        std::string const& sourceKeyFile = useLegacyKey ? LEGACY_KEY_FILE : KEY_FILE;
        QFileInfo const& sourceFileInfo = useLegacyKey ? legacyFileInfo : fileInfo;
        if (sourceFileInfo.exists() && sourceFileInfo.isFile()) {   // a) Read existing key from file
            QFile keyFile{ QString::fromStdString(sourceKeyFile) };
            if (!keyFile.open(QIODevice::ReadOnly))
                addToRoboCryptLogs("RoboCrypt: Failed to open key file: " + sourceKeyFile, MongoSeverity::Error());

            QTextStream in{ &keyFile };
            fileContent = in.readAll();
            if(fileContent.isEmpty())
                addToRoboCryptLogs("RoboCrypt: Key file is empty: " + sourceKeyFile, MongoSeverity::Error());

            _KEY = fileContent.toLongLong();

            if (useLegacyKey) {   // Persist the migrated key at the new location
                QFile migratedKeyFile{ QString::fromStdString(KEY_FILE) };
                if (migratedKeyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out{ &migratedKeyFile };
                    out << fileContent;
                    addToRoboCryptLogs("RoboCrypt: Migrated legacy key to " + KEY_FILE, MongoSeverity::Info());
                }
                else
                    addToRoboCryptLogs("RoboCrypt: Failed to migrate legacy key to " + KEY_FILE, MongoSeverity::Error());
            }
        }
        else {  // b) Generate a new key and save it into file
            addToRoboCryptLogs("RoboCrypt: No key found, generating a new key and saving it into file", 
                                MongoSeverity::Warning());
            // Generate a new key
            std::random_device randomDevice;
            std::mt19937_64 engine{ randomDevice() };
            std::uniform_int_distribution<long long int> dist{ std::llround(std::pow(2,61)), 
                                                               std::llround(std::pow(2,62)) };
            _KEY = dist(engine);
            // Save the key into file
            QFile file{ QString::fromStdString(KEY_FILE) };
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
                addToRoboCryptLogs("RoboCrypt: Failed to save the key into file: " + KEY_FILE, MongoSeverity::Error());
                            
            QTextStream out(&file);
            out << QString::number(_KEY);
        }
    }

}