/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H

#include "NoteStore.h"
#include "UserStore.h"
#include "NotebookSyncConflictResolver.h"
#include "NotebookSyncConflictResolutionCache.h"
#include "TagSyncConflictResolver.h"
#include "TagSyncConflictResolutionCache.h"
#include "SavedSearchSyncConflictResolver.h"
#include "SavedSearchSyncConflictResolutionCache.h"
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Macros.h>
#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/User.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Resource.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/SavedSearch.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloudOAuth.h>
#else
#include <qt4qevercloud/QEverCloudOAuth.h>
#endif

#include <QMultiHash>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

class RemoteToLocalSynchronizationManager: public QObject
{
    Q_OBJECT
public:
    explicit RemoteToLocalSynchronizationManager(LocalStorageManagerAsync & localStorageManagerAsync,
                                                 const QString & host, QSharedPointer<qevercloud::NoteStore> pNoteStore,
                                                 QSharedPointer<qevercloud::UserStore> pUserStore,
                                                 QObject * parent = Q_NULLPTR);

    bool active() const;

    void setAccount(const Account & account);
    Account account() const;

    bool syncUser(const qevercloud::UserID userId, ErrorString & errorDescription,
                  const QString & authToken = QString(), const bool writeUserDataToLocalStorage = true);
    const User & user() const;

