/*
 * Copyright 2016 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SynchronizationManager_p.h"
#include "SynchronizationPersistenceName.h"
#include <quentier/utility/Utility.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/Printable.h>
#include <QApplication>

#define EXPIRATION_TIMESTAMP_KEY QStringLiteral("ExpirationTimestamp")
#define LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX QStringLiteral("LinkedNotebookExpirationTimestamp_")
#define LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART QStringLiteral("_LinkedNotebookAuthToken_")
#define LINKED_NOTEBOOK_SHARD_ID_KEY_PART QStringLiteral("_LinkedNotebookShardId_")
#define READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB QStringLiteral("readLinkedNotebookAuthToken")
#define READ_LINKED_NOTEBOOK_SHARD_ID_JOB QStringLiteral("readLinkedNotebookShardId")
#define WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB QStringLiteral("writeLinkedNotebookAuthToken")
#define WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB QStringLiteral("writeLinkedNotebookShardId")
#define NOTE_STORE_URL_KEY QStringLiteral("NoteStoreUrl")
#define WEB_API_URL_PREFIX_KEY QStringLiteral("WebApiUrlPrefix")

#define LAST_SYNC_PARAMS_KEY_GROUP QStringLiteral("last_sync_params")
#define LAST_SYNC_UPDATE_COUNT_KEY QStringLiteral("last_sync_update_count")
#define LAST_SYNC_TIME_KEY         QStringLiteral("last_sync_time")
#define LAST_SYNC_LINKED_NOTEBOOKS_PARAMS QStringLiteral("last_sync_linked_notebooks_params")
#define LINKED_NOTEBOOK_GUID_KEY QStringLiteral("linked_notebook_guid")
#define LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY QStringLiteral("linked_notebook_last_update_count")
#define LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY QStringLiteral("linked_notebook_last_sync_time")

namespace quentier {

SynchronizationManagerPrivate::SynchronizationManagerPrivate(const QString & consumerKey, const QString & consumerSecret,
                                                             const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                                             IAuthenticationManager & authenticationManager) :
    m_consumerKey(consumerKey),
    m_consumerSecret(consumerSecret),
    m_host(host),
    m_maxSyncChunkEntries(50),
    m_lastUpdateCount(-1),
    m_lastSyncTime(-1),
    m_cachedLinkedNotebookLastUpdateCountByGuid(),
    m_cachedLinkedNotebookLastSyncTimeByGuid(),
    m_onceReadLastSyncParams(false),
    m_noteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore)),
    m_userStore(QSharedPointer<qevercloud::UserStore>(new qevercloud::UserStore(m_host))),
    m_authContext(AuthContext::Blank),
    m_launchSyncPostponeTimerId(-1),
    m_OAuthResult(),
    m_authenticationInProgress(false),
    m_remoteToLocalSyncManager(localStorageManagerAsync, m_host,
                               m_noteStore.getQecNoteStore(), m_userStore.getQecUserStore()),
    m_sendLocalChangesManager(localStorageManagerAsync, m_noteStore.getQecNoteStore()),
    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid(),
    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid(),
    m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth(),
    m_authenticateToLinkedNotebooksPostponeTimerId(-1),
    m_readAuthTokenJob(QApplication::applicationName() + QStringLiteral("_read_auth_token")),
    m_readShardIdJob(QApplication::applicationName() + QStringLiteral("_read_shard_id")),
    m_readingAuthToken(false),
    m_readingShardId(false),
    m_writeAuthTokenJob(QApplication::applicationName() + QStringLiteral("_write_auth_token")),
    m_writeShardIdJob(QApplication::applicationName() + QStringLiteral("_write_shard_id")),
    m_writingAuthToken(false),
    m_writingShardId(false),
    m_deleteAuthTokenJob(QApplication::applicationName() + QStringLiteral("_delete_auth_token")),
    m_deleteShardIdJob(QApplication::applicationName() + QStringLiteral("delete_shard_id")),
    m_deletingAuthToken(false),
    m_deletingShardId(false),
    m_lastRevokedAuthenticationUserId(-1),
    m_readLinkedNotebookAuthTokenJobsByGuid(),
    m_readLinkedNotebookShardIdJobsByGuid(),
    m_writeLinkedNotebookAuthTokenJobsByGuid(),
    m_writeLinkedNotebookShardIdJobsByGuid(),
    m_linkedNotebookGuidsWithoutLocalAuthData(),
    m_shouldRepeatIncrementalSyncAfterSendingChanges(false),
    m_paused(false),
    m_remoteToLocalSyncWasActiveOnLastPause(false)
{
    m_OAuthResult.m_userId = -1;

    m_readAuthTokenJob.setAutoDelete(false);
    m_readShardIdJob.setAutoDelete(false);
    m_writeAuthTokenJob.setAutoDelete(false);
    m_writeShardIdJob.setAutoDelete(false);
    m_deleteAuthTokenJob.setAutoDelete(false);
    m_deleteShardIdJob.setAutoDelete(false);

    createConnections(authenticationManager);
}

SynchronizationManagerPrivate::~SynchronizationManagerPrivate()
{}

bool SynchronizationManagerPrivate::active() const
{
    return m_remoteToLocalSyncManager.active() || m_sendLocalChangesManager.active();
}

bool SynchronizationManagerPrivate::paused() const
{
    return m_paused;
}

bool SynchronizationManagerPrivate::downloadNoteThumbnailsOption() const
{
    return m_remoteToLocalSyncManager.shouldDownloadThumbnailsForNotes();
}

QString SynchronizationManagerPrivate::noteThumbnailsStoragePath() const
{
    return m_remoteToLocalSyncManager.noteThumbnailsStoragePath();
}

void SynchronizationManagerPrivate::setAccount(const Account & account)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::setAccount: ") << account);

    Account currentAccount = m_remoteToLocalSyncManager.account();
    if (currentAccount == account) {
        QNDEBUG(QStringLiteral("The same account is already set, nothing to do"));
        return;
    }

    clear();

    m_OAuthResult = AuthData();
    m_OAuthResult.m_userId = -1;

    if (account.type() == Account::Type::Local) {
        return;
    }

    m_OAuthResult.m_userId = account.id();
    m_remoteToLocalSyncManager.setAccount(account);
    // NOTE: send local changes manager doesn't have any use for the account
}

void SynchronizationManagerPrivate::synchronize()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::synchronize"));

    if (m_authenticationInProgress || m_writingAuthToken || m_writingShardId) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Authentication is not finished yet, please wait"));
        QNDEBUG(error << QStringLiteral(", authentication in progress = ")
                << (m_authenticationInProgress ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing OAuth token = ") << (m_writingAuthToken ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing shard id = ") << (m_writingShardId ? QStringLiteral("true") : QStringLiteral("false")));
        emit notifyError(error);
        return;
    }

    clear();
    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::authenticate()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticate"));

    if (m_authenticationInProgress || m_writingAuthToken || m_writingShardId) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Previous authentication is not finished yet, please wait"));
        QNDEBUG(error << QStringLiteral(", authentication in progress = ")
                << (m_authenticationInProgress ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing OAuth token = ") << (m_writingAuthToken ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing shard id = ") << (m_writingShardId ? QStringLiteral("true") : QStringLiteral("false")));
        emit authenticationFinished(/* success = */ false, error, Account());
        return;
    }

    authenticateImpl(AuthContext::Request);
}

