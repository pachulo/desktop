#include "webflowcredentials.h"

#include "creds/httpcredentials.h"

#include <QAuthenticator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <keychain.h>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>

#include "accessmanager.h"
#include "account.h"
#include "theme.h"
#include "wizard/webview.h"
#include "webflowcredentialsdialog.h"

using namespace QKeychain;

namespace OCC {

Q_LOGGING_CATEGORY(lcWebFlowCredentials, "sync.credentials.webflow", QtInfoMsg)

class WebFlowCredentialsAccessManager : public AccessManager
{
public:
    WebFlowCredentialsAccessManager(const WebFlowCredentials *cred, QObject *parent = nullptr)
        : AccessManager(parent)
        , _cred(cred)
    {
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *outgoingData) Q_DECL_OVERRIDE
    {
        QNetworkRequest req(request);
        if (!req.attribute(HttpCredentials::DontAddCredentialsAttribute).toBool()) {
            if (_cred && !_cred->password().isEmpty()) {
                QByteArray credHash = QByteArray(_cred->user().toUtf8() + ":" + _cred->password().toUtf8()).toBase64();
                req.setRawHeader("Authorization", "Basic " + credHash);
            }
        }

        return AccessManager::createRequest(op, req, outgoingData);
    }

private:
    // The credentials object dies along with the account, while the QNAM might
    // outlive both.
    QPointer<const WebFlowCredentials> _cred;
};

WebFlowCredentials::WebFlowCredentials()
    : _ready(false)
    , _credentialsValid(false)
    , _keychainMigration(false)
{

}

WebFlowCredentials::WebFlowCredentials(const QString &user, const QString &password, const QSslCertificate &certificate, const QSslKey &key)
    : _user(user)
    , _password(password)
    , _clientSslKey(key)
    , _clientSslCertificate(certificate)
    , _ready(true)
    , _credentialsValid(true)
    , _keychainMigration(false)
{

}

QString WebFlowCredentials::authType() const {
    return QString::fromLatin1("webflow");
}

QString WebFlowCredentials::user() const {
    return _user;
}

QString WebFlowCredentials::password() const {
    return _password;
}

QNetworkAccessManager *WebFlowCredentials::createQNAM() const {
    qCInfo(lcWebFlowCredentials()) << "Get QNAM";
    AccessManager *qnam = new WebFlowCredentialsAccessManager(this);

    connect(qnam, &AccessManager::authenticationRequired, this, &WebFlowCredentials::slotAuthentication);
    connect(qnam, &AccessManager::finished, this, &WebFlowCredentials::slotFinished);

    return qnam;
}

bool WebFlowCredentials::ready() const {
    return _ready;
}

void WebFlowCredentials::fetchFromKeychain() {
    _wasFetched = true;

    // Make sure we get the user fromt he config file
    fetchUser();

    if (ready()) {
        emit fetched();
    } else {
        qCInfo(lcWebFlowCredentials()) << "Fetch from keyhchain!";
        fetchFromKeychainHelper();
    }
}

void WebFlowCredentials::askFromUser() {
    _askDialog = new WebFlowCredentialsDialog();

    QUrl url = _account->url();
    QString path = url.path() + "/index.php/login/flow";
    url.setPath(path);
    _askDialog->setUrl(url);

    QString msg = tr("You have been logged out of %1 as user %2. Please login again")
            .arg(_account->displayName(), _user);
    _askDialog->setInfo(msg);

    _askDialog->show();

    connect(_askDialog, &WebFlowCredentialsDialog::urlCatched, this, &WebFlowCredentials::slotAskFromUserCredentialsProvided);

    qCWarning(lcWebFlowCredentials()) << "User needs to reauth!";
}

void WebFlowCredentials::slotAskFromUserCredentialsProvided(const QString &user, const QString &pass, const QString &host) {
    Q_UNUSED(host);

    if (_user != user) {
        qCInfo(lcWebFlowCredentials()) << "Authed with the wrong user!";

        QString msg = tr("Please login with the user: %1")
                .arg(_user);
        _askDialog->setError(msg);

        QUrl url = _account->url();
        QString path = url.path() + "/index.php/login/flow";
        url.setPath(path);
        _askDialog->setUrl(url);

        return;
    }

    qCInfo(lcWebFlowCredentials()) << "Obtained a new password";

    _password = pass;
    _ready = true;
    _credentialsValid = true;
    persist();
    emit asked();

    _askDialog->close();
    delete _askDialog;
    _askDialog = NULL;
}


bool WebFlowCredentials::stillValid(QNetworkReply *reply) {
    qCWarning(lcWebFlowCredentials()) << "Still valid?";
    qCWarning(lcWebFlowCredentials()) << reply->error();
    qCWarning(lcWebFlowCredentials()) << reply->errorString();
    return (reply->error() != QNetworkReply::AuthenticationRequiredError);
}

void WebFlowCredentials::persist() {
    if (_user.isEmpty()) {
        // We don't even have a user nothing to see here move along
        return;
    }

    _account->setCredentialSetting("user", _user);
    _account->wantsAccountSaved(_account);

    //TODO: Add ssl cert and key storing
    WritePasswordJob *job = new WritePasswordJob(Theme::instance()->appName());
    job->setInsecureFallback(false);
    job->setKey(keychainKey(_account->url().toString(), _user, _account->id()));
    job->setTextData(_password);
    job->start();
}

void WebFlowCredentials::invalidateToken() {
    // clear the session cookie.
    _account->clearCookieJar();

    // let QNAM forget about the password
    // This needs to be done later in the event loop because we might be called (directly or
    // indirectly) from QNetworkAccessManagerPrivate::authenticationRequired, which itself
    // is a called from a BlockingQueuedConnection from the Qt HTTP thread. And clearing the
    // cache needs to synchronize again with the HTTP thread.
    QTimer::singleShot(0, _account, &Account::clearQNAMCache);
}

void WebFlowCredentials::forgetSensitiveData(){
    _password = QString();
    _ready = false;

    fetchUser();

    const QString kck = keychainKey(_account->url().toString(), _user, _account->id());
    if (kck.isEmpty()) {
        qCWarning(lcWebFlowCredentials()) << "InvalidateToken: User is empty, bailing out!";
        return;
    }

    DeletePasswordJob *job = new DeletePasswordJob(Theme::instance()->appName());
    job->setInsecureFallback(false);
    job->setKey(kck);
    job->start();

    invalidateToken();
}

void WebFlowCredentials::setAccount(Account *account) {
    AbstractCredentials::setAccount(account);
    if (_user.isEmpty()) {
        fetchUser();
    }
}

QString WebFlowCredentials::fetchUser() {
    _user = _account->credentialSetting("user").toString();
    return _user;
}

void WebFlowCredentials::slotAuthentication(QNetworkReply *reply, QAuthenticator *authenticator) {
    Q_UNUSED(reply);

    if (!_ready) {
        return;
    }

    if (_credentialsValid == false) {
        return;
    }

    qCWarning(lcWebFlowCredentials()) << "Requires authentication";

    authenticator->setUser(_user);
    authenticator->setPassword(_password);
    _credentialsValid = false;
}

void WebFlowCredentials::slotFinished(QNetworkReply *reply) {
    qCInfo(lcWebFlowCredentials()) << "request finished";

    if (reply->error() == QNetworkReply::NoError) {
        _credentialsValid = true;
    }
}

void WebFlowCredentials::fetchFromKeychainHelper() {
    const QString kck = keychainKey(
        _account->url().toString(),
        _user,
        _keychainMigration ? QString() : _account->id());

    ReadPasswordJob *job = new ReadPasswordJob(Theme::instance()->appName());
    job->setInsecureFallback(false);
    job->setKey(kck);
    connect(job, &Job::finished, this, &WebFlowCredentials::slotReadPasswordJobDone);
    job->start();
}

void WebFlowCredentials::slotReadPasswordJobDone(Job *incomingJob) {
    QKeychain::ReadPasswordJob *job = static_cast<ReadPasswordJob *>(incomingJob);
    QKeychain::Error error = job->error();

    // If we could not find the entry try the old entries
    if (!_keychainMigration && error == QKeychain::EntryNotFound) {
        _keychainMigration = true;
        fetchFromKeychainHelper();
        return;
    }

    if (error == QKeychain::NoError) {
        _password = job->textData();
        _ready = true;
        _credentialsValid = true;
    } else {
        _ready = false;
    }
    emit fetched();

    // If keychain data was read from legacy location, wipe these entries and store new ones
    if (_keychainMigration && _ready) {
        _keychainMigration = false;
        persist();
        deleteOldKeychainEntries();
        qCWarning(lcWebFlowCredentials) << "Migrated old keychain entries";
    }
}

void WebFlowCredentials::deleteOldKeychainEntries() {
    DeletePasswordJob *job = new DeletePasswordJob(Theme::instance()->appName());
    job->setInsecureFallback(false);
    job->setKey(keychainKey(_account->url().toString(), _user, QString()));
    job->start();
}

}
