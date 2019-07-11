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

#include <quentier/logging/QuentierLogger.h>
#include "QuentierLogger_p.h"
#include <QCoreApplication>

namespace quentier {

void QuentierInitializeLogging()
{
    QuentierLogger & logger = QuentierLogger::instance();
    Q_UNUSED(logger)
}

void QuentierAddLogEntry(const QString & sourceFileName, const int sourceFileLineNumber,
                         const QString & message, const LogLevel::type logLevel)
{
    QString relativeSourceFileName = sourceFileName;
    int prefixIndex = relativeSourceFileName.indexOf(QStringLiteral("libquentier"),
                                                     Qt::CaseInsensitive);
    if (prefixIndex >= 0) {
        relativeSourceFileName.remove(0, prefixIndex);
    }
    else {
        QString appName = QCoreApplication::applicationName().toLower();
        prefixIndex = relativeSourceFileName.indexOf(appName, Qt::CaseInsensitive);
        if (prefixIndex >= 0) {
            relativeSourceFileName.remove(0, prefixIndex + appName.size() + 1);
        }
    }

    QString logEntry = relativeSourceFileName;
    logEntry += QStringLiteral(QNLOG_FILE_LINENUMBER_DELIMITER);
    logEntry += QString::number(sourceFileLineNumber);
    logEntry += QStringLiteral(" [");

    switch(logLevel)
    {
    case LogLevel::TraceLevel:
        logEntry += QStringLiteral("Trace]: ");
        break;
    case LogLevel::DebugLevel:
        logEntry += QStringLiteral("Debug]: ");
        break;
    case LogLevel::InfoLevel:
        logEntry += QStringLiteral("Info]: ");
        break;
    case LogLevel::WarnLevel:
        logEntry += QStringLiteral("Warn]: ");
        break;
    case LogLevel::ErrorLevel:
        logEntry += QStringLiteral("Error]: ");
        break;
    default:
        logEntry += QStringLiteral("Unknown log level: ") +
            QString::number(logLevel) + QStringLiteral("]: ");
        break;
    }

    logEntry += message;

    QuentierLogger & logger = QuentierLogger::instance();
    logger.write(logEntry);
}

LogLevel::type QuentierMinLogLevel()
{
    QuentierLogger & logger = QuentierLogger::instance();
    return logger.minLogLevel();
}

void QuentierSetMinLogLevel(const LogLevel::type logLevel)
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.setMinLogLevel(logLevel);
}

bool QuentierIsLogLevelActive(const LogLevel::type logLevel)
{
    return (QuentierLogger::instance().minLogLevel() <= logLevel);
}

void QuentierAddStdOutLogDestination()
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.addLogWriter(new QuentierConsoleLogWriter);
}

QString QuentierLogFilesDirPath()
{
    return QuentierLogger::logFilesDirPath();
}

void QuentierRestartLogging()
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.restartLogging();
}

} // namespace quentier
