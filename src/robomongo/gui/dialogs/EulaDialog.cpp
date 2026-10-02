#include "robomongo/gui/dialogs/EulaDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QSettings>
#include <QLabel>
#include <QTextBrowser>
#include <QLineEdit>
#include <QRadioButton>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDesktopWidget>
#include <QTimeZone>

#include "robomongo/core/AppRegistry.h"
#include "robomongo/core/settings/SettingsManager.h"
#include "robomongo/core/utils/Logger.h"
#include "robomongo/core/utils/QtUtils.h"

namespace Robomongo
{
    EulaDialog::EulaDialog(bool showFormPage, QWidget *parent)
        : QWizard(parent), _showFormPage(showFormPage)
    {
        setWindowTitle("License");

        // Romo: the Studio 3T signup form page is permanently disabled — it used
        // to POST name/email/phone/company/OS/timezone to https://rm-form.3t.io.
        // Nothing is ever sent anywhere now.
        _showFormPage = false;

        //// First page
        auto firstPage = new QWizardPage;

        auto agreeButton = new QRadioButton("I agree");
        VERIFY(connect(agreeButton, SIGNAL(clicked()),
            this, SLOT(on_agreeButton_clicked())));

        auto notAgreeButton = new QRadioButton("I don't agree");
        notAgreeButton->setChecked(true);
        VERIFY(connect(notAgreeButton, SIGNAL(clicked()),
            this, SLOT(on_notAgreeButton_clicked())));

        auto radioButtonsLay = new QHBoxLayout;
        radioButtonsLay->setAlignment(Qt::AlignHCenter);
        radioButtonsLay->setSpacing(30);
        radioButtonsLay->addWidget(agreeButton);
        radioButtonsLay->addWidget(notAgreeButton);

        auto textBrowser = new QTextBrowser;
        textBrowser->setOpenExternalLinks(true);
        textBrowser->setOpenLinks(true);
        QFile file(":gnu_gpl3_license.html");
        if (file.open(QFile::ReadOnly | QFile::Text))
            textBrowser->setText(file.readAll());

        auto hline = new QFrame();
        hline->setFrameShape(QFrame::HLine);
        hline->setFrameShadow(QFrame::Sunken);

        auto mainLayout1 = new QVBoxLayout();
        mainLayout1->addWidget(new QLabel("<h3>Romo — License Notice</h3>"));
        auto introLabel = new QLabel(
            "<b>Romo</b> is free software licensed under the <b>GNU General Public License v3</b> "
            "— a fork of <b>Robo 3T</b> (formerly Robomongo) by Studio 3T and Robomongo contributors. "
            "The full license text is below. This build bundles the MongoDB shell (SSPL), Qt (LGPL) "
            "and OpenSSL (Apache 2.0). Romo is provided 'as is' with no warranty, and it never "
            "sends any data anywhere.");
        introLabel->setWordWrap(true);
        mainLayout1->addWidget(introLabel);
        mainLayout1->addWidget(textBrowser);
        mainLayout1->addWidget(new QLabel(""));
        mainLayout1->addLayout(radioButtonsLay, Qt::AlignCenter);
        mainLayout1->addWidget(new QLabel(""));
        mainLayout1->addWidget(hline);

        firstPage->setLayout(mainLayout1);

        // Romo: the former second page (Studio 3T marketing signup form that
        // posted user data to https://rm-form.3t.io) has been removed.
        addPage(firstPage);

        //// Buttons
        setButtonText(QWizard::CustomButton1, tr("Back"));
        setButtonText(QWizard::CustomButton2, tr("Next"));
        setButtonText(QWizard::CustomButton3, tr("Finish"));

        VERIFY(connect(button(QWizard::CustomButton1), SIGNAL(clicked()), this, SLOT(on_back_clicked())));
        VERIFY(connect(button(QWizard::CustomButton2), SIGNAL(clicked()), this, SLOT(on_next_clicked())));
        VERIFY(connect(button(QWizard::CustomButton3), SIGNAL(clicked()), this, SLOT(on_finish_clicked())));

        setButtonLayout(QList<WizardButton>{ QWizard::Stretch, QWizard::CustomButton1, QWizard::CustomButton2,
                                             QWizard::CancelButton, QWizard::CustomButton3});

        button(QWizard::CustomButton1)->setDisabled(true);
        button(QWizard::CustomButton2)->setDisabled(true);
        button(QWizard::CustomButton2)->setHidden(!_showFormPage);
        button(QWizard::CustomButton3)->setDisabled(true);

        setWizardStyle(QWizard::ModernStyle);

        QSettings const settings("Romo", "Romo");
        if (settings.contains("EulaDialog/size")) {
            restoreWindowSettings();
        }
        else {
            auto const desktop = QApplication::desktop();
            auto const& mainScreenSize = desktop->availableGeometry(desktop->primaryScreen()).size();
            resize(mainScreenSize.width()*0.5, mainScreenSize.height()*0.6);
        }
    }

