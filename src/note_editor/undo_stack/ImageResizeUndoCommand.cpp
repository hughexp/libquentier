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

#include "ImageResizeUndoCommand.h"
#include "../NoteEditor_p.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditorPrivate.page()); \
    if (Q_UNLIKELY(!page)) { \
        ErrorString error(QT_TR_NOOP("Can't undo/redo image resizing: can't get note editor page")); \
        QNWARNING(error); \
        Q_EMIT notifyError(error); \
        return; \
    }

ImageResizeUndoCommand::ImageResizeUndoCommand(NoteEditorPrivate & noteEditor, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent)
{}

ImageResizeUndoCommand::ImageResizeUndoCommand(NoteEditorPrivate & noteEditor, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent)
{}

ImageResizeUndoCommand::~ImageResizeUndoCommand()
{}

void ImageResizeUndoCommand::redoImpl()
{
    QNDEBUG(QStringLiteral("ImageResizeUndoCommand::redoImpl"));

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("resizableImageManager.redo();"));
}

void ImageResizeUndoCommand::undoImpl()
{
    QNDEBUG(QStringLiteral("ImageResizeUndoCommand::undoImpl"));

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("resizableImageManager.undo();"));
}

} // namespace quentier
