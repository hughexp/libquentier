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

#include "SpellCheckerDynamicHelper.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

SpellCheckerDynamicHelper::SpellCheckerDynamicHelper(QObject * parent) :
    QObject(parent)
{}

#ifdef QUENTIER_USE_QT_WEB_ENGINE
void SpellCheckerDynamicHelper::setLastEnteredWords(QVariant words)
{
    QNDEBUG(QStringLiteral("SpellCheckerDynamicHelper::setLastEnteredWords: ") << words);

    QStringList wordsList = words.toStringList();
    Q_EMIT lastEnteredWords(wordsList);
}
#else
void SpellCheckerDynamicHelper::setLastEnteredWords(QVariantList words)
{
    QNDEBUG(QStringLiteral("SpellCheckerDynamicHelper::setLastEnteredWords: ") << words);

    QStringList wordsList;
    for(auto it = words.begin(), end = words.end(); it != end; ++it) {
        const QVariant & word = *it;
        wordsList << word.toString();
    }
    Q_EMIT lastEnteredWords(wordsList);
}
#endif

} // namespace quentier