void SynchronizationManagerPrivate::pause()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::pause"));

    if (m_remoteToLocalSyncManager.active()) {
        m_paused = true;
        m_remoteToLocalSyncWasActiveOnLastPause = true;
        emit pauseRemoteToLocalSync();
    }

    if (m_sendLocalChangesManager.active()) {
        m_paused = true;
        m_remoteToLocalSyncWasActiveOnLastPause = false;
        emit pauseSendingLocalChanges();
    }
}

void SynchronizationManagerPrivate::resume()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::resume"));

    if (!m_paused) {
        QNINFO(QStringLiteral("Wasn't paused; not doing anything on attempt to resume"));
        return;
    }

    m_paused = false;

    if (m_remoteToLocalSyncWasActiveOnLastPause) {
        m_remoteToLocalSyncManager.resume();
    }
    else {
        m_sendLocalChangesManager.resume();
    }
}

void SynchronizationManagerPrivate::stop()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::stop"));

    emit stopRemoteToLocalSync();
    emit stopSendingLocalChanges();
}

void SynchronizationManagerPrivate::revokeAuthentication(const qevercloud::UserID userId)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::revokeAuthentication: user id = ")
            << userId);

    m_lastRevokedAuthenticationUserId = userId;

    m_deleteAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_") +
                                m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId));
    m_deletingAuthToken = true;
    m_deleteAuthTokenJob.start();

    m_deleteShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_") +
                              m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId));
    m_deletingShardId = true;
    m_deleteShardIdJob.start();
}

void SynchronizationManagerPrivate::setDownloadNoteThumbnails(const bool flag)
{
    m_remoteToLocalSyncManager.setDownloadNoteThumbnails(flag);
}

void SynchronizationManagerPrivate::setNoteThumbnailsStoragePath(const QString & path)
{
    m_remoteToLocalSyncManager.setNoteThumbnailsStoragePath(path);
}

void SynchronizationManagerPrivate::onOAuthResult(bool success, qevercloud::UserID userId, QString authToken,
                                                  qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                                                  QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onOAuthResult: ") << (success ? QStringLiteral("success") : QStringLiteral("failure"))
            << QStringLiteral(", user id = ") << userId << QStringLiteral(", auth token expiration time = ")
            << printableDateTimeFromTimestamp(authTokenExpirationTime) << QStringLiteral(", error: ") << errorDescription);

    m_authenticationInProgress = false;

    if (success)
    {
        AuthData authData;
        authData.m_userId = userId;
        authData.m_authToken = authToken;
        authData.m_expirationTime = authTokenExpirationTime;
        authData.m_shardId = shardId;
        authData.m_noteStoreUrl = noteStoreUrl;
        authData.m_webApiUrlPrefix = webApiUrlPrefix;

        m_OAuthResult = authData;
        QNDEBUG(QStringLiteral("OAuth result = ") << m_OAuthResult);

        m_noteStore.setNoteStoreUrl(noteStoreUrl);
        m_noteStore.setAuthenticationToken(authToken);

        Account previousAccount = m_remoteToLocalSyncManager.account();

        Account newAccount(QString(), Account::Type::Evernote, userId, Account::EvernoteAccountType::Free, m_host);
        m_remoteToLocalSyncManager.setAccount(newAccount);

        ErrorString error;
        bool res = m_remoteToLocalSyncManager.syncUser(userId, error, authToken, /* write user data to local storage = */ false);
        if (Q_UNLIKELY(!res))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP("", "Can't switch to new Evernote account: failed to sync user data"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(errorDescription);
            emit notifyError(errorDescription);

            m_remoteToLocalSyncManager.setAccount(previousAccount);

            return;
        }

        const User & user = m_remoteToLocalSyncManager.user();
        if (Q_UNLIKELY(!user.hasUsername()))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP("", "Can't switch to new Evernote account: the synched user data lacks username"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(errorDescription);
            emit notifyError(errorDescription);

            m_remoteToLocalSyncManager.setAccount(previousAccount);

            return;
        }

        launchStoreOAuthResult(authData);
    }
    else
    {
        if (m_authContext == AuthContext::Request) {
            emit authenticationFinished(/* success = */ false, errorDescription, Account());
        }
        else {
            emit notifyError(errorDescription);
        }
    }
}

