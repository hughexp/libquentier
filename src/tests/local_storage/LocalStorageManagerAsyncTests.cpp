/*
 * Copyright 2019 Dmitry Ivanov
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

#include "LocalStorageManagerAsyncTests.h"
#include "../TestMacros.h"
#include "LinkedNotebookLocalStorageManagerAsyncTester.h"
#include "LocalStorageCacheAsyncTester.h"
#include "NotebookLocalStorageManagerAsyncTester.h"
#include "NoteLocalStorageManagerAsyncTester.h"
#include "NoteNotebookAndTagListTrackingAsyncTester.h"
#include "ResourceLocalStorageManagerAsyncTester.h"
#include "SavedSearchLocalStorageManagerAsyncTester.h"
#include "TagLocalStorageManagerAsyncTester.h"
#include "UserLocalStorageManagerAsyncTester.h"
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <QTimer>

namespace quentier {
namespace test {

void TestSavedSearhAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        SavedSearchLocalStorageManagerAsyncTester savedSearchAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&savedSearchAsyncTester,
                         QNSIGNAL(SavedSearchLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&savedSearchAsyncTester,
                         QNSIGNAL(SavedSearchLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &savedSearchAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "SavedSearch async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("SavedSearch async tester failed to finish in time");
    }
}

void TestLinkedNotebookAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        LinkedNotebookLocalStorageManagerAsyncTester linkedNotebookAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&linkedNotebookAsyncTester,
                         QNSIGNAL(LinkedNotebookLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&linkedNotebookAsyncTester,
                         QNSIGNAL(LinkedNotebookLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &linkedNotebookAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "LinkedNotebook async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("LinkedNotebook async tester failed to finish in time");
    }
}

void TestTagAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        TagLocalStorageManagerAsyncTester tagAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&tagAsyncTester,
                         QNSIGNAL(TagLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&tagAsyncTester,
                         QNSIGNAL(TagLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &tagAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in Tag "
              "async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Tag async tester failed to finish in time");
    }
}

void TestUserAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        UserLocalStorageManagerAsyncTester userAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&userAsyncTester,
                         QNSIGNAL(UserLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&userAsyncTester,
                         QNSIGNAL(UserLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &userAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "User async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("User async tester failed to finish in time");
    }
}

void TestNotebookAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        NotebookLocalStorageManagerAsyncTester notebookAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&notebookAsyncTester,
                         QNSIGNAL(NotebookLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&notebookAsyncTester,
                         QNSIGNAL(NotebookLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &notebookAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "Notebook async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Notebook async tester failed to finish in time");
    }
}

void TestNoteAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        NoteLocalStorageManagerAsyncTester noteAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&noteAsyncTester,
                         QNSIGNAL(NoteLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&noteAsyncTester,
                         QNSIGNAL(NoteLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &noteAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "Note async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Note async tester failed to finish in time");
    }
}

void TestResourceAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        ResourceLocalStorageManagerAsyncTester resourceAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&resourceAsyncTester,
                         QNSIGNAL(ResourceLocalStorageManagerAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&resourceAsyncTester,
                         QNSIGNAL(ResourceLocalStorageManagerAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &resourceAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "Resource async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Resource async tester failed to finish in time");
    }
}

void TestNoteNotebookAndTagListTrackingAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString errorDescription;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        NoteNotebookAndTagListTrackingAsyncTester noteNotebookAndTagListTrackingAsycTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&noteNotebookAndTagListTrackingAsycTester,
                         QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&noteNotebookAndTagListTrackingAsycTester,
                         QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &noteNotebookAndTagListTrackingAsycTester,
                                     SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec());
        status = loop.exitStatus();
        errorDescription = loop.errorDescription();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure)
    {
        QFAIL(qPrintable(QString::fromUtf8(
            "Detected failure during the asynchronous "
            "loop processing in Note notebook and tag "
            "list tracking async tester: ") +
            errorDescription.nonLocalizedString()));
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout)
    {
        QFAIL("Note notebook and tag list tracking async tester failed to "
              "finish in time");
    }
}

void TestCacheAsync()
{
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        LocalStorageCacheAsyncTester localStorageCacheAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&localStorageCacheAsyncTester,
                         QNSIGNAL(LocalStorageCacheAsyncTester,success),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&localStorageCacheAsyncTester,
                         QNSIGNAL(LocalStorageCacheAsyncTester,failure,QString),
                         &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &localStorageCacheAsyncTester, SLOT(onInitTestCase()));
        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in "
              "local storage cache async tester");
    }
    else if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Local storage cache async tester failed to finish in time");
    }
}

} // namespace test
} // namespace quentier