    bool shouldDownloadThumbnailsForNotes() const;
    bool shouldDownloadInkNoteImages() const;
    QString inkNoteImagesStoragePath() const;

Q_SIGNALS:
    void failure(ErrorString errorDescription);
    void finished(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime, QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                  QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    // signal notifying that the Evernote API rate limit was exceeded so that the synchronization
    // needs to wait for the specified number of seconds before it proceeds (that would happen automatically,
    // there's no need to restart the synchronization manually)
    void rateLimitExceeded(qint32 secondsToWait);

    // signals notifying about the progress of synchronization
    void syncChunksDownloaded();
    void notesDownloadProgress(quint32 notesDownloaded, quint32 totalNotesToDownload);

    void linkedNotebooksSyncChunksDownloaded();
    void linkedNotebooksNotesDownloadProgress(quint32 notesDownloaded, quint32 totalNotesToDownload);

    void expungedFromServerToClient();

    void paused(bool pendingAuthenticaton);
    void stopped();

    void requestAuthenticationToken();
    void requestAuthenticationTokensForLinkedNotebooks(QVector<QPair<QString, QString> > linkedNotebookGuidsAndSharedNotebookGlobalIds);
    void requestLastSyncParameters();

public Q_SLOTS:
    void start(qint32 afterUsn = 0);
    void stop();
    void pause();
    void resume();

    void onAuthenticationInfoReceived(QString authToken, QString shardId, qevercloud::Timestamp expirationTime);
    void onAuthenticationTokensForLinkedNotebooksReceived(QHash<QString,QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuid,
                                                          QHash<QString,qevercloud::Timestamp> authenticationTokenExpirationTimesByLinkedNotebookGuid);
    void onLastSyncParametersReceived(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                      QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                      QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    void setDownloadNoteThumbnails(const bool flag);
    void setDownloadInkNoteImages(const bool flag);
    void setInkNoteImagesStoragePath(const QString & path);

    void collectNonProcessedItemsSmallestUsns(qint32 & usn, QHash<QString,qint32> & usnByLinkedNotebookGuid);

// private signals
Q_SIGNALS:
    void addUser(User user, QUuid requestId);
    void updateUser(User user, QUuid requestId);
    void findUser(User user, QUuid requestId);

    void addNotebook(Notebook notebook, QUuid requestId);
    void updateNotebook(Notebook notebook, QUuid requestId);
    void findNotebook(Notebook notebook, QUuid requestId);
    void expungeNotebook(Notebook notebook, QUuid requestId);

    void addNote(Note note, QUuid requestId);
    void updateNote(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void findNote(Note note, bool withResourceBinaryData, QUuid requestId);
    void expungeNote(Note note, QUuid requestId);

    void addTag(Tag tag, QUuid requestId);
    void updateTag(Tag tag, QUuid requestId);
    void findTag(Tag tag, QUuid requestId);
    void expungeTag(Tag tag, QUuid requestId);

    void expungeNotelessTagsFromLinkedNotebooks(QUuid requestId);

    void addResource(Resource resource, QUuid requestId);
    void updateResource(Resource resource, QUuid requestId);
    void findResource(Resource resource, bool withBinaryData, QUuid requestId);

    void addLinkedNotebook(LinkedNotebook notebook, QUuid requestId);
    void updateLinkedNotebook(LinkedNotebook notebook, QUuid requestId);
    void findLinkedNotebook(LinkedNotebook linkedNotebook, QUuid requestId);
    void expungeLinkedNotebook(LinkedNotebook notebook, QUuid requestId);
    void listAllLinkedNotebooks(size_t limit, size_t offset, LocalStorageManager::ListLinkedNotebooksOrder::type,
                                LocalStorageManager::OrderDirection::type orderDirection, QUuid requestId);

    void addSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void updateSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void findSavedSearch(SavedSearch savedSearch, QUuid requestId);
    void expungeSavedSearch(SavedSearch savedSearch, QUuid requestId);

private Q_SLOTS:
    void onFindUserCompleted(User user, QUuid requestId);
    void onFindUserFailed(User user, ErrorString errorDescription, QUuid requestId);
    void onFindNotebookCompleted(Notebook notebook, QUuid requestId);
    void onFindNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);
    void onFindNoteCompleted(Note note, bool withResourceBinaryData, QUuid requestId);
    void onFindNoteFailed(Note note, bool withResourceBinaryData, ErrorString errorDescription, QUuid requestId);
    void onFindTagCompleted(Tag tag, QUuid requestId);
    void onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onFindResourceCompleted(Resource resource, bool withResourceBinaryData, QUuid requestId);
    void onFindResourceFailed(Resource resource, bool withResourceBinaryData,
                              ErrorString errorDescription, QUuid requestId);
    void onFindLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId);
    void onFindLinkedNotebookFailed(LinkedNotebook linkedNotebook, ErrorString errorDescription, QUuid requestId);
    void onFindSavedSearchCompleted(SavedSearch savedSearch, QUuid requestId);
    void onFindSavedSearchFailed(SavedSearch savedSearch, ErrorString errorDescription, QUuid requestId);

    void onAddUserCompleted(User user, QUuid requestId);
    void onAddUserFailed(User user, ErrorString errorDescription, QUuid requestId);
    void onAddTagCompleted(Tag tag, QUuid requestId);
    void onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onUpdateUserCompleted(User user, QUuid requestId);
    void onUpdateUserFailed(User user, ErrorString errorDescription, QUuid requestId);
    void onUpdateTagCompleted(Tag tag, QUuid requestId);
    void onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);
    void onExpungeTagCompleted(Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId);
    void onExpungeTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId);

    void onExpungeNotelessTagsFromLinkedNotebooksCompleted(QUuid requestId);
    void onExpungeNotelessTagsFromLinkedNotebooksFailed(ErrorString errorDescription, QUuid);

    void onAddSavedSearchCompleted(SavedSearch search, QUuid requestId);
    void onAddSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);
    void onUpdateSavedSearchCompleted(SavedSearch search, QUuid requestId);
    void onUpdateSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);
    void onExpungeSavedSearchCompleted(SavedSearch search, QUuid requestId);
    void onExpungeSavedSearchFailed(SavedSearch search, ErrorString errorDescription, QUuid requestId);

    void onAddLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId);
    void onAddLinkedNotebookFailed(LinkedNotebook linkedNotebook, ErrorString errorDescription, QUuid requestId);
    void onUpdateLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId);
    void onUpdateLinkedNotebookFailed(LinkedNotebook linkedNotebook, ErrorString errorDescription, QUuid requestId);
    void onExpungeLinkedNotebookCompleted(LinkedNotebook linkedNotebook, QUuid requestId);
    void onExpungeLinkedNotebookFailed(LinkedNotebook linkedNotebook, ErrorString errorDescription, QUuid requestId);

    void onListAllLinkedNotebooksCompleted(size_t limit, size_t offset, LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                           LocalStorageManager::OrderDirection::type orderDirection,
                                           QList<LinkedNotebook> linkedNotebooks, QUuid requestId);
    void onListAllLinkedNotebooksFailed(size_t limit, size_t offset, LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                        LocalStorageManager::OrderDirection::type orderDirection, ErrorString errorDescription, QUuid requestId);

    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);
    void onAddNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);
    void onUpdateNotebookCompleted(Notebook notebook, QUuid requestId);
    void onUpdateNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);
    void onExpungeNotebookCompleted(Notebook notebook, QUuid requestId);
    void onExpungeNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onAddNoteCompleted(Note note, QUuid requestId);
    void onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId);
    void onUpdateNoteCompleted(Note note, bool updateResources, bool updateTags, QUuid requestId);
    void onUpdateNoteFailed(Note note, bool updateResources, bool updateTags,
                            ErrorString errorDescription, QUuid requestId);
    void onExpungeNoteCompleted(Note note, QUuid requestId);
    void onExpungeNoteFailed(Note note, ErrorString errorDescription, QUuid requestId);

    void onAddResourceCompleted(Resource resource, QUuid requestId);
    void onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId);
    void onUpdateResourceCompleted(Resource resource, QUuid requestId);
    void onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId);

    void onInkNoteImageDownloadFinished(bool status, QString resourceGuid, QString noteGuid, ErrorString errorDescription);
    void onNoteThumbnailDownloadingFinished(bool status, QString noteGuid, QByteArray downloadedThumbnailImageData,
                                            ErrorString errorDescription);

    void onGetNoteAsyncFinished(qint32 errorCode, Note note, qint32 rateLimitSeconds, ErrorString errorDescription);

    // Slots for sync conflict resolvers
    void onNotebookSyncConflictResolverFinished(qevercloud::Notebook remoteNotebook);
    void onNotebookSyncConflictResolverFailure(qevercloud::Notebook remoteNotebook, ErrorString errorDescription);

    void onTagSyncConflictResolverFinished(qevercloud::Tag remoteTag);
    void onTagSyncConflictResolverFailure(qevercloud::Tag remoteTag, ErrorString errorDescription);

    void onSavedSearchSyncConflictResolverFinished(qevercloud::SavedSearch remoteSavedSearch);
    void onSavedSearchSyncConflictResolverFailure(qevercloud::SavedSearch remoteSavedSearch, ErrorString errorDescription);