void SynchronizationManagerPrivate::onKeychainJobFinished(QKeychain::Job * job)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onKeychainJobFinished"));

    if (!job) {
        ErrorString error(QT_TRANSLATE_NOOP("", "qtkeychain error: null pointer to keychain job on finish"));
        emit notifyError(error);
        return;
    }

    if (job == &m_readAuthTokenJob)
    {
        onReadAuthTokenFinished();
    }
    else if (job == &m_readShardIdJob)
    {
        onReadShardIdFinished();
    }
    else if (job == &m_writeAuthTokenJob)
    {
        onWriteAuthTokenFinished();
    }
    else if (job == &m_writeShardIdJob)
    {
        onWriteShardIdFinished();
    }
    else if (job == &m_deleteAuthTokenJob)
    {
        onDeleteAuthTokenFinished();
    }
    else if (job == &m_deleteShardIdJob)
    {
        onDeleteShardIdFinished();
    }
    else
    {
        for(auto it = m_writeLinkedNotebookShardIdJobsByGuid.begin(), end = m_writeLinkedNotebookShardIdJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == job)
            {
                if (job->error() != QKeychain::NoError) {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error saving linked notebook's shard id to the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(job->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += job->errorString();
                    QNWARNING(error);
                    emit notifyError(error);
                }

                Q_UNUSED(m_writeLinkedNotebookShardIdJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_writeLinkedNotebookAuthTokenJobsByGuid.begin(), end = m_writeLinkedNotebookAuthTokenJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == job)
            {
                if (job->error() != QKeychain::NoError) {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error saving linked notebook's authentication token to the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(job->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += job->errorString();
                    QNWARNING(error);
                    emit notifyError(error);
                }

                Q_UNUSED(m_writeLinkedNotebookAuthTokenJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_readLinkedNotebookAuthTokenJobsByGuid.begin(), end = m_readLinkedNotebookAuthTokenJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == job)
            {
                if (job->error() == QKeychain::NoError)
                {
                    QNDEBUG(QStringLiteral("Successfully read the authentication token for linked notebook from the keychain: "
                                           "linked notebook guid: ") << it.key());
                    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[it.key()].first = cachedJob->textData();
                }
                else if (job->error() == QKeychain::EntryNotFound)
                {
                    QNDEBUG(QStringLiteral("Could not find authentication token for linked notebook in the keychain: "
                                           "linked notebook guid: ") << it.key());
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }
                else
                {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error reading linked notebook's authentication token from the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(job->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += job->errorString();
                    QNWARNING(error);
                    emit notifyError(error);

                    // Try to recover by making user to authenticate again in the blind hope that
                    // the next time the persistence of auth settings in the keychain would work
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }

                authenticateToLinkedNotebooks();
                Q_UNUSED(m_readLinkedNotebookAuthTokenJobsByGuid.erase(it))
                return;
            }
        }

        for(auto it = m_readLinkedNotebookShardIdJobsByGuid.begin(), end = m_readLinkedNotebookShardIdJobsByGuid.end();
            it != end; ++it)
        {
            const auto & cachedJob = it.value();
            if (cachedJob.data() == job)
            {
                if (job->error() == QKeychain::NoError)
                {
                    QNDEBUG(QStringLiteral("Successfully read the shard id for linked notebook from the keychain: "
                                           "linked notebook guid: ") << it.key());
                    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[it.key()].second = cachedJob->textData();
                }
                else if (job->error() == QKeychain::EntryNotFound)
                {
                    QNDEBUG(QStringLiteral("Could not find shard id for linked notebook in the keychain: "
                                           "linked notebook guid: ") << it.key());
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }
                else
                {
                    ErrorString error(QT_TRANSLATE_NOOP("", "Error reading linked notebook's shard id from the keychain"));
                    error.details() = QStringLiteral("error = ");
                    error.details() += ToString(job->error());
                    error.details() += QStringLiteral(": ");
                    error.details() += job->errorString();
                    QNWARNING(error);
                    emit notifyError(error);

                    // Try to recover by making user to authenticate again in the blind hope that
                    // the next time the persistence of auth settings in the keychain would work
                    Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(it.key()))
                }

                authenticateToLinkedNotebooks();
                Q_UNUSED(m_readLinkedNotebookShardIdJobsByGuid.erase(it))
                return;
            }
        }

        ErrorString error(QT_TRANSLATE_NOOP("", "Unknown keychain job finished event"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
}

void SynchronizationManagerPrivate::onRequestAuthenticationToken()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRequestAuthenticationToken"));

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found valid auth token and shard id, returning them"));
        emit sendAuthenticationTokenAndShardId(m_OAuthResult.m_authToken, m_OAuthResult.m_shardId, m_OAuthResult.m_expirationTime);
        return;
    }

    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString,QString> > linkedNotebookGuidsAndShareKeys)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks"));
    m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth = linkedNotebookGuidsAndShareKeys;
    authenticateToLinkedNotebooks();
}

void SynchronizationManagerPrivate::onRequestLastSyncParameters()
{
    if (m_onceReadLastSyncParams) {
        emit sendLastSyncParameters(m_lastUpdateCount, m_lastSyncTime, m_cachedLinkedNotebookLastUpdateCountByGuid,
                                    m_cachedLinkedNotebookLastSyncTimeByGuid);
        return;
    }

    readLastSyncParameters();

    emit sendLastSyncParameters(m_lastUpdateCount, m_lastSyncTime, m_cachedLinkedNotebookLastUpdateCountByGuid,
                                m_cachedLinkedNotebookLastSyncTimeByGuid);
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncFinished(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                                                QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                                                QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncFinished: lastUpdateCount = ")
            << lastUpdateCount << QStringLiteral(", lastSyncTime = ") << lastSyncTime
            << QStringLiteral(", last update count per linked notebook = ")
            << lastUpdateCountByLinkedNotebookGuid << QStringLiteral("\nlastSyncTimeByLinkedNotebookGuid = ")
            << lastSyncTimeByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;
    m_cachedLinkedNotebookLastUpdateCountByGuid = lastUpdateCountByLinkedNotebookGuid;
    m_cachedLinkedNotebookLastSyncTimeByGuid = lastSyncTimeByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    m_onceReadLastSyncParams = true;
    emit notifyRemoteToLocalSyncDone();

    sendChanges();
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncPaused(bool pendingAuthenticaton)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncPaused: pending authentication = ")
            << (pendingAuthenticaton ? QStringLiteral("true") : QStringLiteral("false")));
    emit remoteToLocalSyncPaused(pendingAuthenticaton);
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncStopped()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncStopped"));
    emit remoteToLocalSyncStopped();
}

void SynchronizationManagerPrivate::onShouldRepeatIncrementalSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onShouldRepeatIncrementalSync"));

    m_shouldRepeatIncrementalSyncAfterSendingChanges = true;
    emit willRepeatRemoteToLocalSyncAfterSendingChanges();
}

void SynchronizationManagerPrivate::onConflictDetectedDuringLocalChangesSending()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onConflictDetectedDuringLocalChangesSending"));

    emit detectedConflictDuringLocalChangesSending();

    m_sendLocalChangesManager.stop();

    // NOTE: the detection of non-synchronized state with respect to remote service often precedes the actual conflict detection;
    // need to drop this flag to prevent launching the incremental sync after sending the local changes after the incremental sync
    // which we'd launch now
    m_shouldRepeatIncrementalSyncAfterSendingChanges = false;

    launchIncrementalSync();
}

void SynchronizationManagerPrivate::onLocalChangesSent(qint32 lastUpdateCount, QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onLocalChangesSent: last update count = ") << lastUpdateCount
            << QStringLiteral(", last update count per linked notebook guid: ") << lastUpdateCountByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_cachedLinkedNotebookLastUpdateCountByGuid = lastUpdateCountByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    if (m_shouldRepeatIncrementalSyncAfterSendingChanges) {
        QNDEBUG(QStringLiteral("Repeating the incremental sync after sending the changes"));
        m_shouldRepeatIncrementalSyncAfterSendingChanges = false;
        launchIncrementalSync();
        return;
    }

    QNINFO(QStringLiteral("Finished the whole synchronization procedure!"));
    emit notifyFinish(m_remoteToLocalSyncManager.account());
}

void SynchronizationManagerPrivate::onSendLocalChangesPaused(bool pendingAuthenticaton)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onSendLocalChangesPaused: pending authentication = ")
            << (pendingAuthenticaton ? QStringLiteral("true") : QStringLiteral("false")));
    emit sendLocalChangesPaused(pendingAuthenticaton);
}

