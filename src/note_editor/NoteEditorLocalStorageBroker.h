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

#ifndef LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
#define LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/types/Note.h>
#include <quentier/utility/LRUCache.hpp>
#include <QObject>
#include <QHash>
#include <QSet>

namespace quentier {

class NoteEditorLocalStorageBroker: public QObject
{
    Q_OBJECT
public:
    explicit NoteEditorLocalStorageBroker(LocalStorageManagerAsync & localStorageManager,
                                          QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void noteSavedToLocalStorage(QString noteLocalUid);
    void failedToSaveNoteToLocalStorage(QString noteLocalUid, ErrorString errorDescription);

    void foundNoteAndNotebook(Note note, Notebook notebook);
    void failedToFindNoteOrNotebook(QString noteLocalUid, ErrorString errorDescription);

    void noteUpdated(Note note);
    void notebookUpdated(Notebook);
    void noteDeleted(QString noteLocalUid);
    void notebookDeleted(QString noteLocalUid);

// private signals
    void updateNote(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId);
    void addResource(Resource resource, QUuid requestId);
    void updateResource(Resource resource, QUuid requestId);
    void expungeResource(Resource resource, QUuid requestId);
    void findNote(Note note, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId);
    void findNotebook(Notebook notebook, QUuid requestId);

public Q_SLOTS:
    void saveNoteToLocalStorage(const Note & note);
    void findNoteAndNotebook(const QString & noteLocalUid);

private Q_SLOTS:
    void onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId);
    void onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                            ErrorString errorDescription, QUuid requestId);
    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);

    void onFindNoteComplete(Note foundNote, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId);
    void onFindNoteFailed(Note note, bool withResourceMetadata, bool withResourceBinaryData,
                          ErrorString errorDescription, QUuid requestId);
    void onFindNotebookComplete(Notebook foundNotebook, QUuid requestId);
    void onFindNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onAddResourceComplete(Resource resource, QUuid requestId);
    void onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId);

    void onUpdateResourceComplete(Resource resource, QUuid requestId);
    void onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId);

    void onExpungeResourceComplete(Resource resource, QUuid requestId);
    void onExpungeResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId);

    void onExpungeNoteComplete(Note note, QUuid requestId);
    void onExpungeNotebookComplete(Notebook notebook, QUuid requestId);

private:
    void createConnections(LocalStorageManagerAsync & localStorageManager);
    void emitFindNoteRequest(const QString & noteLocalUid);
    void emitFindNotebookRequest(const QString & notebookLocalUid);

private:
    Q_DISABLE_COPY(NoteEditorLocalStorageBroker)

private:
    QHash<QString, QSet<QString> >   m_originalNoteResourceLocalUidsByNoteLocalUid;

    QSet<QUuid>     m_findNoteRequestIds;
    QSet<QUuid>     m_findNotebookRequestIds;

    QHash<QUuid, QString>   m_noteLocalUidsByAddResourceRequestIds;
    QHash<QUuid, QString>   m_noteLocalUidsByUpdateResourceRequestIds;
    QHash<QUuid, QString>   m_noteLocalUidsByExpungeResourceRequestIds;

    LRUCache<QString, Notebook>     m_notebooksCache;
    LRUCache<QString, Note>         m_notesCache;

    class SaveNoteInfo: public Printable
    {
    public:
        SaveNoteInfo() :
            m_notePendingSaving(),
            m_pendingAddResourceRequests(0),
            m_pendingUpdateResourceRequests(0),
            m_pendingExpungeResourceRequests(0)
        {}

        virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

        Note        m_notePendingSaving;
        quint32     m_pendingAddResourceRequests;
        quint32     m_pendingUpdateResourceRequests;
        quint32     m_pendingExpungeResourceRequests;
    };

    QHash<QString, SaveNoteInfo>    m_saveNoteInfoByNoteLocalUids;

    QSet<QUuid>     m_updateNoteRequestIds;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_NOTE_EDITOR_LOCAL_STORAGE_BROKER_H