private:
    void createConnections();

    void connectToLocalStorage();
    void disconnectFromLocalStorage();

    QString defaultInkNoteImageStoragePath() const;

    void launchSync();

    // If any of these returns false, it is either due to error or due to API rate limit exceeding
    bool checkProtocolVersion(ErrorString & errorDescription);
    bool syncUserImpl(const bool waitIfRateLimitReached, ErrorString & errorDescription,
                      const bool writeUserDataToLocalStorage = true);

    void launchWritingUserDataToLocalStorage();

    bool checkAndSyncAccountLimits(const bool waitIfRateLimitReached, ErrorString & errorDescription);
    bool syncAccountLimits(const bool waitIfRateLimitReached, ErrorString & errorDescription);
    void readSavedAccountLimits();
    void writeAccountLimitsToAppSettings();

    void launchTagsSync();
    void launchSavedSearchSync();
    void launchLinkedNotebookSync();
    void launchNotebookSync();

    bool syncingLinkedNotebooksContent() const;

    void checkAndIncrementNoteDownloadProgress(const QString & noteGuid);

    void checkHighUsnCollectingCompletion();

    struct ContentSource
    {
        enum type
        {
            UserAccount,
            LinkedNotebook
        };
    };

    friend QTextStream & operator<<(QTextStream & strm, const ContentSource::type & obj);

    template <class ContainerType, class LocalType>
    void launchDataElementSync(const ContentSource::type contentSource,
                               const QString & typeName, ContainerType & container,
                               QList<QString> & expungedElements);

    template <class ElementType>
    void extractExpungedElementsFromSyncChunk(const qevercloud::SyncChunk & syncChunk,
                                              QList<QString> & expungedElementGuids);

    template <class ElementType>
    void checkAndAddLinkedNotebookBinding(const ElementType & sourceElement, ElementType & targetElement);

    template <class RemoteElementType, class ElementType>
    void resolveSyncConflict(const RemoteElementType & remoteElement,
                             const ElementType & localConflict);

    template <class ContainerType>
    bool mapContainerElementsWithLinkedNotebookGuid(const QString & linkedNotebookGuid,
                                                    const ContainerType & container);

    template <class ElementType>
    void unmapContainerElementsFromLinkedNotebookGuid(const QList<QString> & guids);

    template <class ContainerType>
    void appendDataElementsFromSyncChunkToContainer(const qevercloud::SyncChunk & syncChunk,
                                                    ContainerType & container);

    // ========= Find by guid helpers ===========

    template <class ElementType>
    void emitFindByGuidRequest(const QString & guid);

    template <class ElementType, class ContainerType>
    bool onFoundDuplicateByGuid(ElementType element, const QUuid & requestId,
                                const QString & typeName, ContainerType & container,
                                QSet<QUuid> & findByGuidRequestIds);

    template <class ContainerType, class ElementType>
    bool onNoDuplicateByGuid(ElementType element, const QUuid & requestId,
                             const ErrorString & errorDescription,
                             const QString & typeName, ContainerType & container,
                             QSet<QUuid> & findElementRequestIds);

    // ========= Find by name helpers ===========

    template <class ElementType>
    void emitFindByNameRequest(const ElementType & elementToFind);

    template <class ContainerType, class ElementType>
    bool onFoundDuplicateByName(ElementType element, const QUuid & requestId,
                                const QString & typeName, ContainerType & container,
                                QSet<QUuid> & findElementRequestIds);

    template <class ContainerType, class ElementType>
    bool onNoDuplicateByName(ElementType element, const QUuid & requestId,
                             const ErrorString & errorDescription,
                             const QString & typeName, ContainerType & container,
                             QSet<QUuid> & findElementRequestIds);

    // ========= Add helpers ============

    template <class ElementType>
    void emitAddRequest(const ElementType & elementToAdd);

    template <class ElementType>
    void onAddDataElementCompleted(const ElementType & element, const QUuid & requestId,
                                   const QString & typeName, QSet<QUuid> & addElementRequestIds);

    template <class ElementType>
    void onAddDataElementFailed(const ElementType & element, const QUuid & requestId,
                                const ErrorString & errorDescription, const QString & typeName,
                                QSet<QUuid> & addElementRequestIds);

    // ========== Update helpers ===========

    template <class ElementType>
    void emitUpdateRequest(const ElementType & elementToUpdate,
                           const ElementType * elementToAddLater = Q_NULLPTR);

    template <class ElementType, class ElementsToAddByUuid>
    void onUpdateDataElementCompleted(const ElementType & element, const QUuid & requestId,
                                      const QString & typeName, QSet<QUuid> & updateElementRequestIds,
                                      ElementsToAddByUuid & elementsToAddByRenameRequestId);

    template <class ElementType, class ElementsToAddByUuid>
    void onUpdateDataElementFailed(const ElementType & element, const QUuid & requestId,
                                   const ErrorString & errorDescription, const QString & typeName,
                                   QSet<QUuid> & updateElementRequestIds,
                                   ElementsToAddByUuid & elementsToAddByRenameRequestId);

    template<class ElementType>
    void performPostAddOrUpdateChecks();

    template <class ElementType>
    void unsetLocalUid(ElementType & element);

    template <class ElementType>
    void setNonLocalAndNonDirty(ElementType & element);

    // ========= Expunge helpers ============

    template <class ElementType>
    void onExpungeDataElementCompleted(const ElementType & element, const QUuid & requestId,
                                       const QString & typeName, QSet<QUuid> & expungeElementRequestIds);

    template <class ElementType>
    void onExpungeDataElementFailed(const ElementType & element, const QUuid & requestId,
                                    const ErrorString & errorDescription, const QString & typeName,
                                    QSet<QUuid> & expungeElementRequestIds);

    void expungeTags();
    void expungeSavedSearches();
    void expungeLinkedNotebooks();
    void expungeNotebooks();
    void expungeNotes();

    template <class ElementType>
    void performPostExpungeChecks();

    void expungeFromServerToClient();
    void checkExpungesCompletion();

    // ========= Find in blocks from sync chunk helpers ===========

    template <class ContainerType, class ElementType>
    typename ContainerType::iterator findItemByName(ContainerType & container,
                                                    const ElementType & element,
                                                    const QString & typeName);

    template <class ContainerType, class ElementType>
    typename ContainerType::iterator findItemByGuid(ContainerType & container,
                                                    const ElementType & element,
                                                    const QString & typeName);

    // ========= Helpers launching the sync of dependent data elements ==========
    void checkNotebooksAndTagsSyncAndLaunchNotesSync();
    void launchNotesSync();

    void checkNotesSyncAndLaunchResourcesSync();
    void launchResourcesSync();

    // Helpers launching the sync of content from someone else's shared notebooks, to be used
    // when LinkedNotebook representing pointers to content from someone else's account are in sync
    void checkLinkedNotebooksSyncAndLaunchLinkedNotebookContentSync();
    void checkLinkedNotebooksNotebooksAndTagsSyncAndLaunchLinkedNotebookNotesSync();

    void launchLinkedNotebooksContentsSync();
    void startLinkedNotebooksSync();

    bool checkAndRequestAuthenticationTokensForLinkedNotebooks();
    void requestAuthenticationTokensForAllLinkedNotebooks();

    void requestAllLinkedNotebooks();

    void getLinkedNotebookSyncState(const LinkedNotebook & linkedNotebook,
                                    const QString & authToken, qevercloud::SyncState & syncState,
                                    bool & asyncWait, bool & error);
    bool downloadLinkedNotebooksSyncChunks();

    void launchLinkedNotebooksTagsSync();
    void launchLinkedNotebooksNotebooksSync();
    void launchLinkedNotebooksNotesSync();

    bool hasPendingRequests() const;
    void checkServerDataMergeCompletion();

    void finalize();
    void clear();

    void handleLinkedNotebookAdded(const LinkedNotebook & linkedNotebook);
    void handleLinkedNotebookUpdated(const LinkedNotebook & linkedNotebook);

    virtual void timerEvent(QTimerEvent * pEvent);

    void getFullNoteDataAsync(const Note & note);
    void getFullNoteDataAsyncAndAddToLocalStorage(const Note & note);
    void getFullNoteDataAsyncAndUpdateInLocalStorage(const Note & note);

    void downloadSyncChunksAndLaunchSync(qint32 afterUsn);

    const Notebook * getNotebookPerNote(const Note & note) const;

    void handleAuthExpiration();

    bool checkUserAccountSyncState(bool & asyncWait, bool & error, qint32 & afterUsn);
    bool checkLinkedNotebooksSyncStates(bool & asyncWait, bool & error);

    void authenticationInfoForNotebook(const Notebook & notebook, QString & authToken,
                                       QString & shardId, bool & isPublic) const;

    bool findNotebookForInkNoteImageDownloading(const Note & note);
    void setupInkNoteImageDownloading(const QString & resourceGuid, const int resourceHeight, const int resourceWidth,
                                      const QString & noteGuid, const Notebook & notebook);
    bool setupInkNoteImageDownloadingForNote(const Note & note, const Notebook & notebook);

    bool findNotebookForNoteThumbnailDownloading(const Note & note);
    bool setupNoteThumbnailDownloading(const Note & note, const Notebook & notebook);

    QString clientNameForProtocolVersionCheck() const;

    Note createConflictingNote(const Note & originalNote) const;

    qint32 nonProcessedItemsSmallestUsn(const QString & linkedNotebookGuid = QString()) const;

