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

#ifndef LIB_QUENTIER_TYPES_DATA_SAVED_SEARCH_DATA_H
#define LIB_QUENTIER_TYPES_DATA_SAVED_SEARCH_DATA_H

#include "FavoritableDataElementData.h"

#include <quentier/types/ErrorString.h>

#include <qt5qevercloud/QEverCloud.h>

namespace quentier {

class Q_DECL_HIDDEN SavedSearchData: public FavoritableDataElementData
{
public:
    SavedSearchData();
    SavedSearchData(const SavedSearchData & other);
    SavedSearchData(SavedSearchData && other);
    SavedSearchData(const qevercloud::SavedSearch & other);
    SavedSearchData(qevercloud::SavedSearch && other);
    virtual ~SavedSearchData() override;

    void clear();
    bool checkParameters(ErrorString & errorDescription) const;

    bool operator==(const SavedSearchData & other) const;
    bool operator!=(const SavedSearchData & other) const;

    qevercloud::SavedSearch     m_qecSearch;

private:
    SavedSearchData & operator=(const SavedSearchData & other) = delete;
    SavedSearchData & operator=(SavedSearchData && other) = delete;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_SAVED_SEARCH_DATA_H
