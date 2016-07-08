#ifndef LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/utility/Qt4Helper.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/ResourceWrapper.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerThreadWorker)

namespace test {

class ResourceLocalStorageManagerAsyncTester: public QObject
{
    Q_OBJECT
public:
    explicit ResourceLocalStorageManagerAsyncTester(QObject * parent = Q_NULLPTR);
    ~ResourceLocalStorageManagerAsyncTester();

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

// private signals:
    void addNotebookRequest(Notebook notebook, QUuid requestId = QUuid());
    void addNoteRequest(Note note, QUuid requestId = QUuid());
    void getResourceCountRequest(QUuid requestId = QUuid());
    void addResourceRequest(ResourceWrapper resource, QUuid requestId = QUuid());
    void updateResourceRequest(ResourceWrapper resource, QUuid requestId = QUuid());
    void findResourceRequest(ResourceWrapper resource, bool withBinaryData, QUuid requestId = QUuid());
    void expungeResourceRequest(ResourceWrapper resource, QUuid requestId = QUuid());

private Q_SLOTS:
    void onWorkerInitialized();
    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);
    void onAddNotebookFailed(Notebook notebook, QString errorDescription, QUuid requestId);
    void onAddNoteCompleted(Note note, QUuid requestId);
    void onAddNoteFailed(Note note, QString errorDescription, QUuid requestId);
    void onGetResourceCountCompleted(int count, QUuid requestId);
    void onGetResourceCountFailed(QString errorDescription, QUuid requestId);
    void onAddResourceCompleted(ResourceWrapper resource, QUuid requestId);
    void onAddResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId);
    void onUpdateResourceCompleted(ResourceWrapper resource, QUuid requestId);
    void onUpdateResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId);
    void onFindResourceCompleted(ResourceWrapper resource, bool withBinaryData, QUuid requestId);
    void onFindResourceFailed(ResourceWrapper resource, bool withBinaryData,
                              QString errorDescription, QUuid requestId);
    void onExpungeResourceCompleted(ResourceWrapper resource, QUuid requestId);
    void onExpungeResourceFailed(ResourceWrapper resource, QString errorDescription, QUuid requestId);

private:
    void createConnections();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_NOTE_REQUEST,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST
    };

    State m_state;

    LocalStorageManagerThreadWorker * m_pLocalStorageManagerThreadWorker;
    QThread *                   m_pLocalStorageManagerThread;

    Notebook                    m_notebook;
    Note                        m_note;
    ResourceWrapper             m_initialResource;
    ResourceWrapper             m_foundResource;
    ResourceWrapper             m_modifiedResource;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H