private:
    template <class T>
    class CompareItemByName
    {
    public:
        CompareItemByName(const QString & name) : m_name(name) {}
        bool operator()(const T & item) const;

    private:
        const QString m_name;
    };

    template <class T>
    class CompareItemByGuid
    {
    public:
        CompareItemByGuid(const QString & guid) : m_guid(guid) {}
        bool operator()(const T & item) const;

    private:
        const QString m_guid;
    };

    typedef QList<qevercloud::Tag> TagsList;
    typedef QList<qevercloud::SavedSearch> SavedSearchesList;
    typedef QList<qevercloud::LinkedNotebook> LinkedNotebooksList;
    typedef QList<qevercloud::Notebook> NotebooksList;
    typedef QList<qevercloud::Note> NotesList;
    typedef QList<qevercloud::Resource> ResourcesList;

    struct InkNoteResourceData
    {
        InkNoteResourceData() :
            m_resourceGuid(),
            m_noteGuid(),
            m_resourceHeight(0),
            m_resourceWidth(0)
        {}

        InkNoteResourceData(const QString & resourceGuid, const QString & noteGuid, int height, int width) :
            m_resourceGuid(resourceGuid),
            m_noteGuid(noteGuid),
            m_resourceHeight(height),
            m_resourceWidth(width)
        {}

        QString     m_resourceGuid;
        QString     m_noteGuid;
        int         m_resourceHeight;
        int         m_resourceWidth;
    };

    struct SyncMode
    {
        enum type
        {
            FullSync = 0,
            IncrementalSync
        };
    };

    friend QTextStream & operator<<(QTextStream & strm, const SyncMode::type & obj);

