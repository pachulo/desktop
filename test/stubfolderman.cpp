// stub to prevent linker error
#include "accountmanager.h"

OCC::AccountManager *OCC::AccountManager::instance() { return static_cast<AccountManager *>(new QObject); }
void OCC::AccountManager::save(bool) { }
void OCC::AccountManager::saveAccountState(AccountState *) { }
void OCC::AccountManager::deleteAccount(AccountState *) { }
void OCC::AccountManager::accountRemoved(OCC::AccountState*) { }
OCC::AccountStatePtr OCC::AccountManager::account(const QString &){ return AccountStatePtr(); }
void OCC::AccountManager::removeAccountFolders(OCC::AccountState*) { }
const QMetaObject OCC::AccountManager::staticMetaObject = QObject::staticMetaObject;