void SynchronizationManagerPrivate::onSendLocalChangesStopped()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onSendLocalChangesStopped"));
    emit sendLocalChangesStopped();
}

void SynchronizationManagerPrivate::onRateLimitExceeded(qint32 secondsToWait)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRateLimitExceeded"));
    emit rateLimitExceeded(secondsToWait);
}

void SynchronizationManagerPrivate::createConnections(IAuthenticationManager & authenticationManager)
{
    // Connections with authentication manager
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,requestAuthentication),
                     &authenticationManager, QNSLOT(IAuthenticationManager,onAuthenticationRequest));
    QObject::connect(&authenticationManager, QNSIGNAL(IAuthenticationManager,sendAuthenticationResult,bool,qevercloud::UserID,
                                                      QString,qevercloud::Timestamp,QString,QString,QString,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onOAuthResult,bool,qevercloud::UserID,
                                  QString,qevercloud::Timestamp,QString,QString,QString,ErrorString));

    // Connections with remote to local synchronization manager
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,failure,ErrorString),
                     this, QNSIGNAL(SynchronizationManagerPrivate,notifyError,ErrorString));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,finished,qint32,qevercloud::Timestamp,QHash<QString,qint32>,
                                                           QHash<QString,qevercloud::Timestamp>),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncFinished,qint32,qevercloud::Timestamp,QHash<QString,qint32>,
                                  QHash<QString,qevercloud::Timestamp>));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,rateLimitExceeded,qint32),
                     this, QNSLOT(SynchronizationManagerPrivate,onRateLimitExceeded,qint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAuthenticationToken),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationToken));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAuthenticationTokensForLinkedNotebooks,QVector<QPair<QString,QString> >),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationTokensForLinkedNotebooks,QVector<QPair<QString,QString> >));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,paused,bool),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncPaused,bool));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,stopped),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncStopped));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestLastSyncParameters),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestLastSyncParameters));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,syncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloaded));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,notesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,notesDownloadProgress,quint32,quint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebooksSyncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksSyncChunksDownloaded));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebooksNotesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksNotesDownloadProgress,quint32,quint32));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,pauseRemoteToLocalSync),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,pause));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,resumeRemoteToLocalSync),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,resume));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,stopRemoteToLocalSync),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,stop));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokenAndShardId,QString,QString,qevercloud::Timestamp),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onAuthenticationInfoReceived,QString,QString,qevercloud::Timestamp));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onAuthenticationTokensForLinkedNotebooksReceived,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendLastSyncParameters,qint32,qevercloud::Timestamp,QHash<QString,qint32>,QHash<QString,qevercloud::Timestamp>),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onLastSyncParametersReceived,qint32,qevercloud::Timestamp,QHash<QString,qint32>,QHash<QString,qevercloud::Timestamp>));

    // Connections with send local changes manager
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,failure,ErrorString),
                     this, QNSIGNAL(SynchronizationManagerPrivate,notifyError,ErrorString));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,finished,qint32,QHash<QString,qint32>),
                     this, QNSLOT(SynchronizationManagerPrivate,onLocalChangesSent,qint32,QHash<QString,qint32>));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,rateLimitExceeded,qint32),
                     this, QNSLOT(SynchronizationManagerPrivate,onRateLimitExceeded,qint32));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,requestAuthenticationToken),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationToken));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,requestAuthenticationTokensForLinkedNotebooks,QVector<QPair<QString,QString> >),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationTokensForLinkedNotebooks,QVector<QPair<QString,QString> >));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,shouldRepeatIncrementalSync),
                     this, QNSLOT(SynchronizationManagerPrivate,onShouldRepeatIncrementalSync));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,conflictDetected),
                     this, QNSLOT(SynchronizationManagerPrivate,onConflictDetectedDuringLocalChangesSending));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,paused,bool),
                     this, QNSLOT(SynchronizationManagerPrivate,onSendLocalChangesPaused,bool));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,stopped),
                     this, QNSLOT(SynchronizationManagerPrivate,onSendLocalChangesStopped));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,receivedUserAccountDirtyObjects),
                     this, QNSIGNAL(SynchronizationManagerPrivate,preparedDirtyObjectsForSending));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,receivedAllDirtyObjects),
                     this, QNSIGNAL(SynchronizationManagerPrivate,preparedLinkedNotebooksDirtyObjectsForSending));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,onAuthenticationTokensForLinkedNotebooksReceived,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,pauseSendingLocalChanges),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,pause));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,resumeSendingLocalChanges),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,resume));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,stopSendingLocalChanges),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,stop));

    // Connections with read/write/delete auth tokens/shard ids jobs
    QObject::connect(&m_readAuthTokenJob, QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
    QObject::connect(&m_readShardIdJob, QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
    QObject::connect(&m_writeAuthTokenJob, QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
    QObject::connect(&m_writeShardIdJob, QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
    QObject::connect(&m_deleteAuthTokenJob, QNSIGNAL(QKeychain::DeletePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
    QObject::connect(&m_deleteShardIdJob, QNSIGNAL(QKeychain::DeletePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
}

void SynchronizationManagerPrivate::readLastSyncParameters()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::readLastSyncParameters"));

    m_lastSyncTime = 0;
    m_lastUpdateCount = 0;
    m_cachedLinkedNotebookLastUpdateCountByGuid.clear();
    m_cachedLinkedNotebookLastSyncTimeByGuid.clear();

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = QStringLiteral("Synchronization/") + m_host + QStringLiteral("/") +
                             QString::number(m_OAuthResult.m_userId) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    QVariant lastUpdateCountVar = appSettings.value(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        m_lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last update count from persistent application settings"));
            m_lastUpdateCount = 0;
        }
    }

    QVariant lastSyncTimeVar = appSettings.value(keyGroup + LAST_SYNC_TIME_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        m_lastSyncTime = lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last sync time from persistent application settings"));
            m_lastSyncTime = 0;
        }
    }

    int numLinkedNotebooksSyncParams = appSettings.beginReadArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS);
    for(int i = 0; i < numLinkedNotebooksSyncParams; ++i)
    {
        appSettings.setArrayIndex(i);

        QString guid = appSettings.value(LINKED_NOTEBOOK_GUID_KEY).toString();
        if (guid.isEmpty()) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's guid from persistent application settings"));
            continue;
        }

        QVariant lastUpdateCountVar = appSettings.value(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY);
        bool conversionResult = false;
        qint32 lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last update count from persistent application settings"));
            continue;
        }

        QVariant lastSyncTimeVar = appSettings.value(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY);
        conversionResult = false;
        qevercloud::Timestamp lastSyncTime = lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last sync time from persistent application settings"));
            continue;
        }

        m_cachedLinkedNotebookLastUpdateCountByGuid[guid] = lastUpdateCount;
        m_cachedLinkedNotebookLastSyncTimeByGuid[guid] = lastSyncTime;
    }
    appSettings.endArray();

    m_onceReadLastSyncParams = true;
}

