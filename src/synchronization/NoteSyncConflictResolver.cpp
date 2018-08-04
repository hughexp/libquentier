/*
 * Copyright 2018 Dmitry Ivanov
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

#include "NoteSyncConflictResolver.h"
#include "SynchronizationShared.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier_private/synchronization/INoteStore.h>
#include <QTimerEvent>

namespace quentier {

NoteSyncConflictResolver::NoteSyncConflictResolver(IManager & manager,
                                                   const qevercloud::Note & remoteNote,
                                                   const Note & localConflict,
                                                   QObject * parent) :
    QObject(parent),
    m_manager(manager),
    m_remoteNote(remoteNote),
    m_localConflict(localConflict),
    m_remoteNoteAsLocalNote(),
    m_shouldOverrideLocalNoteWithRemoteNote(false),
    m_pendingLocalConflictUpdateInLocalStorage(false),
    m_pendingFullRemoteNoteDataDownload(false),
    m_pendingRemoteNoteAdditionToLocalStorage(false),
    m_pendingRemoteNoteUpdateInLocalStorage(false),
    m_retryNoteDownloadingTimerId(-1),
    m_addNoteRequestId(),
    m_updateNoteRequestId(),
    m_started(false)
{}

void NoteSyncConflictResolver::start()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::start: remote note guid = ")
            << (m_remoteNote.guid.isSet() ? m_remoteNote.guid.ref() : QStringLiteral("<not set>"))
            << QStringLiteral(", local conflict local uid = ") << m_localConflict);

    if (m_started) {
        QNDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_started = true;

    connectToLocalStorage();
    processNotesConflictByGuid();
}

void NoteSyncConflictResolver::onAddNoteComplete(Note note, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onAddNoteComplete: request id = ")
            << requestId << QStringLiteral(", note: ") << note);

    m_addNoteRequestId = QUuid();

    if (m_pendingRemoteNoteAdditionToLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully added remote note as a new note to the local storage"));
        m_pendingRemoteNoteAdditionToLocalStorage = false;
        Q_EMIT finished(m_remoteNote);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: internal error, "
                                     "received unidentified note addition acknowledge event within the local storage"));
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onAddNoteFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral("; note: ") << note);

    m_addNoteRequestId = QUuid();
    Q_EMIT failure(m_remoteNote, errorDescription);
}

void NoteSyncConflictResolver::onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onUpdateNoteComplete: note = ") << note
            << QStringLiteral("\nRequest id = ") << requestId << QStringLiteral(", update resource metadata = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata) ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", update resource binary data = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData) ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", update tags = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags) ? QStringLiteral("true") : QStringLiteral("false")));

    m_updateNoteRequestId = QUuid();

    if (m_pendingLocalConflictUpdateInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Local conflicting note was successfully updated in the local storage"));

        m_pendingLocalConflictUpdateInLocalStorage = false;

        if (m_pendingFullRemoteNoteDataDownload) {
            QNDEBUG(QStringLiteral("Still waiting for full remote note's data downloading"));
            return;
        }

        addRemoteNoteToLocalStorageAsNewNote();
    }
    else if (m_pendingRemoteNoteUpdateInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Remote note was successfully updated in the local storage"));
        m_pendingRemoteNoteUpdateInLocalStorage = false;
        Q_EMIT finished(m_remoteNote);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: internal error, "
                                     "received unidentified note update acknowledge from local storage"));
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                  ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNWARNING(QStringLiteral("NoteSyncConflictResolver::onUpdateNoteFailed: note = ") << note
              << QStringLiteral("\nRequest id = ") << requestId << QStringLiteral(", update resource metadata = ")
              << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata) ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral(", update resource binary data = ")
              << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData) ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral(", update tags = ")
              << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags) ? QStringLiteral("true") : QStringLiteral("false"))
              << QStringLiteral("; error description = ") << errorDescription);

    m_updateNoteRequestId = QUuid();

    if (m_pendingLocalConflictUpdateInLocalStorage)
    {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: failed to update "
                                     "the local conflicting note in the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
    else if (m_pendingRemoteNoteUpdateInLocalStorage)
    {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: failed to update "
                                     "the remote note in the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: internal error, "
                                     "received unidentified note update reject event within the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onGetNoteAsyncFinished(qint32 errorCode, qevercloud::Note qecNote,
                                                      qint32 rateLimitSeconds, ErrorString errorDescription)
{
    if (!qecNote.guid.isSet() || !m_remoteNote.guid.isSet() || (qecNote.guid.ref() != m_remoteNote.guid.ref())) {
        return;
    }

    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::onGetNoteAsyncFinished: error code = ") << errorCode
            << QStringLiteral(", note = ") << qecNote << QStringLiteral("\nRate limit seconds = ")
            << rateLimitSeconds << QStringLiteral(", error description = ") << errorDescription);

    if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(QT_TR_NOOP("QEverCloud or Evernote protocol error: caught RATE_LIMIT_REACHED "
                                                "exception but the number of seconds to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            Q_EMIT failure(m_remoteNote, errorDescription);
            return;
        }

        int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to postpone the Evernote API call "
                                                "due to rate limit exceeding"));
            Q_EMIT failure(m_remoteNote, errorDescription);
            return;
        }

        m_retryNoteDownloadingTimerId = timerId;
        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
    {
        // FIXME: handle auth expiration
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(m_remoteNote, errorDescription);
        return;
    }

    m_pendingFullRemoteNoteDataDownload = false;

    if (m_shouldOverrideLocalNoteWithRemoteNote)
    {
        overrideLocalNoteWithRemoteChanges();

        m_pendingRemoteNoteUpdateInLocalStorage = true;

        m_updateNoteRequestId = QUuid::createUuid();
        LocalStorageManager::UpdateNoteOptions options(LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
                                                       LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
                                                       LocalStorageManager::UpdateNoteOption::UpdateTags);
        QNDEBUG(QStringLiteral("Emitting the request to update the local conflict overridden by the remote note "
                               "within the local storage: request id = ") << m_updateNoteRequestId
                << QStringLiteral(", note: ") << m_remoteNoteAsLocalNote);
        Q_EMIT updateNote(m_localConflict, options, m_updateNoteRequestId);
    }
    else
    {
        if (m_pendingLocalConflictUpdateInLocalStorage) {
            QNDEBUG(QStringLiteral("Still pending the update of local conflicting note in the local storage"));
            return;
        }

        addRemoteNoteToLocalStorageAsNewNote();
    }
}

void NoteSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::connectToLocalStorage"));

    LocalStorageManagerAsync & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(NoteSyncConflictResolver,addNote,Note,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this, QNSIGNAL(NoteSyncConflictResolver,updateNote,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,Note,LocalStorageManager::UpdateNoteOptions,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onAddNoteComplete,Note,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onAddNoteFailed,Note,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onUpdateNoteComplete,Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid),
                     this, QNSLOT(NoteSyncConflictResolver,onUpdateNoteFailed,Note,LocalStorageManager::UpdateNoteOptions,ErrorString,QUuid));
}

void NoteSyncConflictResolver::processNotesConflictByGuid()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::processNotesConflictByGuid"));

    if (Q_UNLIKELY(!m_remoteNote.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the confict between remote and local notes: "
                                     "the remote note has no guid set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteNote.updateSequenceNum.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: "
                                     "the remote note has no update sequence number set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(error << QStringLiteral(": ") << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notes: "
                                     "the local note has no guid set"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(m_remoteNote.guid.ref() != m_localConflict.guid())) {
        ErrorString error(QT_TR_NOOP("Note sync conflict resolution was applied to notes which do not conflict by guid"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (m_localConflict.hasUpdateSequenceNumber() && (m_localConflict.updateSequenceNumber() >= m_remoteNote.updateSequenceNum.ref())) {
        QNDEBUG(QStringLiteral("The local note has update sequence number equal to or greater than the remote note's one ")
                << QStringLiteral("=> local note should override the remote one"));
        Q_EMIT finished(m_remoteNote);
        return;
    }

    // Regardless of the exact way of further processing we need to download full note data for the remote note
    if (!downloadFullRemoteNoteData()) {
        return;
    }

    if (!m_localConflict.isDirty()) {
        QNDEBUG(QStringLiteral("The local conflicting note is not dirty and thus should be overridden with the remote note"));
        m_shouldOverrideLocalNoteWithRemoteNote = true;
        return;
    }

    QNDEBUG(QStringLiteral("The local conflicting note has been marked as dirty one, need to clear Evernote-assigned fields from it"));

    m_localConflict.setGuid(QString());
    m_localConflict.setUpdateSequenceNumber(-1);

    if (m_localConflict.hasResources())
    {
        QList<Resource> resources = m_localConflict.resources();
        for(auto it = resources.begin(), end = resources.end(); it != end; ++it) {
            Resource & resource = *it;
            resource.setGuid(QString());
            resource.setUpdateSequenceNumber(-1);
            resource.setDirty(true);
        }

        m_localConflict.setResources(resources);
    }

    m_pendingLocalConflictUpdateInLocalStorage = true;

    m_updateNoteRequestId = QUuid::createUuid();
    LocalStorageManager::UpdateNoteOptions options(LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);
    QNDEBUG(QStringLiteral("Emitting the request to update the local conflicting note (after clearing Evernote assigned fields from it): request id = ")
            << m_updateNoteRequestId << QStringLiteral(", note to update: ") << m_localConflict);
    Q_EMIT updateNote(m_localConflict, options, m_updateNoteRequestId);
}

void NoteSyncConflictResolver::overrideLocalNoteWithRemoteChanges()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::overrideLocalNoteWithRemoteChanges"));
    QNTRACE(QStringLiteral("Local conflict: ") << m_localConflict << QStringLiteral("\nRemote note: ") << m_remoteNote);

    Note localNote(m_localConflict);

    // Need to clear out the tag local uids from the local note so that the local storage uses tag guids list
    // from the remote note instead
    localNote.setTagLocalUids(QStringList());

    // NOTE: dealing with resources is tricky: need to not screw up the local uids of note's resources
    QList<Resource> resources;
    if (localNote.hasResources()) {
        resources = localNote.resources();
    }

    localNote.qevercloudNote() = m_remoteNote;
    localNote.setDirty(false);
    localNote.setLocal(false);

    QList<qevercloud::Resource> updatedResources;
    if (m_remoteNote.resources.isSet()) {
        updatedResources = m_remoteNote.resources.ref();
    }

    QList<Resource> amendedResources;
    amendedResources.reserve(updatedResources.size());

    // First update those resources which were within the local note already
    for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
    {
        Resource & resource = *it;
        if (!resource.hasGuid()) {
            continue;
        }

        bool foundResource = false;
        for(auto uit = updatedResources.constBegin(), uend = updatedResources.constEnd(); uit != uend; ++uit)
        {
            const qevercloud::Resource & updatedResource = *uit;
            if (!updatedResource.guid.isSet()) {
                continue;
            }

            if (updatedResource.guid.ref() == resource.guid()) {
                resource.qevercloudResource() = updatedResource;
                // NOTE: need to not forget to reset the dirty flag since we are
                // resetting the state of the local resource here
                resource.setDirty(false);
                resource.setLocal(false);
                foundResource = true;
                break;
            }
        }

        if (foundResource) {
            amendedResources << resource;
        }
    }

    // Then account for new resources
    for(auto uit = updatedResources.constBegin(), uend = updatedResources.constEnd(); uit != uend; ++uit)
    {
        const qevercloud::Resource & updatedResource = *uit;
        if (Q_UNLIKELY(!updatedResource.guid.isSet())) {
            QNWARNING(QStringLiteral("Skipping resource from remote note without guid: ") << updatedResource);
            continue;
        }

        const Resource * pExistingResource = Q_NULLPTR;
        for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
        {
            const Resource & resource = *it;
            if (resource.hasGuid() && (resource.guid() == updatedResource.guid.ref())) {
                pExistingResource = &resource;
                break;
            }
        }

        if (pExistingResource) {
            continue;
        }

        Resource newResource;
        newResource.qevercloudResource() = updatedResource;
        newResource.setDirty(false);
        newResource.setLocal(false);
        newResource.setNoteLocalUid(localNote.localUid());
        amendedResources << newResource;
    }

    localNote.setResources(amendedResources);
    QNTRACE(QStringLiteral("Local note after overriding: ") << localNote);

    m_localConflict = localNote;
}

void NoteSyncConflictResolver::addRemoteNoteToLocalStorageAsNewNote()
{
    QNDEBUG(QStringLiteral("NoteSyncConflictResolver::addRemoteNoteToLocalStorageAsNewNote"));

    m_pendingRemoteNoteAdditionToLocalStorage = true;

    m_addNoteRequestId = QUuid::createUuid();
    QNDEBUG(QStringLiteral("Emitting the request to add note to the local storage: request id = ")
            << m_addNoteRequestId << QStringLiteral(", note: ") << m_remoteNoteAsLocalNote);
    Q_EMIT addNote(m_remoteNoteAsLocalNote, m_addNoteRequestId);
}

bool NoteSyncConflictResolver::downloadFullRemoteNoteData()
{
    m_remoteNoteAsLocalNote = Note(m_remoteNote);
    QString authToken;
    ErrorString errorDescription;
    INoteStore * pNoteStore = m_manager.noteStoreForNote(m_remoteNoteAsLocalNote, authToken, errorDescription);
    if (Q_UNLIKELY(!pNoteStore)) {
        ErrorString error(QT_TR_NOOP("Can't resolve sync conflict between notes: internal error, failed to find note store for the remote note"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote);
        QNWARNING(error << QStringLiteral(": ") << m_remoteNoteAsLocalNote);
        Q_EMIT failure(m_remoteNote, error);
        return false;
    }

    QObject::connect(pNoteStore, QNSIGNAL(INoteStore,getNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                     this, QNSLOT(NoteSyncConflictResolver,onGetNoteAsyncFinished,qint32,qevercloud::Note,qint32,ErrorString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    bool withContent = true;
    bool withResourceData = true;
    bool withResourceRecognition = true;
    bool withResourceAlternateData = true;
    bool withSharedNotes = true;
    bool withNoteAppDataValues = true;
    bool withResourceAppDataValues = true;
    bool withNoteLimits = m_manager.syncingLinkedNotebooksContent();
    bool res = pNoteStore->getNoteAsync(withContent, withResourceData, withResourceRecognition,
                                        withResourceAlternateData, withSharedNotes,
                                        withNoteAppDataValues, withResourceAppDataValues,
                                        withNoteLimits, m_remoteNoteAsLocalNote.guid(), authToken, errorDescription);
    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, m_remoteNoteAsLocalNote)
        QNWARNING(errorDescription << QStringLiteral(", note: ") << m_remoteNoteAsLocalNote);
        Q_EMIT failure(m_remoteNote, errorDescription);
        return false;
    }

    m_pendingFullRemoteNoteDataDownload = true;
    return true;
}

void NoteSyncConflictResolver::timerEvent(QTimerEvent * pEvent)
{
    if (!pEvent) {
        ErrorString errorDescription(QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING(errorDescription);
        Q_EMIT failure(m_remoteNote, errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);
    QNDEBUG(QStringLiteral("Killed timer with id ") << timerId);

    if (timerId == m_retryNoteDownloadingTimerId) {
        Q_UNUSED(downloadFullRemoteNoteData());
    }
}

} // namespace quentier