    void EulaDialog::accept()
    {
        saveWindowSettings();
        QDialog::accept();
    }

    void EulaDialog::reject()
    {
        saveWindowSettings();
        QDialog::reject();
    }

    void EulaDialog::closeEvent(QCloseEvent *event)
    {
        saveWindowSettings();
        QWidget::closeEvent(event);
    }

    void EulaDialog::on_agreeButton_clicked()
    {
        if(_showFormPage)
            button(QWizard::CustomButton2)->setEnabled(true);
        else
            button(QWizard::CustomButton3)->setEnabled(true);
    }

    void EulaDialog::on_notAgreeButton_clicked()
    {
        if (_showFormPage)
            button(QWizard::CustomButton2)->setEnabled(false);
        else
            button(QWizard::CustomButton3)->setEnabled(false);
    }

    void EulaDialog::on_next_clicked()
    {
        next();
        button(QWizard::CustomButton1)->setEnabled(true);
        button(QWizard::CustomButton2)->setEnabled(false);
        button(QWizard::CustomButton3)->setEnabled(true);
    }

    void EulaDialog::on_back_clicked()
    {
        back();
        button(QWizard::CustomButton1)->setEnabled(false);
        button(QWizard::CustomButton2)->setEnabled(true);
        button(QWizard::CustomButton3)->setEnabled(false);
    }

    void EulaDialog::on_finish_clicked()
    {
        accept();
    }

    void EulaDialog::postUserData() const
    {
        // Removed for Romo: this used to POST the form data to
        // https://rm-form.3t.io (Studio 3T's marketing endpoint).
        if (true)
            return;
        if (_emailEdit->text().isEmpty() || 
            AppRegistry::instance().settingsManager()->disableHttpsFeatures()
        )
            return;

        // OS string
#ifdef _WIN32
        QString const OS = "win";
#elif __APPLE__
        QString const OS = "osx";
#elif __linux__
        QString const OS = "linux";
#else
        QString const OS = "unknown";
#endif

        // Timezone string
        QDateTime now = QDateTime::currentDateTime();
        now.setOffsetFromUtc(now.offsetFromUtc());
        QString dateAndTimezone = now.toString(Qt::ISODate);
        QString const date = QDateTime::currentDateTime().toString(Qt::ISODate);
        QString const timezone = "UTC" + dateAndTimezone.remove(date);

        // Build post data and send
        QJsonObject jsonStr {
            { "email", _emailEdit->text() },
            { "firstName", _nameEdit->text() },
            { "lastName", _lastNameEdit->text() },
            { "phone", _phone->text() },
            { "company", _company->text() },
            { "os", OS },
            { "timezone", timezone }
        };      

        QJsonDocument jsonDoc(jsonStr);
        QUrlQuery postData(jsonDoc.toJson());
        postData = QUrlQuery("rd=" + postData.toString(QUrl::FullyEncoded).toUtf8());
      
        QNetworkRequest request(QUrl("https://rm-form.3t.io/"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

        auto networkManager = new QNetworkAccessManager;
        _reply = networkManager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
        debugLog("EulaDialog: Form posted");
    }

    void EulaDialog::saveWindowSettings() const
    {
        QSettings settings("Romo", "Romo");
        settings.setValue("EulaDialog/size", size());
    }

    void EulaDialog::restoreWindowSettings()
    {
        QSettings settings("Romo", "Romo");
        resize(settings.value("EulaDialog/size").toSize());
    }

}
