/*
 * Copyright 2016-2018 Dmitry Ivanov
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

#include "ResourceDataInTemporaryFileStorageManager.h"
#include "NoteEditorLocalStorageBroker.h"
#include <quentier/types/Note.h>
#include <quentier/types/Resource.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Utility.h>
#include <quentier/utility/StandardPaths.h>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QCryptographicHash>

// 4 megabytes
#define RESOURCE_DATA_BATCH_SIZE_IN_BYTES (4194304)

namespace quentier {

ResourceDataInTemporaryFileStorageManager::ResourceDataInTemporaryFileStorageManager(QObject * parent) :
    QObject(parent),
    m_nonImageResourceFileStorageLocation(nonImageResourceFileStorageFolderPath()),
    m_imageResourceFileStorageLocation(imageResourceFileStorageFolderPath()),
    m_pCurrentNote(),
    m_resourceLocalUidsPendingFindInLocalStorage(),
    m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening(),
    m_resourceLocalUidByFilePath(),
    m_fileSystemWatcher()
{
    createConnections();
}

QString ResourceDataInTemporaryFileStorageManager::imageResourceFileStorageFolderPath()
{
    return applicationTemporaryStoragePath() + QStringLiteral("/resources/image");
}

QString ResourceDataInTemporaryFileStorageManager::nonImageResourceFileStorageFolderPath()
{
    return applicationTemporaryStoragePath() + QStringLiteral("/resources/non-image");
}

void ResourceDataInTemporaryFileStorageManager::onSaveResourceDataToTemporaryFileRequest(QString noteLocalUid, QString resourceLocalUid,
                                                                                         QByteArray data, QByteArray dataHash,
                                                                                         QUuid requestId, bool isImage)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onSaveResourceDataToTemporaryFileRequest: note local uid = ")
            << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid << QStringLiteral(", request id = ")
            << requestId << QStringLiteral(", data hash = ") << dataHash.toHex() << QStringLiteral(", is image = ")
            << (isImage ? QStringLiteral("true") : QStringLiteral("false")));

    if (dataHash.isEmpty()) {
        dataHash = calculateHash(data);
    }

    ErrorString errorDescription;
    bool res = writeResourceDataToTemporaryFile(noteLocalUid, resourceLocalUid, data, dataHash,
                                                (isImage ? ResourceType::Image : ResourceType::NonImage),
                                                errorDescription);
    if (!res) {
        Q_EMIT saveResourceDataToTemporaryFileCompleted(requestId, dataHash, errorDescription);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully wrote resource data to file: resource local uid = ") << resourceLocalUid);
    Q_EMIT saveResourceDataToTemporaryFileCompleted(requestId, dataHash, ErrorString());
}

void ResourceDataInTemporaryFileStorageManager::onReadResourceFromFileRequest(QString fileStoragePath, QString resourceLocalUid, QUuid requestId)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onReadResourceFromFileRequest: resource local uid = ")
            << resourceLocalUid << QStringLiteral(", request id = ") << requestId);

    if (Q_UNLIKELY(m_nonImageResourceFileStorageLocation.isEmpty()))
    {
        ErrorString errorDescription(QT_TR_NOOP("Resource file storage location is empty"));
        QNWARNING(errorDescription << QStringLiteral(", resource local uid = ") << resourceLocalUid
                  << QStringLiteral(", request id = ") << requestId);
        Q_EMIT readResourceFromFileCompleted(requestId, QByteArray(), QByteArray(),
                                             Errors::NoResourceFileStorageLocation,
                                             errorDescription);
        return;
    }

    QFile resourceFile(fileStoragePath);
    bool open = resourceFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!open))
    {
        ErrorString errorDescription(QT_TR_NOOP("Can't open resource file for reading"));
        errorDescription.details() = resourceFile.errorString();
        int errorCode = resourceFile.error();
        QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode << QStringLiteral(", resource local uid = ")
                  << resourceLocalUid << QStringLiteral(", request id = ") << requestId);
        Q_EMIT readResourceFromFileCompleted(requestId, QByteArray(), QByteArray(),
                                             errorCode, errorDescription);
        return;
    }

    QFileInfo resourceFileInfo(fileStoragePath);
    QFile resourceHashFile(resourceFileInfo.absolutePath() + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".hash"));
    open = resourceHashFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!open))
    {
        ErrorString errorDescription(QT_TR_NOOP("Can't open resource hash file for reading"));
        errorDescription.details() = resourceHashFile.errorString();
        int errorCode = resourceHashFile.error();
        QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode << QStringLiteral(", resource local uid = ")
                  << resourceLocalUid << QStringLiteral(", request id = ") << requestId);
        Q_EMIT readResourceFromFileCompleted(requestId, QByteArray(), QByteArray(),
                                             errorCode, errorDescription);
        return;
    }

    QByteArray data = resourceFile.readAll();
    QByteArray dataHash = resourceHashFile.readAll();

    QNDEBUG(QStringLiteral("Successfully read resource data and hash from files"));
    Q_EMIT readResourceFromFileCompleted(requestId, data, dataHash, 0, ErrorString());
}

void ResourceDataInTemporaryFileStorageManager::onOpenResourceRequest(QString resourceLocalUid)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onOpenResourceRequest: resource local uid = ")
            << resourceLocalUid);

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        ErrorString errorDescription(QT_TR_NOOP("Can't open the resource in external editor: internal error, no note is set to ResourceDataInTemporaryFileStorageManager"));
        errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
        QNWARNING(errorDescription);
        Q_EMIT failedToOpenResource(resourceLocalUid, QString(), errorDescription);
        return;
    }

    QString noteLocalUid = m_pCurrentNote->localUid();

    QList<Resource> resources = m_pCurrentNote->resources();
    const Resource * pResource = Q_NULLPTR;
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;
        if (resource.localUid() == resourceLocalUid) {
            pResource = &resource;
            break;
        }
    }

    if (Q_UNLIKELY(!pResource)) {
        ErrorString errorDescription(QT_TR_NOOP("Can't open the resource in external editor: internal error, failed to find the resource within the note"));
        errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
        QNWARNING(errorDescription);
        Q_EMIT failedToOpenResource(resourceLocalUid, noteLocalUid, errorDescription);
        return;
    }

    if (Q_UNLIKELY(!pResource->hasMime())) {
        ErrorString errorDescription(QT_TR_NOOP("Can't open the resource in external editor: resource has no mime type"));
        errorDescription.details() = QStringLiteral("resource local uid = ") + resourceLocalUid;
        QNWARNING(errorDescription << QStringLiteral(", resource: ") << *pResource);
        Q_EMIT failedToOpenResource(resourceLocalUid, noteLocalUid, errorDescription);
        return;
    }

    const QString & mime = pResource->mime();
    bool isImageResource = mime.startsWith(QStringLiteral("image"));
    QString fileStoragePath = (isImageResource
                               ? m_imageResourceFileStorageLocation
                               : m_nonImageResourceFileStorageLocation);
    fileStoragePath += QStringLiteral("/") + noteLocalUid + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".dat");

    if (pResource->hasDataHash() &&
        checkIfResourceFileExistsAndIsActual(noteLocalUid, resourceLocalUid, fileStoragePath, pResource->dataHash()))
    {
        QNDEBUG(QStringLiteral("Temporary file for resource local uid ") << resourceLocalUid << QStringLiteral(" already exists and is actual"));
        watchResourceFileForChanges(resourceLocalUid, fileStoragePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
        Q_EMIT openedResource(resourceLocalUid, noteLocalUid);
        return;
    }

    if (!pResource->hasDataBody()) {
        Q_UNUSED(m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.insert(resourceLocalUid))
        requestResourceDataFromLocalStorage(*pResource);
        return;
    }

    QByteArray dataHash = (pResource->hasDataHash()
                           ? pResource->dataHash()
                           : calculateHash(pResource->dataBody()));

    WriteResourceDataCallback callback = OpenResourcePreparationProgressFunctor(resourceLocalUid, *this);

    ErrorString errorDescription;
    bool res = writeResourceDataToTemporaryFile(noteLocalUid, resourceLocalUid, pResource->dataBody(), dataHash,
                                                (isImageResource ? ResourceType::Image : ResourceType::NonImage),
                                                errorDescription, CheckResourceFileActualityOption::On, callback);
    if (!res) {
        Q_EMIT failedToOpenResource(resourceLocalUid, noteLocalUid, errorDescription);
        return;
    }

    watchResourceFileForChanges(resourceLocalUid, fileStoragePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
    Q_EMIT openedResource(resourceLocalUid, noteLocalUid);
}

void ResourceDataInTemporaryFileStorageManager::onCurrentNoteChanged(Note note)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onCurrentNoteChanged; new note local uid = ") << note.localUid()
            << QStringLiteral(", previous note local uid = ") << (m_pCurrentNote.isNull() ? QStringLiteral("<null>") : m_pCurrentNote->localUid()));

    if (!m_pCurrentNote.isNull() && (m_pCurrentNote->localUid() == note.localUid()))
    {
        QNTRACE(QStringLiteral("The current note is the same, only the note object might have changed"));

        QList<Resource> previousResources = m_pCurrentNote->resources();
        *m_pCurrentNote = note;

        ErrorString errorDescription;
        ResultType::type res = partialUpdateResourceFilesForCurrentNote(previousResources, errorDescription);
        if (res == ResultType::Error) {
            Q_EMIT noteResourcesPreparationError(m_pCurrentNote->localUid(), errorDescription);
        }
        else if (res == ResultType::Ready) {
            Q_EMIT noteResourcesReady(m_pCurrentNote->localUid());
        }

        return;
    }

    for(auto it = m_resourceLocalUidByFilePath.constBegin(), end = m_resourceLocalUidByFilePath.constEnd(); it != end; ++it) {
        m_fileSystemWatcher.removePath(it.key());
        QNTRACE(QStringLiteral("Stopped watching for file ") << it.key());
    }
    m_resourceLocalUidByFilePath.clear();

    if (m_pCurrentNote.isNull()) {
        m_pCurrentNote.reset(new Note(note));
    }
    else {
        *m_pCurrentNote = note;
    }

    if (!m_pCurrentNote->hasResources()) {
        QNTRACE(QStringLiteral("Current note has no resources, emitting noteResourcesReady signal"));
        Q_EMIT noteResourcesReady(m_pCurrentNote->localUid());
        return;
    }

    QList<Resource> imageResources;
    QList<Resource> resources = m_pCurrentNote->resources();
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;
        if (!resource.hasMime() || !resource.mime().startsWith(QStringLiteral("image"))) {
            continue;
        }

        imageResources << resource;
        QNDEBUG(QStringLiteral("Will process image resource with local uid ") << resource.localUid());
    }

    if (imageResources.isEmpty()) {
        Q_EMIT noteResourcesReady(m_pCurrentNote->localUid());
        return;
    }

    ErrorString errorDescription;
    ResultType::type res = putResourcesDataToTemporaryFiles(imageResources, errorDescription);
    if (res == ResultType::Error) {
        Q_EMIT noteResourcesPreparationError(m_pCurrentNote->localUid(), errorDescription);
    }
    else if (res == ResultType::Ready) {
        Q_EMIT noteResourcesReady(m_pCurrentNote->localUid());
    }
}

void ResourceDataInTemporaryFileStorageManager::onRequestDiagnostics(QUuid requestId)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onRequestDiagnostics: request id = ") << requestId);

    QString diagnostics = QStringLiteral("ResourceDataInTemporaryFileStorageManager diagnostics: {\n");

    diagnostics += QStringLiteral("  Resource local uids by file paths: \n");
    for(auto it = m_resourceLocalUidByFilePath.constBegin(), end = m_resourceLocalUidByFilePath.constEnd(); it != end; ++it) {
        diagnostics += QStringLiteral("    [") + it.key() + QStringLiteral("]: ") + it.value() + QStringLiteral("\n");
    }

    diagnostics += QStringLiteral("  Watched files: \n");
    QStringList watchedFiles = m_fileSystemWatcher.files();
    const int numWatchedFiles = watchedFiles.size();
    for(int i = 0; i < numWatchedFiles; ++i) {
        diagnostics += QStringLiteral("    ") + watchedFiles[i] + QStringLiteral("\n");
    }

    diagnostics += QStringLiteral("}\n");

    Q_EMIT diagnosticsCollected(requestId, diagnostics);
}

void ResourceDataInTemporaryFileStorageManager::onFileChanged(const QString & path)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFileChanged: ") << path);

    auto it = m_resourceLocalUidByFilePath.find(path);

    QFileInfo resourceFileInfo(path);
    if (!resourceFileInfo.exists())
    {
        if (it != m_resourceLocalUidByFilePath.end()) {
            Q_UNUSED(m_resourceLocalUidByFilePath.erase(it));
        }

        m_fileSystemWatcher.removePath(path);
        QNINFO(QStringLiteral("Stopped watching for file ") << path << QStringLiteral(" as it was deleted"));

        return;
    }

    if (Q_UNLIKELY(it == m_resourceLocalUidByFilePath.end())) {
        QNWARNING(QStringLiteral("Can't process resource local file change properly: can't find resource local uid by file path: ")
                  << path << QStringLiteral("; stopped watching for that file's changes"));
        m_fileSystemWatcher.removePath(path);
        return;
    }

    QFile file(path);
    bool open = file.QIODevice::open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!open)) {
        QNWARNING(QStringLiteral("Can't process resource local file change properly: can't open resource file for reading: error code = ")
                  << file.error() << QStringLiteral(", error description: ") << file.errorString());
        m_fileSystemWatcher.removePath(path);
        return;
    }

    QByteArray data = file.readAll();
    QByteArray dataHash = calculateHash(data);

    int errorCode = 0;
    ErrorString errorDescription;
    bool res = updateResourceHash(it.value(), dataHash, resourceFileInfo.absolutePath(), errorCode, errorDescription);
    if (Q_UNLIKELY(!res)) {
        QNWARNING(QStringLiteral("Can't process resource local file change properly: can't update the hash for resource file: error code = ")
                  << errorCode << QStringLiteral(", error description: ") << errorDescription);
        m_fileSystemWatcher.removePath(path);
        return;
    }

    Q_EMIT resourceFileChanged(it.value(), path);
}

void ResourceDataInTemporaryFileStorageManager::onFileRemoved(const QString & path)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFileRemoved: ") << path);

    auto it = m_resourceLocalUidByFilePath.find(path);
    if (it != m_resourceLocalUidByFilePath.end()) {
        Q_UNUSED(m_resourceLocalUidByFilePath.erase(it));
    }
}

void ResourceDataInTemporaryFileStorageManager::onFoundResourceData(Resource resource)
{
    QString resourceLocalUid = resource.localUid();

    auto fit = m_resourceLocalUidsPendingFindInLocalStorage.find(resourceLocalUid);
    if (fit != m_resourceLocalUidsPendingFindInLocalStorage.end())
    {
        QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFoundResourceData: ") << resource);

        m_resourceLocalUidsPendingFindInLocalStorage.erase(fit);

        if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
            QNWARNING(QStringLiteral("Received resource data from local storage but no note is set to ResourceDataInTemporaryFileStorageManager"));
            return;
        }

        QString noteLocalUid = m_pCurrentNote->localUid();

        QByteArray dataHash = (resource.hasDataHash()
                               ? resource.dataHash()
                               : calculateHash(resource.dataBody()));

        ErrorString errorDescription;
        bool res = writeResourceDataToTemporaryFile(noteLocalUid, resourceLocalUid, resource.dataBody(), dataHash,
                                                    ResourceType::Image, errorDescription, CheckResourceFileActualityOption::Off);
        if (!res) {
            Q_EMIT failedToPutResourceDataIntoTemporaryFile(resourceLocalUid, noteLocalUid, errorDescription);
        }

        if (m_resourceLocalUidsPendingFindInLocalStorage.empty()) {
            QNDEBUG(QStringLiteral("Received and processed all image resources data for the current note, "
                                   "emitting noteResourcesReady signal: note local uid = ") << noteLocalUid);
            Q_EMIT noteResourcesReady(noteLocalUid);
        }
        else {
            QNDEBUG(QStringLiteral("Still pending ") << m_resourceLocalUidsPendingFindInLocalStorage.size()
                    << QStringLiteral(" resources data to be found within the local storage"));
        }

        return;
    }

    auto oit = m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.find(resourceLocalUid);
    if (oit != m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.end())
    {
        QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFoundResourceData (for resource file opening): ")
                << resource);

        m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.erase(oit);

        if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
            QNWARNING(QStringLiteral("Received resource data from local storage (for resource file opening) but no note "
                                     "is set to ResourceDataInTemporaryFileStorageManager"));
            return;
        }

        QString noteLocalUid = m_pCurrentNote->localUid();

        QByteArray dataHash = (resource.hasDataHash()
                               ? resource.dataHash()
                               : calculateHash(resource.dataBody()));

        WriteResourceDataCallback callback = OpenResourcePreparationProgressFunctor(resourceLocalUid, *this);

        bool isImageResource = (resource.hasMime() && resource.mime().startsWith(QStringLiteral("image")));
        ResourceType::type resourceType = (isImageResource ? ResourceType::Image : ResourceType::NonImage);

        ErrorString errorDescription;
        bool res = writeResourceDataToTemporaryFile(noteLocalUid, resourceLocalUid, resource.dataBody(), dataHash,
                                                    resourceType, errorDescription, CheckResourceFileActualityOption::Off,
                                                    callback);
        if (!res) {
            Q_EMIT failedToOpenResource(resourceLocalUid, noteLocalUid, errorDescription);
            return;
        }

        QString fileStoragePath = (isImageResource
                                   ? m_imageResourceFileStorageLocation
                                   : m_nonImageResourceFileStorageLocation);
        fileStoragePath += QStringLiteral("/") + noteLocalUid + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".dat");
        watchResourceFileForChanges(resourceLocalUid, fileStoragePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
        Q_EMIT openedResource(resourceLocalUid, noteLocalUid);
    }
}

void ResourceDataInTemporaryFileStorageManager::onFailedToFindResourceData(QString resourceLocalUid, ErrorString errorDescription)
{
    auto fit = m_resourceLocalUidsPendingFindInLocalStorage.find(resourceLocalUid);
    if (fit != m_resourceLocalUidsPendingFindInLocalStorage.end())
    {
        QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFailedToFindResourceData: resource local uid = ")
                << resourceLocalUid << QStringLiteral(", error description = ") << errorDescription);

        m_resourceLocalUidsPendingFindInLocalStorage.erase(fit);

        if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
            QNWARNING(QStringLiteral("Received failure to locate resource data within the local storage but no note "
                                     "is set to ResourceDataInTemporaryFileStorageManager"));
            return;
        }

        QString noteLocalUid = m_pCurrentNote->localUid();
        Q_EMIT failedToPutResourceDataIntoTemporaryFile(resourceLocalUid, noteLocalUid, errorDescription);

        if (m_resourceLocalUidsPendingFindInLocalStorage.empty()) {
            Q_EMIT noteResourcesReady(noteLocalUid);
        }
        else {
            QNDEBUG(QStringLiteral("Still pending ") << m_resourceLocalUidsPendingFindInLocalStorage.size()
                    << QStringLiteral(" resources data to be found within the local storage"));
        }

        return;
    }

    auto oit = m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.find(resourceLocalUid);
    if (oit != m_resourceLocalUidsPendingFindInLocalStorageForWritingToFileForOpening.end())
    {
        QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::onFailedToFindResourceData (for resource file "
                               "opening): resource local uid = ") << resourceLocalUid << QStringLiteral(", error description = ")
                << errorDescription);

        m_resourceLocalUidsPendingFindInLocalStorage.erase(oit);

        if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
            QNWARNING(QStringLiteral("Received failure to locate resource data within the local storage (for resource file "
                                    "opening) but no note is set to ResourceDataInTemporaryFileStorageManager"));

            return;
        }

        QString noteLocalUid = m_pCurrentNote->localUid();
        Q_EMIT failedToOpenResource(resourceLocalUid, noteLocalUid, errorDescription);
        return;
    }
}

void ResourceDataInTemporaryFileStorageManager::createConnections()
{
    QObject::connect(&m_fileSystemWatcher, QNSIGNAL(FileSystemWatcher,fileChanged,QString),
                     this, QNSLOT(ResourceDataInTemporaryFileStorageManager,onFileChanged,QString));

    NoteEditorLocalStorageBroker & noteEditorLocalStorageBroker = NoteEditorLocalStorageBroker::instance();
    QObject::connect(this, QNSIGNAL(ResourceDataInTemporaryFileStorageManager,findResourceData,QString),
                     &noteEditorLocalStorageBroker, QNSLOT(NoteEditorLocalStorageBroker,findResourceData,QString));
    QObject::connect(&noteEditorLocalStorageBroker, QNSIGNAL(NoteEditorLocalStorageBroker,foundResourceData,Resource),
                     this, QNSLOT(ResourceDataInTemporaryFileStorageManager,onFoundResourceData,Resource));
    QObject::connect(&noteEditorLocalStorageBroker, QNSIGNAL(NoteEditorLocalStorageBroker,failedToFindResourceData,QString,ErrorString),
                     this, QNSLOT(ResourceDataInTemporaryFileStorageManager,onFailedToFindResourceData,QString,ErrorString));
}

QByteArray ResourceDataInTemporaryFileStorageManager::calculateHash(const QByteArray & data) const
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

bool ResourceDataInTemporaryFileStorageManager::checkIfResourceFileExistsAndIsActual(const QString & noteLocalUid, const QString & resourceLocalUid,
                                                                                     const QString & fileStoragePath, const QByteArray & dataHash) const
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::checkIfResourceFileExistsAndIsActual: note local uid = ")
            << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid << QStringLiteral(", data hash = ")
            << dataHash.toHex());

    if (Q_UNLIKELY(fileStoragePath.isEmpty())) {
        QNWARNING(QStringLiteral("Resource file storage location is empty"));
        return false;
    }

    QFileInfo resourceFileInfo(fileStoragePath);
    if (!resourceFileInfo.exists()) {
        QNTRACE(QStringLiteral("Resource file for note local uid ") << noteLocalUid << QStringLiteral(" and resource local uid ")
                << resourceLocalUid << QStringLiteral(" does not exist"));
        return false;
    }

    QFileInfo resourceHashFileInfo(resourceFileInfo.absolutePath() + QStringLiteral("/") +
                                   resourceFileInfo.baseName() + QStringLiteral(".hash"));
    if (!resourceHashFileInfo.exists()) {
        QNTRACE(QStringLiteral("Resource hash file for note local uid ") << noteLocalUid << QStringLiteral(" and resource local uid ")
                << resourceLocalUid << QStringLiteral(" does not exist"));
        return false;
    }

    QFile resourceHashFile(resourceHashFileInfo.absoluteFilePath());
    bool open = resourceHashFile.open(QIODevice::ReadOnly);
    if (!open) {
        QNWARNING(QStringLiteral("Can't open resource hash file for reading"));
        return false;
    }

    QByteArray storedHash = resourceHashFile.readAll();
    if (storedHash != dataHash) {
        QNTRACE(QStringLiteral("Resource must be stale, the stored hash ") << storedHash.toHex()
                << QStringLiteral(" does not match the actual hash ") << dataHash.toHex());
        return false;
    }

    QNDEBUG(QStringLiteral("Resource file exists and is actual"));
    return true;
}

bool ResourceDataInTemporaryFileStorageManager::updateResourceHash(const QString & resourceLocalUid, const QByteArray & dataHash,
                                                                   const QString & storageFolderPath, int & errorCode,
                                                                   ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::updateResourceHash: resource local uid = ") << resourceLocalUid
            << QStringLiteral(", data hash = ") << dataHash.toHex() << QStringLiteral(", storage folder path = ") << storageFolderPath);

    QFile file(storageFolderPath + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".hash"));

    bool open = file.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!open)) {
        errorDescription.setBase(QT_TR_NOOP("Can't open the file with resource's hash for writing"));
        errorDescription.details() = file.errorString();
        errorCode = file.error();
        return false;
    }

    qint64 writeRes = file.write(dataHash);
    if (Q_UNLIKELY(writeRes < 0)) {
        errorDescription.setBase(QT_TR_NOOP("Can't write resource data hash to the separate file"));
        errorDescription.details() = file.errorString();
        errorCode = file.error();
        return false;
    }

    file.close();
    return true;
}

void ResourceDataInTemporaryFileStorageManager::watchResourceFileForChanges(const QString & resourceLocalUid, const QString & fileStoragePath)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::watchResourceFileForChanges: resource local uid = ")
            << resourceLocalUid << QStringLiteral(", file storage path = ") << fileStoragePath);

    m_fileSystemWatcher.addPath(fileStoragePath);
    QNINFO(QStringLiteral("Start watching for resource file ") << fileStoragePath);
}

void ResourceDataInTemporaryFileStorageManager::stopWatchingResourceFile(const QString & filePath)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::stopWatchingResourceFile: ") << filePath);

    auto it = m_resourceLocalUidByFilePath.find(filePath);
    if (it == m_resourceLocalUidByFilePath.end()) {
        QNTRACE(QStringLiteral("File is not being watched, nothing to do"));
        return;
    }

    m_fileSystemWatcher.removePath(filePath);
    QNTRACE(QStringLiteral("Stopped watching for file"));
}

void ResourceDataInTemporaryFileStorageManager::removeStaleResourceFilesFromCurrentNote()
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::removeStaleResourceFilesFromCurrentNote"));

    if (m_pCurrentNote.isNull()) {
        QNDEBUG(QStringLiteral("No current note, nothing to do"));
        return;
    }

    const QString & noteLocalUid = m_pCurrentNote->localUid();

    QList<Resource> resources = m_pCurrentNote->resources();
    const int numResources = resources.size();

    QFileInfoList fileInfoList;
    int numFiles = -1;

    QDir imageResourceFilesFolder(m_imageResourceFileStorageLocation + QStringLiteral("/") + m_pCurrentNote->localUid());
    if (imageResourceFilesFolder.exists())
    {
        fileInfoList = imageResourceFilesFolder.entryInfoList(QDir::Files);
        numFiles = fileInfoList.size();
        QNTRACE(QStringLiteral("Found ") << numFiles << QStringLiteral(" files wihin the image resource files folder for note with local uid ")
                << m_pCurrentNote->localUid());
    }

    QDir genericResourceImagesFolder(m_nonImageResourceFileStorageLocation + QStringLiteral("/") + m_pCurrentNote->localUid());
    if (genericResourceImagesFolder.exists())
    {
        QFileInfoList genericResourceImageFileInfos = genericResourceImagesFolder.entryInfoList(QDir::Files);
        int numGenericResourceImageFileInfos = genericResourceImageFileInfos.size();
        QNTRACE(QStringLiteral("Found ") << numGenericResourceImageFileInfos
                << QStringLiteral(" files within the generic resource files folder for note with local uid ")
                << m_pCurrentNote->localUid());

        fileInfoList.append(genericResourceImageFileInfos);
        numFiles = fileInfoList.size();
    }

    QNTRACE(QStringLiteral("Total ") << numFiles << QStringLiteral(" to check for staleness"));

    for(int i = 0; i < numFiles; ++i)
    {
        const QFileInfo & fileInfo = fileInfoList[i];
        QString filePath = fileInfo.absoluteFilePath();

        if (fileInfo.isSymLink()) {
            QNTRACE(QStringLiteral("Removing symlink file without any checks"));
            stopWatchingResourceFile(filePath);
            Q_UNUSED(removeFile(filePath))
            continue;
        }

        QString fullSuffix = fileInfo.completeSuffix();
        if (fullSuffix == QStringLiteral("hash")) {
            QNTRACE(QStringLiteral("Skipping .hash helper file ") << filePath);
            continue;
        }

        QString baseName = fileInfo.baseName();
        QNTRACE(QStringLiteral("Checking file with base name ") << baseName);

        int resourceIndex = -1;
        for(int j = 0; j < numResources; ++j)
        {
            QNTRACE(QStringLiteral("checking against resource with local uid ") << resources[j].localUid());
            if (baseName.startsWith(resources[j].localUid()))
            {
                QNTRACE(QStringLiteral("File ") << fileInfo.fileName() << QStringLiteral(" appears to correspond to resource ")
                        << resources[j].localUid());
                resourceIndex = j;
                break;
            }
        }

        if (resourceIndex >= 0)
        {
            const Resource & resource = resources[resourceIndex];
            if (resource.hasDataHash())
            {
                bool actual = checkIfResourceFileExistsAndIsActual(noteLocalUid, resource.localUid(), filePath, resource.dataHash());
                if (actual) {
                    QNTRACE(QStringLiteral("The resource file ") << filePath << QStringLiteral(" is still actual, will keep it"));
                    continue;
                }
            }
            else
            {
                QNTRACE(QStringLiteral("Resource at index ") << resourceIndex
                        << QStringLiteral(" doesn't have the data hash, will remove its resource file just in case"));
            }
        }

        QNTRACE(QStringLiteral("Found stale resource file ") << filePath << QStringLiteral(", removing it"));
        stopWatchingResourceFile(filePath);
        Q_UNUSED(removeFile(filePath))

        // Need to also remove the helper .hash file
        stopWatchingResourceFile(filePath);
        Q_UNUSED(removeFile(fileInfo.absolutePath() + QStringLiteral("/") + baseName + QStringLiteral(".hash")));
    }
}

ResourceDataInTemporaryFileStorageManager::ResultType::type
ResourceDataInTemporaryFileStorageManager::partialUpdateResourceFilesForCurrentNote(const QList<Resource> & previousResources,
                                                                                    ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::partialUpdateResourceFilesForCurrentNote"));

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        QNDEBUG(QStringLiteral("No current note, nothing to do"));
        return ResultType::Ready;
    }

    QList<Resource> newAndUpdatedResources;
    QStringList removedAndStaleResourceLocalUids;

    QList<Resource> resources = m_pCurrentNote->resources();
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;
        const QString resourceLocalUid = resource.localUid();
        QNTRACE(QStringLiteral("Examining resource with local uid ") << resourceLocalUid);

        const Resource * pPreviousResource = Q_NULLPTR;
        for(auto pit = previousResources.constBegin(), pend = previousResources.constEnd(); pit != pend; ++pit)
        {
            const Resource & previousResource = *pit;
            if (previousResource.localUid() == resourceLocalUid) {
                pPreviousResource = &previousResource;
                break;
            }
        }

        if (!pPreviousResource)
        {
            QNTRACE(QStringLiteral("No previous resource, considering the resource new: local uid = ") << resourceLocalUid);

            if (!resource.hasMime() || !resource.mime().startsWith(QStringLiteral("image"))) {
                QNTRACE(QStringLiteral("Resource has no mime type or mime type is not an image one, won't add the resource to the list of new ones"));
            }
            else {
                newAndUpdatedResources << resource;
            }

            continue;
        }

        QNTRACE(QStringLiteral("Previous resource's data size = ") << (pPreviousResource->hasDataSize() ? pPreviousResource->dataSize() : 0)
                << QStringLiteral(", updated resource's data size = ") << (resource.hasDataSize() ? resource.dataSize() : 0)
                << QStringLiteral("; previous resource's data hash = ") << (pPreviousResource->hasDataHash() ? pPreviousResource->dataHash().toHex() : QByteArray())
                << QStringLiteral(", updated resource's data hash = ") << (resource.hasDataHash() ? resource.dataHash().toHex() : QByteArray()));

        bool dataHashIsDifferent = (!pPreviousResource->hasDataHash() || !resource.hasDataHash() ||
                                    (pPreviousResource->dataHash() != resource.dataHash()));
        bool dataSizeIsDifferent = (!pPreviousResource->hasDataSize() || !resource.hasDataSize() ||
                                    (pPreviousResource->dataSize() != resource.dataSize()));

        if (dataHashIsDifferent || dataSizeIsDifferent)
        {
            QNTRACE(QStringLiteral("Different or missing data hash or size, considering the resource updated: local uid = ")
                    << resourceLocalUid);

            if (!resource.hasMime() || !resource.mime().startsWith(QStringLiteral("image"))) {
                QNTRACE(QStringLiteral("Resource has no mime type or mime type is not an image one, will remove the resource "
                                       "instead of adding it to the list of updated resources"));
                removedAndStaleResourceLocalUids << resourceLocalUid;
            }
            else {
                newAndUpdatedResources << resource;
            }

            continue;
        }
    }

    for(auto it = previousResources.constBegin(), end = previousResources.constEnd(); it != end; ++it)
    {
        const Resource & previousResource = *it;
        const QString resourceLocalUid = previousResource.localUid();

        const Resource * pResource = Q_NULLPTR;
        for(auto uit = resources.constBegin(), uend = resources.constEnd(); uit != uend; ++uit)
        {
            const Resource & resource = *uit;
            if (resource.localUid() == resourceLocalUid) {
                pResource = &resource;
                break;
            }
        }

        if (!pResource) {
            QNTRACE(QStringLiteral("Found no resource with local uid ") << resourceLocalUid
                    << QStringLiteral(" within the list of new/updated resources, considering it stale"));
            removedAndStaleResourceLocalUids << resourceLocalUid;
        }
    }

    QString noteLocalUid = m_pCurrentNote->localUid();

    QStringList dirsToCheck;
    dirsToCheck.reserve(2);
    dirsToCheck << (m_imageResourceFileStorageLocation + QStringLiteral("/") + noteLocalUid);
    dirsToCheck << (m_nonImageResourceFileStorageLocation + QStringLiteral("/") + noteLocalUid);

    for(auto dit = dirsToCheck.constBegin(), dend = dirsToCheck.constEnd(); dit != dend; ++dit)
    {
        QDir dir(*dit);
        if (!dir.exists()) {
            continue;
        }

        QDirIterator dirIterator(dir);
        while(dirIterator.hasNext())
        {
            QString entry = dirIterator.next();
            QFileInfo entryInfo(entry);
            if (!entryInfo.isFile()) {
                continue;
            }

            for(auto it = removedAndStaleResourceLocalUids.constBegin(),
                end = removedAndStaleResourceLocalUids.constEnd(); it != end; ++it)
            {
                if (!entry.startsWith(*it) || (entryInfo.completeSuffix() == (QStringLiteral("hash")))) {
                    continue;
                }

                stopWatchingResourceFile(dir.absoluteFilePath(entry));

                if (!removeFile(dir.absoluteFilePath(entry))) {
                    errorDescription.setBase(QT_TR_NOOP("Failed to remove stale temporary resource file"));
                    errorDescription.details() = QDir::toNativeSeparators(dir.absoluteFilePath(entry));
                    QNWARNING(errorDescription);
                    return ResultType::Error;
                }

                QString hashFile = entryInfo.baseName() + QStringLiteral(".hash");
                QFileInfo hashFileInfo(dir.absoluteFilePath(hashFile));
                if (hashFileInfo.exists() && !removeFile(hashFileInfo.absoluteFilePath())) {
                    errorDescription.setBase(QT_TR_NOOP("Failed to remove stale temporary resource's helper .hash file"));
                    errorDescription.details() = QDir::toNativeSeparators(dir.absoluteFilePath(entry));
                    QNWARNING(errorDescription);
                    return ResultType::Error;
                }
            }
        }
    }

    return putResourcesDataToTemporaryFiles(newAndUpdatedResources, errorDescription);
}

void ResourceDataInTemporaryFileStorageManager::emitPartialUpdateResourceFilesForCurrentNoteProgress(const double progress)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::emitPartialUpdateResourceFilesForCurrentNoteProgress: progress = ") << progress);

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        QNWARNING(QStringLiteral("Detected attempt to emit partial update resource files for current note progress but no current note is set; progress = ")
                  << progress);
        return;
    }

    Q_EMIT noteResourcesPreparationProgress(progress, m_pCurrentNote->localUid());
}

ResourceDataInTemporaryFileStorageManager::ResultType::type
ResourceDataInTemporaryFileStorageManager::putResourcesDataToTemporaryFiles(const QList<Resource> & resources,
                                                                            ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::putResourcesDataToTemporaryFiles: ")
            << resources.size() << QStringLiteral(" resources"));

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        errorDescription.setBase(QT_TR_NOOP("Can't put resources data into temporary files: internal error, "
                                            "no current note is set to ResourceDataInTemporaryFileStorageManager"));
        QNWARNING(errorDescription);
        return ResultType::Error;
    }

    size_t numResourcesPendingDataFromLocalStorage = 0;
    const int numNewAndUpdatedResources = resources.size();
    int newOrUpdatedResourceIndex = 0;
    for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
    {
        const Resource & resource = *it;
        if (!resource.hasDataBody()) {
            Q_UNUSED(m_resourceLocalUidsPendingFindInLocalStorage.insert(resource.localUid()))
            requestResourceDataFromLocalStorage(resource);
            ++numResourcesPendingDataFromLocalStorage;
            continue;
        }

        QByteArray dataHash = (resource.hasDataHash()
                               ? resource.dataHash()
                               : calculateHash(resource.dataBody()));

        WriteResourceDataCallback callback = PartialUpdateResourceFilesForCurrentNoteProgressFunctor(newOrUpdatedResourceIndex,
                                                                                                     numNewAndUpdatedResources,
                                                                                                     *this);

        bool res = writeResourceDataToTemporaryFile(m_pCurrentNote->localUid(), resource.localUid(),
                                                    resource.dataBody(), dataHash, ResourceType::Image,
                                                    errorDescription, CheckResourceFileActualityOption::On,
                                                    callback);
        if (!res) {
            Q_EMIT failedToPutResourceDataIntoTemporaryFile(resource.localUid(), m_pCurrentNote->localUid(), errorDescription);
        }

        ++newOrUpdatedResourceIndex;
    }

    if (numResourcesPendingDataFromLocalStorage > 0) {
        return ResultType::AsyncPending;
    }

    return ResultType::Ready;
}

void ResourceDataInTemporaryFileStorageManager::emitOpenResourcePreparationProgress(const double progress, const QString & resourceLocalUid)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::emitOpenResourcePreparationProgress: resource local uid = ")
            << resourceLocalUid << QStringLiteral(", progress = ") << progress);

    if (Q_UNLIKELY(m_pCurrentNote.isNull())) {
        QNWARNING(QStringLiteral("Detected attempt to emit open resource preparation progress but no current note is set; resource local uid = ")
                  << resourceLocalUid << QStringLiteral(", progress = ") << progress);
        return;
    }

    Q_EMIT openResourcePreparationProgress(progress, resourceLocalUid, m_pCurrentNote->localUid());
}

void ResourceDataInTemporaryFileStorageManager::requestResourceDataFromLocalStorage(const Resource & resource)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::requestResourceDataFromLocalStorage: resource local uid = ")
            << resource.localUid());
    Q_EMIT findResourceData(resource.localUid());
}

bool ResourceDataInTemporaryFileStorageManager::writeResourceDataToTemporaryFile(const QString & noteLocalUid, const QString & resourceLocalUid,
                                                                                 const QByteArray & data, const QByteArray & dataHash,
                                                                                 const ResourceType::type resourceType,
                                                                                 ErrorString & errorDescription,
                                                                                 const CheckResourceFileActualityOption::type checkActualityOption,
                                                                                 WriteResourceDataCallback callback)
{
    QNDEBUG(QStringLiteral("ResourceDataInTemporaryFileStorageManager::writeResourceDataToTemporaryFile: note local uid = ")
            << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid);

    if (Q_UNLIKELY(noteLocalUid.isEmpty())) {
        errorDescription.setBase(QT_TR_NOOP("Detected attempt to write resource data for empty note local uid to local file"));
        QNWARNING(errorDescription);
        return false;
    }

    if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
        errorDescription.setBase(QT_TR_NOOP("Detected attempt to write data for empty resource local uid to local file"));
        QNWARNING(errorDescription << QStringLiteral(", note local uid = ") << noteLocalUid);
        return false;
    }

    if (Q_UNLIKELY(data.isEmpty())) {
        errorDescription.setBase(QT_TR_NOOP("Detected attempt to write empty resource data to local file"));
        QNWARNING(errorDescription << QStringLiteral(", note local uid = ") << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid);
        return false;
    }

    QString fileStoragePath = ((resourceType == ResourceType::Image)
                               ? m_imageResourceFileStorageLocation
                               : m_nonImageResourceFileStorageLocation);
    fileStoragePath += QStringLiteral("/") + noteLocalUid + QStringLiteral("/") + resourceLocalUid + QStringLiteral(".dat");

    QFileInfo fileStoragePathInfo(fileStoragePath);
    QDir fileStorageDir(fileStoragePathInfo.absoluteDir());
    if (!fileStorageDir.exists())
    {
        bool createdDir = fileStorageDir.mkpath(fileStorageDir.absolutePath());
        if (!createdDir) {
            errorDescription.setBase(QT_TR_NOOP("Can't create folder to write the resource into"));
            QNWARNING(errorDescription << QStringLiteral(", note local uid = ") << noteLocalUid
                      << QStringLiteral(", resource local uid = ") << resourceLocalUid);
            return false;
        }
    }

    if (checkActualityOption == CheckResourceFileActualityOption::On)
    {
        bool actual = checkIfResourceFileExistsAndIsActual(noteLocalUid, resourceLocalUid, fileStoragePath,
                                                           (dataHash.isEmpty() ? calculateHash(data) : dataHash));
        if (actual) {
            QNTRACE(QStringLiteral("Skipping writing the resource to file as it is not necessary, the file already exists and is actual"));
            return true;
        }
    }

    QFile file(fileStoragePath);
    bool open = file.open(QIODevice::WriteOnly);
    if (Q_UNLIKELY(!open))
    {
        errorDescription.setBase(QT_TR_NOOP("Can't open resource file for writing"));
        errorDescription.details() = file.errorString();
        int errorCode = file.error();
        QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode << QStringLiteral(", note local uid = ")
                  << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid);
        return false;
    }

    if (!callback || (data.size() <= RESOURCE_DATA_BATCH_SIZE_IN_BYTES))
    {
        qint64 writeRes = file.write(data);
        if (Q_UNLIKELY(writeRes < 0))
        {
            errorDescription.setBase(QT_TR_NOOP("Can't write data to resource file"));
            errorDescription.details() = file.errorString();
            int errorCode = file.error();
            QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode << QStringLiteral(", note local uid = ")
                      << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid);
            return false;
        }
    }
    else
    {
        const char * rawData = data.constData();
        size_t offset = 0;
        double progress = 0;
        while(true)
        {
            qint64 writeRes = file.write(rawData, RESOURCE_DATA_BATCH_SIZE_IN_BYTES);
            if (Q_UNLIKELY(writeRes < 0)) {
                errorDescription.setBase(QT_TR_NOOP("Can't write data to resource file"));
                errorDescription.details() = file.errorString();
                int errorCode = file.error();
                QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode << QStringLiteral(", note local uid = ")
                          << noteLocalUid << QStringLiteral(", resource local uid = ") << resourceLocalUid);
                return false;
            }

            offset += static_cast<size_t>(writeRes);
            if (offset >= static_cast<size_t>(data.size())) {
                break;
            }

            rawData += writeRes;

            if (callback) {
                progress = static_cast<double>(offset) / data.size();
                callback(progress);
            }
        }
    }

    file.close();

    m_resourceLocalUidByFilePath[fileStoragePath] = resourceLocalUid;

    int errorCode = 0;
    bool res = updateResourceHash(resourceLocalUid, dataHash, fileStoragePathInfo.absolutePath(),
                                  errorCode, errorDescription);
    if (Q_UNLIKELY(!res)) {
        QNWARNING(errorDescription << QStringLiteral(", error code = ") << errorCode
                  << QStringLiteral(", resource local uid = ") << resourceLocalUid);
        return false;
    }

    QNDEBUG(QStringLiteral("Successfully wrote resource data to file: resource local uid = ") << resourceLocalUid
            << QStringLiteral(", file path = ") << fileStoragePath);
    return true;
}

} // namespace quentier
