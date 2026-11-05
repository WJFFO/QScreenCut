#include "ClipboardStore.h"

#include "ThumbnailUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

namespace {

// 连接名固定，避免每次 addDatabase 都产生一个新连接
const char *const CONNECTION_NAME = "clipboard";

}

bool openClipboardDatabase()
{
    // 已打开的连接直接复用，重复调用是安全的
    if (QSqlDatabase::contains(CONNECTION_NAME)) {
        QSqlDatabase existing = QSqlDatabase::database(CONNECTION_NAME);
        if (existing.isOpen()) {
            return true;
        }
    }

    // 数据库和配置文件放在一起，都在程序目录下的 data 子目录里
    const QString dataDirectory = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    QDir().mkpath(dataDirectory);

    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", CONNECTION_NAME);
    database.setDatabaseName(dataDirectory + "/clipboard.db");
    if (!database.open()) {
        qWarning() << "打开剪贴板数据库失败:" << database.lastError().text();
        return false;
    }

    QSqlQuery query(database);
    const QString createTable =
        "CREATE TABLE IF NOT EXISTS clipboard_item ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  kind TEXT NOT NULL,"
        "  content BLOB NOT NULL,"
        "  preview TEXT NOT NULL,"
        "  created_at TEXT NOT NULL)";
    if (!query.exec(createTable)) {
        qWarning() << "创建剪贴板表失败:" << query.lastError().text();
        return false;
    }

    return true;
}

void closeClipboardDatabase()
{
    if (!QSqlDatabase::contains(CONNECTION_NAME)) {
        return;
    }

    {
        // QSqlDatabase 对象必须先析构，才能安全地移除连接
        QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
        database.close();
    }
    QSqlDatabase::removeDatabase(CONNECTION_NAME);
}

qint64 insertClipboardEntry(const ClipboardEntry &entry)
{
    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);

    // 连续复制同一份内容很常见（比如反复 Ctrl+C 同一段文字），
    // 这里只和最新一条比较，相同就刷新时间，避免历史列表被重复项占满。
    QSqlQuery latestQuery(database);
    if (latestQuery.exec("SELECT id, kind, content FROM clipboard_item ORDER BY id DESC LIMIT 1")
        && latestQuery.next()) {
        const QString latestKind = latestQuery.value(1).toString();
        const QByteArray latestContent = latestQuery.value(2).toByteArray();
        if (latestKind == entry.kind && latestContent == entry.content) {
            const qint64 latestId = latestQuery.value(0).toLongLong();

            QSqlQuery touchQuery(database);
            touchQuery.prepare("UPDATE clipboard_item SET created_at = ? WHERE id = ?");
            touchQuery.addBindValue(entry.createdAt.toString(Qt::ISODate));
            touchQuery.addBindValue(latestId);
            touchQuery.exec();
            return latestId;
        }
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare("INSERT INTO clipboard_item (kind, content, preview, created_at)"
                        " VALUES (?, ?, ?, ?)");
    insertQuery.addBindValue(entry.kind);
    insertQuery.addBindValue(entry.content);
    insertQuery.addBindValue(entry.preview);
    insertQuery.addBindValue(entry.createdAt.toString(Qt::ISODate));

    if (!insertQuery.exec()) {
        qWarning() << "写入剪贴板记录失败:" << insertQuery.lastError().text();
        return -1;
    }

    return insertQuery.lastInsertId().toLongLong();
}

QVector<ClipboardEntry> loadClipboardEntries(int limit, int offset)
{
    QVector<ClipboardEntry> entries;

    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
    QSqlQuery query(database);

    // 列表需要摘要加上一小段内容，几千条记录也不会拖慢窗口。
    // 文本截断到 400 字节，免得把整份日志读出来；
    // 图片和文件则要整段：图片的 content 本身就是缩略图，文件要整串路径
    // 才能挑出里面第一个图片 / 视频
    query.prepare("SELECT id, kind, preview, created_at,"
                  " CASE WHEN kind IN ('image', 'files') THEN content"
                  "      ELSE substr(content, 1, 400) END"
                  " FROM clipboard_item ORDER BY id DESC LIMIT ? OFFSET ?");
    query.addBindValue(limit);
    query.addBindValue(offset);

    if (!query.exec()) {
        qWarning() << "读取剪贴板记录失败:" << query.lastError().text();
        return entries;
    }

    while (query.next()) {
        ClipboardEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.kind = query.value(1).toString();
        entry.preview = query.value(2).toString();
        entry.createdAt = QDateTime::fromString(query.value(3).toString(), Qt::ISODate);
        entry.content = query.value(4).toByteArray();

        if (entry.kind == QStringLiteral("files")) {
            // content 是 JSON 路径数组，形如 ["D:\\a\\1.png","D:\\a\\2.pdf"]。
            // 第一个路径决定列表显示哪个类型徽章，
            // 第一个图片 / 视频决定列表用哪张缩略图
            const QJsonArray paths = QJsonDocument::fromJson(entry.content).array();

            for (const QJsonValue &value : paths) {
                const QString path = value.toString();
                if (path.isEmpty()) {
                    continue;
                }

                if (entry.firstFilePath.isEmpty()) {
                    entry.firstFilePath = path;
                    entry.fileExtension = QFileInfo(path).suffix().toLower();
                }

                if (entry.previewFilePath.isEmpty() && isPreviewableFile(path)) {
                    entry.previewFilePath = path;
                }
            }
        }

        entries.append(entry);
    }

    return entries;
}

ClipboardEntry loadClipboardEntry(qint64 id)
{
    ClipboardEntry entry;

    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
    QSqlQuery query(database);
    query.prepare("SELECT id, kind, content, preview, created_at FROM clipboard_item WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec() || !query.next()) {
        return entry;
    }

    entry.id = query.value(0).toLongLong();
    entry.kind = query.value(1).toString();
    entry.content = query.value(2).toByteArray();
    entry.preview = query.value(3).toString();
    entry.createdAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODate);

    if (entry.kind == QStringLiteral("files")) {
        // 详情弹窗需要文件名和扩展名，从 JSON 里取第一个路径就够了
        const QJsonArray paths = QJsonDocument::fromJson(entry.content).array();
        if (!paths.isEmpty()) {
            const QString firstPath = paths.first().toString();
            entry.firstFilePath = firstPath;
            entry.fileExtension = QFileInfo(firstPath).suffix().toLower();
        }
    }

    return entry;
}

QByteArray loadClipboardEntryContent(qint64 id)
{
    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
    QSqlQuery query(database);
    query.prepare("SELECT content FROM clipboard_item WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec() || !query.next()) {
        return {};
    }

    return query.value(0).toByteArray();
}

void deleteClipboardEntry(qint64 id)
{
    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
    QSqlQuery query(database);
    query.prepare("DELETE FROM clipboard_item WHERE id = ?");
    query.addBindValue(id);

    if (!query.exec()) {
        qWarning() << "删除剪贴板记录失败:" << query.lastError().text();
    }
}

void clearClipboardEntries()
{
    QSqlDatabase database = QSqlDatabase::database(CONNECTION_NAME);
    QSqlQuery query(database);

    if (!query.exec("DELETE FROM clipboard_item")) {
        qWarning() << "清空剪贴板记录失败:" << query.lastError().text();
    }
}