void SynchronizationManagerPrivate::authenticateImpl(const AuthContext::type authContext)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticateImpl: auth context = ") << authContext);

    m_authContext = authContext;

    if (m_authContext == AuthContext::Request) {
        QNDEBUG(QStringLiteral("Authentication of the new user is requested, proceeding to OAuth"));
        launchOAuth();
        return;
    }

    if (m_OAuthResult.m_userId < 0) {
        QNDEBUG(QStringLiteral("No current user id, launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found already valid authentication info"));
        finalizeAuthentication();
        return;
    }

    QNTRACE(QStringLiteral("Trying to restore persistent authentication settings..."));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.m_userId) + QStringLiteral("/");

    QVariant tokenExpirationValue = appSettings.value(keyGroup + EXPIRATION_TIMESTAMP_KEY);
    if (tokenExpirationValue.isNull()) {
        QNINFO(QStringLiteral("Authentication token expiration timestamp was not found within application settings, "
                              "assuming it has never been written & launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    bool conversionResult = false;
    qevercloud::Timestamp tokenExpirationTimestamp = tokenExpirationValue.toLongLong(&conversionResult);
    if (!conversionResult) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: failed to convert QVariant with authentication token "
                                            "expiration timestamp to the actual timestamp"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    if (checkIfTimestampIsAboutToExpireSoon(tokenExpirationTimestamp)) {
        QNINFO(QStringLiteral("Authentication token stored in persistent application settings is about to expire soon enough, "
                              "launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    m_OAuthResult.m_expirationTime = tokenExpirationTimestamp;

    QNTRACE(QStringLiteral("Restoring persistent note store url"));

    QVariant noteStoreUrlValue = appSettings.value(keyGroup + NOTE_STORE_URL_KEY);
    if (noteStoreUrlValue.isNull()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to find the note store url within persistent application settings"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    QString noteStoreUrl = noteStoreUrlValue.toString();
    if (noteStoreUrl.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: failed to convert the note store url from QVariant to QString"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    m_OAuthResult.m_noteStoreUrl = noteStoreUrl;

    QNDEBUG(QStringLiteral("Restoring persistent web api url prefix"));

    QVariant webApiUrlPrefixValue = appSettings.value(keyGroup + WEB_API_URL_PREFIX_KEY);
    if (webApiUrlPrefixValue.isNull()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to find the web API url prefix within persistent application settings"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    QString webApiUrlPrefix = webApiUrlPrefixValue.toString();
    if (webApiUrlPrefix.isEmpty()) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to convert the web api url prefix from QVariant to QString"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    m_OAuthResult.m_webApiUrlPrefix = webApiUrlPrefix;

    QNDEBUG(QStringLiteral("Trying to restore the authentication token and the shard id from the keychain"));

    m_readAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_auth_token_") +
                              m_host + QStringLiteral("_") + QString::number(m_OAuthResult.m_userId));
    m_readingAuthToken = true;
    m_readAuthTokenJob.start();

    m_readShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_shard_id_") +
                            m_host + QStringLiteral("_") + QString::number(m_OAuthResult.m_userId));
    m_readingShardId = true;
    m_readShardIdJob.start();
}

void SynchronizationManagerPrivate::launchOAuth()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchOAuth"));

    m_authenticationInProgress = true;
    emit requestAuthentication();
}

void SynchronizationManagerPrivate::launchSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchSync"));

    if (!m_onceReadLastSyncParams) {
        readLastSyncParameters();
    }

    emit notifyStart();

    if (m_lastUpdateCount <= 0) {
        QNDEBUG(QStringLiteral("The client has never synchronized with the remote service, "
                               "performing the full sync"));
        launchFullSync();
        return;
    }

    QNDEBUG(QStringLiteral("Performing incremental sync"));
    launchIncrementalSync();
}

void SynchronizationManagerPrivate::launchFullSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchFullSync"));
    m_remoteToLocalSyncManager.start();
}

void SynchronizationManagerPrivate::launchIncrementalSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchIncrementalSync: m_lastUpdateCount = ")
            << m_lastUpdateCount);
    m_remoteToLocalSyncManager.start(m_lastUpdateCount);
}

void SynchronizationManagerPrivate::sendChanges()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::sendChanges"));
    m_sendLocalChangesManager.start(m_lastUpdateCount, m_cachedLinkedNotebookLastUpdateCountByGuid);
}

void SynchronizationManagerPrivate::launchStoreOAuthResult(const AuthData & result)
{
    m_writtenOAuthResult = result;

    m_writeAuthTokenJob.setKey(QApplication::applicationName() + QStringLiteral("_auth_token_") +
                               m_host + QStringLiteral("_") + QString::number(result.m_userId));
    m_writeAuthTokenJob.setTextData(result.m_authToken);
    m_writingAuthToken = true;
    m_writeAuthTokenJob.start();

    m_writeShardIdJob.setKey(QApplication::applicationName() + QStringLiteral("_shard_id_") +
                             m_host + QStringLiteral("_") + QString::number(result.m_userId));
    m_writeShardIdJob.setTextData(result.m_shardId);
    m_writingShardId = true;
    m_writeShardIdJob.start();
}

void SynchronizationManagerPrivate::finalizeStoreOAuthResult()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::finalizeStoreOAuthResult"));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_writtenOAuthResult.m_userId) + QStringLiteral("/");

    appSettings.setValue(keyGroup + NOTE_STORE_URL_KEY, m_writtenOAuthResult.m_noteStoreUrl);
    appSettings.setValue(keyGroup + EXPIRATION_TIMESTAMP_KEY, m_writtenOAuthResult.m_expirationTime);
    appSettings.setValue(keyGroup + WEB_API_URL_PREFIX_KEY, m_writtenOAuthResult.m_webApiUrlPrefix);

    QNDEBUG(QStringLiteral("Successfully wrote the authentication result info to the application settings for host ")
            << m_host << QStringLiteral(", user id ") << m_writtenOAuthResult.m_userId << QStringLiteral(": ")
            << QStringLiteral(": auth token expiration timestamp = ") << printableDateTimeFromTimestamp(m_writtenOAuthResult.m_expirationTime)
            << QStringLiteral(", web API url prefix = ") << m_writtenOAuthResult.m_webApiUrlPrefix);

    finalizeAuthentication();
}

