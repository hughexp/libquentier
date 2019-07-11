/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include "GenericResourceImageManager.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Utility.h>
#include <quentier/types/Resource.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>

namespace quentier {

GenericResourceImageManager::GenericResourceImageManager(QObject * parent) :
    QObject(parent),
    m_storageFolderPath(),
    m_pCurrentNote()
{}

void GenericResourceImageManager::setStorageFolderPath(
    const QString & storageFolderPath)
{
    QNDEBUG("GenericResourceImageManager::setStorageFolderPath: "
            << storageFolderPath);
    m_storageFolderPath = storageFolderPath;
}

void GenericResourceImageManager::onGenericResourceImageWriteRequest(
    QString noteLocalUid, QString resourceLocalUid,
    QByteArray resourceImageData, QString resourceFileSuffix,
    QByteArray resourceActualHash, QString resourceDisplayName,
    QUuid requestId)
{
    QNDEBUG("GenericResourceImageManager::"
            << "onGenericResourceImageWriteRequest: note local uid = "
            << noteLocalUid << ", resource local uid = "
            << resourceLocalUid << ", resource actual hash = "
            << resourceActualHash.toHex() << ", request id = "
            << requestId);

#define RETURN_WITH_ERROR(message)                                             \
    ErrorString errorDescription(message);                                     \
    QNWARNING(errorDescription);                                               \
    Q_EMIT genericResourceImageWriteReply(/* success = */ false, QByteArray(), \
                                          QString(), errorDescription,         \
                                          requestId);                          \
    return                                                                     \
// RETURN_WITH_ERROR

    if (Q_UNLIKELY(m_storageFolderPath.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Storage folder path is empty"));
    }

    if (Q_UNLIKELY(noteLocalUid.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Note local uid is empty"));
    }

    if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource local uid is empty"));
    }

    if (Q_UNLIKELY(resourceActualHash.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource hash is empty"));
    }

    if (Q_UNLIKELY(resourceFileSuffix.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource image file suffix is empty"));
    }

    QString resourceFileNameMask = resourceLocalUid +
                                   QStringLiteral("*.") +
                                   resourceFileSuffix;
    QDir storageDir(m_storageFolderPath + QStringLiteral("/") + noteLocalUid);
    if (Q_UNLIKELY(!storageDir.exists()))
    {
        bool res = storageDir.mkpath(storageDir.absolutePath());
        if (!res) {
            RETURN_WITH_ERROR(QT_TR_NOOP("Can't create the folder to store "
                                         "the resource image in"));
        }
    }

    QStringList nameFilter = QStringList() << resourceFileNameMask;
    QFileInfoList existingResourceImageFileInfos =
        storageDir.entryInfoList(nameFilter,
                                 QDir::Files |
                                 QDir::Readable |
                                 QDir::NoDotAndDotDot);

    bool resourceHashChanged = true;
    QFileInfo resourceHashFileInfo(storageDir.absolutePath() + QStringLiteral("/") +
                                   resourceLocalUid + QStringLiteral(".hash"));
    bool resourceHashFileExists = resourceHashFileInfo.exists();
    if (resourceHashFileExists)
    {
        if (Q_UNLIKELY(!resourceHashFileInfo.isWritable())) {
            RETURN_WITH_ERROR(QT_TR_NOOP("Resource hash file is not writable"));
        }

        if (resourceHashFileInfo.isReadable())
        {
            QFile resourceHashFile(resourceHashFileInfo.absoluteFilePath());
            Q_UNUSED(resourceHashFile.open(QIODevice::ReadOnly));
            QByteArray previousResourceHash = resourceHashFile.readAll();

            if (resourceActualHash == previousResourceHash) {
                QNTRACE("Resource hash hasn't changed");
                resourceHashChanged = false;
            }
        }
    }

    bool resourceDisplayNameChanged = false;
    QFileInfo resourceNameFileInfo(storageDir.absolutePath() + QStringLiteral("/") +
                                   resourceLocalUid + QStringLiteral(".name"));
    if (!resourceHashChanged)
    {
        bool resourceNameFileExists = resourceNameFileInfo.exists();
        if (resourceNameFileExists)
        {
            if (Q_UNLIKELY(!resourceNameFileInfo.isWritable())) {
                RETURN_WITH_ERROR(QT_TR_NOOP("Resource name file is not writable"));
            }

            if (Q_UNLIKELY(!resourceNameFileInfo.isReadable()))
            {
                QNINFO("Helper file with resource name for generic "
                       << "resource image is not readable: "
                       << resourceNameFileInfo.absoluteFilePath()
                       << " which is quite strange...");
                resourceDisplayNameChanged = true;
            }
            else
            {
                QFile resourceNameFile(resourceNameFileInfo.absoluteFilePath());
                Q_UNUSED(resourceNameFile.open(QIODevice::ReadOnly));
                QString previousResourceName =
                    QString::fromLocal8Bit(resourceNameFile.readAll());

                if (resourceDisplayName != previousResourceName) {
                    QNTRACE("Resource display name has changed from "
                            << previousResourceName << " to "
                            << resourceDisplayName);
                    resourceDisplayNameChanged = true;
                }
            }
        }
    }

    if (!resourceHashChanged && !resourceDisplayNameChanged &&
        !existingResourceImageFileInfos.isEmpty())
    {
        QNDEBUG("resource hash and display name haven't changed, "
                "won't rewrite the resource's image");
        Q_EMIT genericResourceImageWriteReply(
            /* success = */ true, resourceActualHash,
            existingResourceImageFileInfos.front().absoluteFilePath(),
            ErrorString(), requestId);
        return;
    }

    QNTRACE("Writing resource image file and helper files with "
            "hash and display name");

    QString resourceImageFilePath =
        storageDir.absolutePath() + QStringLiteral("/") +
        resourceLocalUid + QStringLiteral("_") +
        QString::number(QDateTime::currentMSecsSinceEpoch()) +
        QStringLiteral(".") + resourceFileSuffix;

    QFile resourceImageFile(resourceImageFilePath);
    if (Q_UNLIKELY(!resourceImageFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Can't open resource image file for writing"));
    }
    resourceImageFile.write(resourceImageData);
    resourceImageFile.close();

    QFile resourceHashFile(resourceHashFileInfo.absoluteFilePath());
    if (Q_UNLIKELY(!resourceHashFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Can't open resource hash file for writing"));
    }
    resourceHashFile.write(resourceActualHash);
    resourceHashFile.close();

    QFile resourceNameFile(resourceNameFileInfo.absoluteFilePath());
    if (Q_UNLIKELY(!resourceNameFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Can't open resource name file for writing"));
    }
    resourceNameFile.write(resourceDisplayName.toLocal8Bit());
    resourceNameFile.close();

    QNTRACE("Successfully wrote resource image file and helper "
            << "files with hash and display name for request "
            << requestId << ", resource image file path = "
            << resourceImageFilePath);

    Q_EMIT genericResourceImageWriteReply(
        /* success = */ true, resourceActualHash,
        resourceImageFilePath, ErrorString(), requestId);

    if (!existingResourceImageFileInfos.isEmpty())
    {
        const int numStaleResourceImageFiles = existingResourceImageFileInfos.size();
        for(int i = 0; i < numStaleResourceImageFiles; ++i)
        {
            QFileInfo & staleResourceImageFileInfo = existingResourceImageFileInfos[i];
            QFile staleResourceImageFile(staleResourceImageFileInfo.absoluteFilePath());
            bool res = staleResourceImageFile.remove();
            if (Q_UNLIKELY(!res)) {
                QNINFO("Can't remove stale generic resource image file: "
                       << staleResourceImageFile.errorString()
                       << " (error code = " << staleResourceImageFile.error() << ")");
            }
        }
    }
}

void GenericResourceImageManager::onCurrentNoteChanged(Note note)
{
    QNDEBUG("GenericResourceImageManager::onCurrentNoteChanged: "
            << "new note local uid = " << note.localUid()
            << ", previous note local uid = "
            << (m_pCurrentNote.isNull()
                ? QStringLiteral("<null>")
                : m_pCurrentNote->localUid()));

    if (!m_pCurrentNote.isNull() && (m_pCurrentNote->localUid() == note.localUid()))
    {
        QNTRACE("The current note is the same, only the note "
                "object might have changed");
        *m_pCurrentNote = note;
        removeStaleGenericResourceImageFilesFromCurrentNote();
        return;
    }

    removeStaleGenericResourceImageFilesFromCurrentNote();

    if (m_pCurrentNote.isNull()) {
        m_pCurrentNote.reset(new Note(note));
    }
    else {
        *m_pCurrentNote = note;
    }
}

void GenericResourceImageManager::removeStaleGenericResourceImageFilesFromCurrentNote()
{
    QNDEBUG("GenericResourceImageManager::"
            "removeStaleGenericResourceImageFilesFromCurrentNote");

    if (m_pCurrentNote.isNull()) {
        QNDEBUG("No current note, nothing to do");
        return;
    }

    const QString & noteLocalUid = m_pCurrentNote->localUid();

    QDir storageDir(m_storageFolderPath + QStringLiteral("/") + noteLocalUid);
    if (!storageDir.exists()) {
        QNTRACE("Storage dir " << storageDir.absolutePath()
                << " does not exist, nothing to do");
        return;
    }

    QList<Resource> resources = m_pCurrentNote->resources();
    const int numResources = resources.size();

    QFileInfoList fileInfoList = storageDir.entryInfoList(QDir::Files);
    int numFiles = fileInfoList.size();

    QNTRACE("Will check " << numFiles
            << " generic resource image files for staleness");

    for(int i = 0; i < numFiles; ++i)
    {
        const QFileInfo & fileInfo = fileInfoList[i];
        QString filePath = fileInfo.absoluteFilePath();

        QString fullSuffix = fileInfo.completeSuffix();
        if (fullSuffix == QStringLiteral("hash")) {
            QNTRACE("Skipping .hash helper file " << filePath);
            continue;
        }

        QString baseName = fileInfo.baseName();
        QNTRACE("Checking file with base name " << baseName);

        int resourceIndex = -1;
        for(int j = 0; j < numResources; ++j)
        {
            QNTRACE("checking against resource with local uid "
                    << resources[j].localUid());
            if (baseName.startsWith(resources[j].localUid())) {
                QNTRACE("File " << fileInfo.fileName()
                        << " appears to correspond to resource "
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
                QFileInfo helperHashFileInfo(
                    fileInfo.absolutePath() + QStringLiteral("/") +
                    resource.localUid() + QStringLiteral(".hash"));

                if (helperHashFileInfo.exists())
                {
                    QFile helperHashFile(helperHashFileInfo.absoluteFilePath());
                    Q_UNUSED(helperHashFile.open(QIODevice::ReadOnly))
                    QByteArray storedHash = helperHashFile.readAll();
                    if (storedHash == resource.dataHash()) {
                        QNTRACE("Resource file " << filePath
                                << " appears to be still actual, will keep it");
                        continue;
                    }
                    else {
                        QNTRACE("The stored hash doesn't match "
                                << "the actual resource data hash: "
                                << "stored = " << storedHash.toHex()
                                << ", actual = "
                                << resource.dataHash().toHex());
                    }
                }
                else
                {
                    QNTRACE("Helper hash file "
                            << helperHashFileInfo.absoluteFilePath()
                            << " does not exist");
                }
            }
            else
            {
                QNTRACE("Resource at index " << resourceIndex
                        << " doesn't have the data hash, will remove "
                        << "its resource file just in case");
            }
        }

        QNTRACE("Found stale generic resource image file "
                << filePath << ", removing it");
        Q_UNUSED(removeFile(filePath))

        // Need to also remove the helper .hash file
        Q_UNUSED(removeFile(fileInfo.absolutePath() + QStringLiteral("/") +
                            baseName + QStringLiteral(".hash")));
    }
}

} // namespace quentier
