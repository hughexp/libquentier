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

#ifndef LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H
#define LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H

#include <quentier/types/Account.h>
#include <QSharedData>

namespace quentier {

class Q_DECL_HIDDEN AccountData: public QSharedData
{
public:
    explicit AccountData();
    virtual ~AccountData();

    AccountData(const AccountData & other);
    AccountData(AccountData && other);

    void switchEvernoteAccountType(
        const Account::EvernoteAccountType::type evernoteAccountType);
    void setEvernoteAccountLimits(const qevercloud::AccountLimits & limits);

    QString                             m_name;
    QString                             m_displayName;
    Account::Type::type                 m_accountType;
    Account::EvernoteAccountType::type  m_evernoteAccountType;
    qevercloud::UserID                  m_userId;
    QString                             m_evernoteHost;
    QString                             m_shardId;
    qint32                              m_mailLimitDaily;
    qint64                              m_noteSizeMax;
    qint64                              m_resourceSizeMax;
    qint32                              m_linkedNotebookMax;
    qint32                              m_noteCountMax;
    qint32                              m_notebookCountMax;
    qint32                              m_tagCountMax;
    qint32                              m_noteTagCountMax;
    qint32                              m_savedSearchCountMax;
    qint32                              m_noteResourceCountMax;

    qint32 mailLimitDaily() const;
    qint64 noteSizeMax() const;
    qint64 resourceSizeMax() const;
    qint32 linkedNotebookMax() const;
    qint32 noteCountMax() const;
    qint32 notebookCountMax() const;
    qint32 tagCountMax() const;
    qint32 noteTagCountMax() const;
    qint32 savedSearchCountMax() const;
    qint32 noteResourceCountMax() const;

private:
    AccountData & operator=(const AccountData & other)  = delete;
    AccountData & operator=(AccountData && other)  = delete;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_ACCOUNT_DATA_H