void SynchronizationManagerPrivate::finalizeRevokeAuthentication()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::finalizeRevokeAuthentication: user id = ")
            << m_lastRevokedAuthenticationUserId);

    QKeychain::Error errorCode = m_deleteAuthTokenJob.error();
    if ((errorCode != QKeychain::NoError) && (errorCode != QKeychain::EntryNotFound)) {
        QNWARNING(QStringLiteral("Attempt to delete the auth token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_deleteAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to delete authentication token from the keychain"));
        error.details() = m_deleteAuthTokenJob.errorString();
        emit authenticationRevoked(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    errorCode = m_deleteShardIdJob.error();
    if ((errorCode != QKeychain::NoError) && (errorCode != QKeychain::EntryNotFound)) {
        QNWARNING(QStringLiteral("Attempt to delete the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_deleteShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to delete shard id from the keychain"));
        error.details() = m_deleteShardIdJob.errorString();
        emit authenticationRevoked(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully revoked the authentication for user id ")
            << m_lastRevokedAuthenticationUserId
            << QStringLiteral(": both auth token and shard id either deleted or didn't exist"));
    emit authenticationRevoked(/* success = */ true, ErrorString(),
                               m_lastRevokedAuthenticationUserId);
}

void SynchronizationManagerPrivate::finalizeAuthentication()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::finalizeAuthentication: result = ") << m_OAuthResult);

    switch(m_authContext)
    {
    case AuthContext::Blank:
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: incorrect authentication context: blank"));
        emit notifyError(error);
        break;
    }
    case AuthContext::SyncLaunch:
        launchSync();
        break;
    case AuthContext::Request:
    {
        Account account = m_remoteToLocalSyncManager.account();
        QNDEBUG(QStringLiteral("Emitting the authenticationFinished signal: ") << account);
        emit authenticationFinished(/* success = */ true, ErrorString(), account);

        m_writtenOAuthResult = AuthData();
        m_writtenOAuthResult.m_userId = -1;
        break;
    }
    case AuthContext::AuthToLinkedNotebooks:
        authenticateToLinkedNotebooks();
        break;
    default:
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Internal error: unknown authentication context"));
        error.details() = ToString(m_authContext);
        emit notifyError(error);
        break;
    }
    }

    m_authContext = AuthContext::Blank;
}

void SynchronizationManagerPrivate::timerEvent(QTimerEvent * pTimerEvent)
{
    if (Q_UNLIKELY(!pTimerEvent)) {
        ErrorString errorDescription(QT_TRANSLATE_NOOP("", "Qt error: detected null pointer to QTimerEvent"));
        QNWARNING(errorDescription);
        emit notifyError(errorDescription);
        return;
    }

    int timerId = pTimerEvent->timerId();
    killTimer(timerId);

    QNDEBUG(QStringLiteral("Timer event for timer id ") << timerId);

    if (timerId == m_launchSyncPostponeTimerId)  {
        QNDEBUG(QStringLiteral("Re-launching the sync procedure due to RATE_LIMIT_REACHED exception "
                               "when trying to get the sync state the last time"));
        launchSync();
        return;
    }

    if (timerId == m_authenticateToLinkedNotebooksPostponeTimerId)  {
        QNDEBUG(QStringLiteral("Re-attempting to authenticate to remaining linked (shared) notebooks"));
        onRequestAuthenticationTokensForLinkedNotebooks(m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth);
        return;
    }
}

void SynchronizationManagerPrivate::clear()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::clear"));

    m_lastUpdateCount = -1;
    m_lastSyncTime = -1;
    m_cachedLinkedNotebookLastUpdateCountByGuid.clear();
    m_cachedLinkedNotebookLastSyncTimeByGuid.clear();
    m_onceReadLastSyncParams = false;

    m_authContext = AuthContext::Blank;

    m_launchSyncPostponeTimerId = -1;

    m_remoteToLocalSyncManager.stop();
    m_sendLocalChangesManager.stop();

    m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.clear();
    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.clear();
    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.clear();

    m_authenticateToLinkedNotebooksPostponeTimerId = -1;

    m_readLinkedNotebookAuthTokenJobsByGuid.clear();
    m_readLinkedNotebookShardIdJobsByGuid.clear();
    m_writeLinkedNotebookAuthTokenJobsByGuid.clear();
    m_writeLinkedNotebookShardIdJobsByGuid.clear();

    m_linkedNotebookGuidsWithoutLocalAuthData.clear();

    m_shouldRepeatIncrementalSyncAfterSendingChanges = false;

    m_paused = false;
    m_remoteToLocalSyncWasActiveOnLastPause = false;
}

bool SynchronizationManagerPrivate::validAuthentication() const
{
    if (m_OAuthResult.m_expirationTime == static_cast<qint64>(0)) {
        // The value is not set
        return false;
    }

    return !checkIfTimestampIsAboutToExpireSoon(m_OAuthResult.m_expirationTime);
}

bool SynchronizationManagerPrivate::checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::checkIfTimestampIsAboutToExpireSoon: ")
            << printableDateTimeFromTimestamp(timestamp));

    qevercloud::Timestamp currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    QNTRACE(QStringLiteral("Current datetime: ") << printableDateTimeFromTimestamp(currentTimestamp));

    if ((timestamp - currentTimestamp) < SIX_HOURS_IN_MSEC) {
        return true;
    }

    return false;
}

