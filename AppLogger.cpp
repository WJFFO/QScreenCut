#include "AppLogger.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDate>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>

#include <cstdlib>

namespace {

// —— 日志模块的全局状态 ——
// 消息处理器可能从任意线程被调用，所有状态都用互斥锁保护
QMutex logMutex;
QFile logFile;
QDate logFileDate;     // 当前打开的日志文件对应的日期
QString logFolder;     // logs 文件夹的完整路径，init() 时确定
int keepDays = 7;

// Qt 消息类型对应的文字标签
QString typeToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO ");
    case QtWarningMsg:  return QStringLiteral("WARN ");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

// 打开指定日期的日志文件（追加模式）。当前有打开的文件先关掉
bool openLogFile(const QDate &date)
{
    if (logFile.isOpen()) {
        logFile.close();
    }

    logFile.setFileName(logFolder + QLatin1Char('/')
                        + date.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log"));

    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    logFileDate = date;
    return true;
}

// 删除超过保留天数的日志文件。
// 只认"日期.log"这种命名，别的文件（比如用户自己放的说明）不会动
void cleanupOldLogs()
{
    QDate today = QDate::currentDate();
    const QDate expireBefore = today.addDays(-keepDays);

    QDir dir(logFolder);
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("????-??-??.log")}, QDir::Files);

    for (const QFileInfo &info : files) {
        const QDate fileDate =
            QDate::fromString(info.baseName(), QStringLiteral("yyyy-MM-dd"));

        // 文件名不是合法日期的跳过，正常日期且过期的删掉
        if (!fileDate.isValid()) {
            continue;
        }
        if (fileDate < expireBefore) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

// 消息处理器：qDebug/qWarning 等的输出都汇到这里。
// 多线程下 Qt 不保证串行，用互斥锁保证一行日志完整写入
void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(context)

    // 致命错误也走正常写文件流程，写完再终止进程
    QMutexLocker locker(&logMutex);

    const QDateTime now = QDateTime::currentDateTime();

    // 日期变了（跨天或首次调用）就切换日志文件。
    // 打开失败就静默放弃：日志只是辅助手段，不能因为它把程序搞挂
    if (!logFile.isOpen() || logFileDate != now.date()) {
        openLogFile(now.date());
    }

    if (logFile.isOpen()) {
        QTextStream stream(&logFile);
        stream << QStringLiteral("[%1] [%2] %3\n")
                    .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                         typeToString(type), message);
        stream.flush();
    }

    // 致命错误写完日志后要自己结束进程，否则 Qt 会再次调用处理器导致递归
    if (type == QtFatalMsg) {
        ::abort();
    }
}

}  // namespace

void AppLogger::init(int daysToKeep)
{
    // logs 文件夹放在 exe 同级目录，和 data 一样跟着程序走
    logFolder = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir().mkpath(logFolder);

    keepDays = daysToKeep;

    // 兜底先清一次：程序不是每天都开，上次退出前攒下的旧日志在这里处理
    cleanupOldLogs();

    // 定时清理：每 6 小时跑一遍。周期不用太短，
    // 过期判断按天算，早一小时晚一小时没有区别。
    // 定时器没有 parent（local 变量出函数就没了），故意让它常驻到进程结束
    auto cleanupTimer = new QTimer;
    cleanupTimer->setInterval(6 * 60 * 60 * 1000);
    QObject::connect(cleanupTimer, &QTimer::timeout, cleanupOldLogs);
    cleanupTimer->start();

    qInstallMessageHandler(logMessageHandler);
}
