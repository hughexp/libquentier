/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_DATA_USER_DATA_H
#define LIB_QUENTIER_TYPES_DATA_USER_DATA_H

#include <quentier/utility/Macros.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedData>

namespace quentier {

class Q_DECL_HIDDEN UserData: public QSharedData
{
public:
    UserData();
    UserData(const UserData & other);
    UserData(UserData && other);
    UserData(const qevercloud::User & user);
    virtual ~UserData();

    qevercloud::User    m_qecUser;
    bool                m_isLocal;
    bool                m_isDirty;

private:
    UserData & operator=(const UserData & other) = delete;
    UserData & operator=(UserData && other) = delete;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_USER_DATA_H