void SynchronizationManagerPrivate::authenticateToLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticateToLinkedNotebooks"));

    if (Q_UNLIKELY(m_OAuthResult.m_userId < 0)) {
        ErrorString error(QT_TRANSLATE_NOOP("", "Detected attempt to authenticate to linked notebooks while there is no user id set to the synchronization manager"));
        QNWARNING(error);
        emit notifyError(error);
        return;
    }

    const int numLinkedNotebooks = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.size();
    if (numLinkedNotebooks == 0) {
        QNDEBUG(QStringLiteral("No linked notebooks waiting for authentication, sending the cached auth tokens, shard ids and expiration times"));
        emit sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
        return;
    }

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.m_userId) + QStringLiteral("/");

    QHash<QString,QPair<QString,QString> >  authTokensAndShardIdsToCacheByGuid;
    QHash<QString,qevercloud::Timestamp>    authTokenExpirationTimestampsToCacheByGuid;

    QString keyPrefix = QApplication::applicationName() + QStringLiteral("_") + m_host +
                        QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);

    for(auto it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.begin();
        it != m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.end(); )
    {
        const QPair<QString,QString> & pair = *it;

        const QString & guid = pair.first;
        const QString & sharedNotebookGlobalId = pair.second;

        bool forceRemoteAuth = false;
        auto linkedNotebookAuthTokenIt = m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.find(guid);
        if (linkedNotebookAuthTokenIt == m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.end())
        {
            auto noAuthDataIt = m_linkedNotebookGuidsWithoutLocalAuthData.find(guid);
            if (noAuthDataIt != m_linkedNotebookGuidsWithoutLocalAuthData.end())
            {
                forceRemoteAuth = true;
                Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.erase(noAuthDataIt))
            }
            else
            {
                QNDEBUG(QStringLiteral("Haven't found the authentication token and shard id for linked notebook guid ") << guid
                        << QStringLiteral(" in the local cache, will try to read them from the keychain"));

                // 1) Set up the job of reading the authentication token
                auto readAuthTokenJobIt = m_readLinkedNotebookAuthTokenJobsByGuid.find(guid);
                if (readAuthTokenJobIt == m_readLinkedNotebookAuthTokenJobsByGuid.end())
                {
                    QSharedPointer<QKeychain::ReadPasswordJob> pReadAuthTokenJob(new QKeychain::ReadPasswordJob(READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB));
                    pReadAuthTokenJob->setKey(keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid);
                    QObject::connect(pReadAuthTokenJob.data(), QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
                    pReadAuthTokenJob->start();
                    Q_UNUSED(m_readLinkedNotebookAuthTokenJobsByGuid.insert(guid, pReadAuthTokenJob))
                }

                // 2) Set up the job reading the shard id
                auto readShardIdJobIt = m_readLinkedNotebookShardIdJobsByGuid.find(guid);
                if (readShardIdJobIt == m_readLinkedNotebookShardIdJobsByGuid.end())
                {
                    QSharedPointer<QKeychain::ReadPasswordJob> pReadShardIdJob(new QKeychain::ReadPasswordJob(READ_LINKED_NOTEBOOK_SHARD_ID_JOB));
                    pReadShardIdJob->setKey(keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid);
                    QObject::connect(pReadShardIdJob.data(), QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                                     this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
                    pReadShardIdJob->start();
                    Q_UNUSED(m_readLinkedNotebookShardIdJobsByGuid.insert(guid, pReadShardIdJob))
                }

                ++it;
                continue;
            }
        }

        if (!forceRemoteAuth)
        {
            auto linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.find(guid);
            if (linkedNotebookAuthTokenExpirationIt == m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end())
            {
                QVariant expirationTimeVariant = appSettings.value(keyGroup + LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + guid);
                if (!expirationTimeVariant.isNull())
                {
                    bool conversionResult = false;
                    qevercloud::Timestamp expirationTime = expirationTimeVariant.toLongLong(&conversionResult);
                    if (conversionResult) {
                        linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.insert(guid, expirationTime);
                    }
                    else {
                        QNWARNING(QStringLiteral("Can't convert linked notebook's authentication token's expiration time from QVariant retrieved from ")
                                  << QStringLiteral("app settings into timestamp: linked notebook guid = ") << guid
                                  << QStringLiteral(", variant = ") << expirationTimeVariant);
                    }
                }
            }

            if ( (linkedNotebookAuthTokenExpirationIt != m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end()) &&
                 !checkIfTimestampIsAboutToExpireSoon(linkedNotebookAuthTokenExpirationIt.value()) )
            {
                QNDEBUG(QStringLiteral("Found authentication data for linked notebook guid ") << guid
                        << QStringLiteral(" + verified its expiration timestamp"));
                it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.erase(it);
                continue;
            }
        }

        QNDEBUG(QStringLiteral("Authentication data for linked notebook guid ") << guid
                << QStringLiteral(" was either not found in local cache (and/or app settings / keychain) ")
                << QStringLiteral("or has expired, need to receive that from remote Evernote service"));

        if (m_authenticateToLinkedNotebooksPostponeTimerId >= 0) {
            QNDEBUG(QStringLiteral("Authenticate to linked notebook postpone timer is active, will wait "
                                   "to preserve the breach of Evernote rate API limit"));
            ++it;
            continue;
        }

        if (m_authContext != AuthContext::Blank) {
            QNDEBUG(QStringLiteral("Authentication context variable is not set to blank which means ")
                    << QStringLiteral("that authentication must be in progress: ")
                    << m_authContext << QStringLiteral("; won't attempt to call remote Evernote API at this time"));
            ++it;
            continue;
        }

        qevercloud::AuthenticationResult authResult;
        ErrorString errorDescription;
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = m_noteStore.authenticateToSharedNotebook(sharedNotebookGlobalId, authResult,
                                                                    errorDescription, rateLimitSeconds);
        if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
        {
            if (validAuthentication()) {
                ErrorString error(QT_TRANSLATE_NOOP("", "Unexpected AUTH_EXPIRED error"));
                error.additionalBases().append(errorDescription.base());
                error.additionalBases().append(errorDescription.additionalBases());
                error.details() = errorDescription.details();
                emit notifyError(error);
            }
            else {
                authenticateImpl(AuthContext::AuthToLinkedNotebooks);
            }

            ++it;
            continue;
        }
        else if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
        {
            if (rateLimitSeconds <= 0) {
                errorDescription.base() = QString::fromUtf8(QT_TRANSLATE_NOOP("", "Rate limit reached but the number of seconds to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                emit notifyError(errorDescription);
                return;
            }

            m_authenticateToLinkedNotebooksPostponeTimerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));

            ++it;
            continue;
        }
        else if (errorCode != 0)
        {
            emit notifyError(errorDescription);
            return;
        }

        QString shardId;
        if (authResult.user.isSet() && authResult.user->shardId.isSet()) {
            shardId = authResult.user->shardId.ref();
        }

        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] = QPair<QString, QString>(authResult.authenticationToken, shardId);
        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] = authResult.expiration;

        QPair<QString,QString> & authTokenAndShardId = authTokensAndShardIdsToCacheByGuid[guid];
        authTokenAndShardId.first = authResult.authenticationToken;
        authTokenAndShardId.second = shardId;

        authTokenExpirationTimestampsToCacheByGuid[guid] = authResult.expiration;

        it = m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.erase(it);
    }

    if (m_linkedNotebookGuidsAndGlobalIdsWaitingForAuth.empty()) {
        QNDEBUG(QStringLiteral("Retrieved authentication data for all requested linked notebooks, sending the answer now"));
        emit sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
    }

    // Caching linked notebook's authentication token's expiration time in app settings
    typedef QHash<QString,qevercloud::Timestamp>::const_iterator ExpirationTimeCIter;
    ExpirationTimeCIter authTokenExpirationTimesToCacheEnd = authTokenExpirationTimestampsToCacheByGuid.end();
    for(ExpirationTimeCIter it = authTokenExpirationTimestampsToCacheByGuid.begin();
        it != authTokenExpirationTimesToCacheEnd; ++it)
    {
        QString key = LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + it.key();
        appSettings.setValue(keyGroup + key, QVariant(it.value()));
    }

    // Caching linked notebook's authentication tokens and shard ids in the keychain

    for(auto it = authTokensAndShardIdsToCacheByGuid.begin(), end = authTokensAndShardIdsToCacheByGuid.end();
        it != end; ++it)
    {
        const QString & guid = it.key();
        const QString & token = it.value().first;
        const QString & shardId = it.value().second;

        // 1) Set up the job writing the auth token to the keychain
        QString key = keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid;
        QSharedPointer<QKeychain::WritePasswordJob> pWriteAuthTokenJob(new QKeychain::WritePasswordJob(WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB));
        Q_UNUSED(m_writeLinkedNotebookAuthTokenJobsByGuid.insert(key, pWriteAuthTokenJob))
        pWriteAuthTokenJob->setKey(key);
        pWriteAuthTokenJob->setTextData(token);
        QObject::connect(pWriteAuthTokenJob.data(), QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                         this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
        pWriteAuthTokenJob->start();

        // 2) Set up the job writing the shard id to the keychain
        key = keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid;
        QSharedPointer<QKeychain::WritePasswordJob> pWriteShardIdJob(new QKeychain::WritePasswordJob(WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB));
        Q_UNUSED(m_writeLinkedNotebookShardIdJobsByGuid.insert(key, pWriteShardIdJob))
        pWriteShardIdJob->setKey(key);
        pWriteShardIdJob->setTextData(shardId);
        QObject::connect(pWriteShardIdJob.data(), QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                         this, QNSLOT(SynchronizationManagerPrivate,onKeychainJobFinished,QKeychain::Job*));
        pWriteShardIdJob->start();
    }
}