private:
    LocalStorageManagerAsync &              m_localStorageManagerAsync;
    bool                                    m_connectedToLocalStorage;

    QString                                 m_host;

    NoteStore                               m_noteStore;
    UserStore                               m_userStore;

    qint32                                  m_maxSyncChunkEntries;
    SyncMode::type                          m_lastSyncMode;
    qevercloud::Timestamp                   m_lastSyncTime;
    qint32                                  m_lastUpdateCount;

    bool                                    m_onceSyncDone;

    qint32                                  m_lastUsnOnStart;
    qint32                                  m_lastSyncChunksDownloadedUsn;

    bool                                    m_syncChunksDownloaded;
    bool                                    m_fullNoteContentsDownloaded;
    bool                                    m_expungedFromServerToClient;
    bool                                    m_linkedNotebooksSyncChunksDownloaded;

    bool                                    m_active;
    bool                                    m_paused;
    bool                                    m_requestedToStop;

    bool                                    m_edamProtocolVersionChecked;

    QVector<qevercloud::SyncChunk>          m_syncChunks;
    QVector<qevercloud::SyncChunk>          m_linkedNotebookSyncChunks;
    QSet<QString>                           m_linkedNotebookGuidsForWhichSyncChunksWereDownloaded;

    qevercloud::AccountLimits               m_accountLimits;

    TagsList                                m_tags;
    QList<QString>                          m_expungedTags;
    QHash<QUuid,Tag>                        m_tagsToAddPerRequestId;
    QSet<QUuid>                             m_findTagByNameRequestIds;
    QSet<QUuid>                             m_findTagByGuidRequestIds;
    QSet<QUuid>                             m_addTagRequestIds;
    QSet<QUuid>                             m_updateTagRequestIds;
    QSet<QUuid>                             m_expungeTagRequestIds;

    TagSyncConflictResolutionCache          m_tagSyncConflictResolutionCache;

    QHash<QString,QString>                  m_linkedNotebookGuidsByTagGuids;
    QUuid                                   m_expungeNotelessTagsRequestId;

    SavedSearchesList                       m_savedSearches;
    QList<QString>                          m_expungedSavedSearches;
    QHash<QUuid,SavedSearch>                m_savedSearchesToAddPerRequestId;
    QSet<QUuid>                             m_findSavedSearchByNameRequestIds;
    QSet<QUuid>                             m_findSavedSearchByGuidRequestIds;
    QSet<QUuid>                             m_addSavedSearchRequestIds;
    QSet<QUuid>                             m_updateSavedSearchRequestIds;
    QSet<QUuid>                             m_expungeSavedSearchRequestIds;

    SavedSearchSyncConflictResolutionCache  m_savedSearchSyncConflictResolutionCache;

    LinkedNotebooksList                     m_linkedNotebooks;
    QList<QString>                          m_expungedLinkedNotebooks;
    QSet<QUuid>                             m_findLinkedNotebookRequestIds;
    QSet<QUuid>                             m_addLinkedNotebookRequestIds;
    QSet<QUuid>                             m_updateLinkedNotebookRequestIds;
    QSet<QUuid>                             m_expungeLinkedNotebookRequestIds;

    QList<LinkedNotebook>                   m_allLinkedNotebooks;
    QUuid                                   m_listAllLinkedNotebooksRequestId;
    bool                                    m_allLinkedNotebooksListed;

    QString                                 m_authenticationToken;
    QString                                 m_shardId;
    qevercloud::Timestamp                   m_authenticationTokenExpirationTime;
    bool                                    m_pendingAuthenticationTokenAndShardId;
    User                                    m_user;

    QUuid                                   m_findUserRequestId;
    QUuid                                   m_addOrUpdateUserRequestId;
    bool                                    m_onceAddedOrUpdatedUserInLocalStorage;

    QHash<QString,QPair<QString,QString> >  m_authenticationTokensAndShardIdsByLinkedNotebookGuid;
    QHash<QString,qevercloud::Timestamp>    m_authenticationTokenExpirationTimesByLinkedNotebookGuid;
    bool                                    m_pendingAuthenticationTokensForLinkedNotebooks;

    QHash<QString,qevercloud::SyncState>    m_syncStatesByLinkedNotebookGuid;
    QHash<QString,qint32>                   m_lastSynchronizedUsnByLinkedNotebookGuid;
    QHash<QString,qevercloud::Timestamp>    m_lastSyncTimeByLinkedNotebookGuid;
    QHash<QString,qint32>                   m_lastUpdateCountByLinkedNotebookGuid;

    NotebooksList                           m_notebooks;
    QList<QString>                          m_expungedNotebooks;
    QHash<QUuid,Notebook>                   m_notebooksToAddPerRequestId;
    QSet<QUuid>                             m_findNotebookByNameRequestIds;
    QSet<QUuid>                             m_findNotebookByGuidRequestIds;
    QSet<QUuid>                             m_addNotebookRequestIds;
    QSet<QUuid>                             m_updateNotebookRequestIds;
    QSet<QUuid>                             m_expungeNotebookRequestIds;

    NotebookSyncConflictResolutionCache     m_notebookSyncConflictResolutionCache;

    QHash<QString,QString>                  m_linkedNotebookGuidsByNotebookGuids;

    NotesList                               m_notes;
    quint32                                 m_originalNumberOfNotes;
    quint32                                 m_numNotesDownloaded;
    QList<QString>                          m_expungedNotes;
    QSet<QUuid>                             m_findNoteByGuidRequestIds;
    QSet<QUuid>                             m_addNoteRequestIds;
    QSet<QUuid>                             m_updateNoteRequestIds;
    QSet<QUuid>                             m_expungeNoteRequestIds;

    typedef QHash<QUuid,QPair<Note,QUuid> > NoteDataPerFindNotebookRequestId;
    NoteDataPerFindNotebookRequestId        m_notesWithFindRequestIdsPerFindNotebookRequestId;

    QHash<QPair<QString,QString>,Notebook>  m_notebooksPerNoteGuids;

    ResourcesList                           m_resources;
    QSet<QUuid>                             m_findResourceByGuidRequestIds;
    QSet<QUuid>                             m_addResourceRequestIds;
    QSet<QUuid>                             m_updateResourceRequestIds;

    typedef QHash<QUuid,QPair<Resource,QUuid> > ResourceDataPerFindNoteRequestId;
    ResourceDataPerFindNoteRequestId        m_resourcesWithFindRequestIdsPerFindNoteRequestId;

    typedef QHash<QUuid,InkNoteResourceData> InkNoteResourceDataPerFindNotebookRequestId;
    InkNoteResourceDataPerFindNotebookRequestId             m_inkNoteResourceDataPerFindNotebookRequestId;

    typedef QMultiHash<QString,QString> ResourceGuidsPendingInkNoteImageDownloadPerNoteGuid;
    ResourceGuidsPendingInkNoteImageDownloadPerNoteGuid     m_resourceGuidsPendingInkNoteImageDownloadPerNoteGuid;

    QHash<QUuid,Note>                       m_notesPendingInkNoteImagesDownloadByFindNotebookRequestId;
    QHash<QUuid,Note>                       m_notesPendingThumbnailDownloadByFindNotebookRequestId;

    QHash<QString,Note>                     m_notesPendingThumbnailDownloadByGuid;
    QSet<QUuid>                             m_updateNoteWithThumbnailRequestIds;

    QSet<QUuid>                             m_resourceFoundFlagPerFindResourceRequestId;

    QSet<QString>                           m_localUidsOfElementsAlreadyAttemptedToFindByName;

    QSet<QString>                           m_guidsOfNotesPendingDownloadForAddingToLocalStorage;
    QSet<QString>                           m_guidsOfNotesPendingDownloadForUpdatingInLocalStorage;

    QHash<int,Note>                         m_notesToAddPerAPICallPostponeTimerId;
    QHash<int,Note>                         m_notesToUpdatePerAPICallPostponeTimerId;
    QHash<int,qint32>                       m_afterUsnForSyncChunkPerAPICallPostponeTimerId;
    int                                     m_getLinkedNotebookSyncStateBeforeStartAPICallPostponeTimerId;
    int                                     m_downloadLinkedNotebookSyncChunkAPICallPostponeTimerId;
    int                                     m_getSyncStateBeforeStartAPICallPostponeTimerId;
    int                                     m_syncUserPostponeTimerId;
    int                                     m_syncAccountLimitsPostponeTimerId;

    bool                                    m_gotLastSyncParameters;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_REMOTE_TO_LOCAL_SYNCHRONIZATION_MANAGER_H
