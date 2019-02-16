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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_TO_DO_CHECKBOX_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_TO_DO_CHECKBOX_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"

namespace quentier {

class Q_DECL_HIDDEN ToDoCheckboxUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    ToDoCheckboxUndoCommand(const quint64 enToDoCheckboxId,
                            NoteEditorPrivate & noteEditorPrivate,
                            QUndoCommand * parent = Q_NULLPTR);
    ToDoCheckboxUndoCommand(const quint64 enToDoCheckboxId,
                            NoteEditorPrivate & noteEditorPrivate,
                            const QString & text,
                            QUndoCommand * parent = Q_NULLPTR);
    virtual ~ToDoCheckboxUndoCommand();

    virtual void redoImpl() Q_DECL_OVERRIDE;
    virtual void undoImpl() Q_DECL_OVERRIDE;

private:
    quint64     m_enToDoCheckboxId;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_TO_DO_CHECKBOX_UNDO_COMMAND_H