void SynchronizationManagerPrivate::onReadAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onReadAuthTokenFinished"));

    m_readingAuthToken = false;

    QKeychain::Error errorCode = m_readAuthTokenJob.error();
    if (errorCode == QKeychain::EntryNotFound)
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Unexpectedly missing OAuth token in the keychain"));
        error.details() = m_readAuthTokenJob.errorString();
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
    else if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to read the authentication token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_readAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to read the stored authentication token from the keychain"));
        error.details() = m_readAuthTokenJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the authentication token"));
    m_OAuthResult.m_authToken = m_readAuthTokenJob.textData();

    if (!m_readingShardId) {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onReadShardIdFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onReadShardIdFinished"));

    m_readingShardId = false;

    QKeychain::Error errorCode = m_readShardIdJob.error();
    if (errorCode == QKeychain::EntryNotFound)
    {
        ErrorString error(QT_TRANSLATE_NOOP("", "Unexpectedly missing OAuth shard id in the keychain"));
        error.details() = m_readShardIdJob.errorString();
        QNWARNING(error);
        emit notifyError(error);
        return;
    }
    else if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to read the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_readShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to read the stored shard id from the keychain"));
        error.details() = m_readShardIdJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the shard id"));
    m_OAuthResult.m_shardId = m_readShardIdJob.textData();

    if (!m_readingAuthToken) {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onWriteAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onWriteAuthTokenFinished"));

    m_writingAuthToken = false;

    QKeychain::Error errorCode = m_writeAuthTokenJob.error();
    if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to write the authentication token returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_writeAuthTokenJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to write the oauth token to the keychain"));
        error.details() = m_writeAuthTokenJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the authentication token in the keychain"));

    if (!m_writingShardId) {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onWriteShardIdFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onWriteShardIdFinished"));

    m_writingShardId = false;

    QKeychain::Error errorCode = m_writeShardIdJob.error();
    if (errorCode != QKeychain::NoError) {
        QNWARNING(QStringLiteral("Attempt to write the shard id returned with error: error code ")
                  << errorCode << QStringLiteral(", ") << m_writeShardIdJob.errorString());
        ErrorString error(QT_TRANSLATE_NOOP("", "Failed to write the oauth shard id to the keychain"));
        error.details() = m_writeShardIdJob.errorString();
        emit notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the shard id in the keychain"));

    if (!m_writingAuthToken) {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onDeleteAuthTokenFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onDeleteAuthTokenFinished: user id = ")
            << m_lastRevokedAuthenticationUserId);

    m_deletingAuthToken = false;

    if (!m_deletingShardId) {
        finalizeRevokeAuthentication();
    }
}

void SynchronizationManagerPrivate::onDeleteShardIdFinished()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onDeleteShardIdFinished: user id = ")
            << m_lastRevokedAuthenticationUserId);

    m_deletingShardId = false;

    if (!m_deletingAuthToken) {
        finalizeRevokeAuthentication();
    }
}

void SynchronizationManagerPrivate::updatePersistentSyncSettings()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::updatePersistentSyncSettings"));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Synchronization/") + m_host + QStringLiteral("/") +
                             QString::number(m_OAuthResult.m_userId) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");
    appSettings.setValue(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY, m_lastUpdateCount);
    appSettings.setValue(keyGroup + LAST_SYNC_TIME_KEY, m_lastSyncTime);

    int numLinkedNotebooksSyncParams = m_cachedLinkedNotebookLastUpdateCountByGuid.size();
    appSettings.beginWriteArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS, numLinkedNotebooksSyncParams);

    int counter = 0;
    auto updateCountEnd = m_cachedLinkedNotebookLastUpdateCountByGuid.end();
    auto syncTimeEnd = m_cachedLinkedNotebookLastSyncTimeByGuid.end();
    for(auto updateCountIt = m_cachedLinkedNotebookLastUpdateCountByGuid.begin(); updateCountIt != updateCountEnd; ++updateCountIt)
    {
        const QString & guid = updateCountIt.key();
        auto syncTimeIt = m_cachedLinkedNotebookLastSyncTimeByGuid.find(guid);
        if (syncTimeIt == syncTimeEnd) {
            QNWARNING(QStringLiteral("Detected inconsistent last sync parameters for one of linked notebooks: last update count is present "
                                     "while last sync time is not, skipping writing the persistent settings entry for this linked notebook"));
            continue;
        }

        appSettings.setArrayIndex(counter);
        appSettings.setValue(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY, updateCountIt.value());
        appSettings.setValue(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY, syncTimeIt.value());

        ++counter;
    }

    appSettings.endArray();

    QNTRACE(QStringLiteral("Wrote ") << counter << QStringLiteral(" last sync params entries for linked notebooks"));
}

QTextStream & SynchronizationManagerPrivate::AuthData::print(QTextStream & strm) const
{
    strm << QStringLiteral("AuthData: {\n")
         << QStringLiteral("    user id = ") << m_userId << QStringLiteral(";\n")
         << QStringLiteral("    auth token expiration time = ") << printableDateTimeFromTimestamp(m_expirationTime) << QStringLiteral(";\n")
         << QStringLiteral("    shard id = ") << m_shardId << QStringLiteral(";\n")
         << QStringLiteral("    note store url = ") << m_noteStoreUrl << QStringLiteral(";\n")
         << QStringLiteral("    web API url prefix = ") << m_webApiUrlPrefix << QStringLiteral(";\n")
         << QStringLiteral("};\n");
    return strm;
}

} // namespace quentier
