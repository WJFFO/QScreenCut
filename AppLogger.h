#pragma once

#include <QString>

// 全局日志：接管 Qt 的 qDebug / qInfo / qWarning / qCritical，
// 统一写到 exe 目录下的 logs 文件夹（和 data 文件夹同级，整个目录可以一起拷走）。
//
// - 文件按日期命名：logs/2026-11-04.log
//   日期变化时自动切到新文件，不用重启程序
// - 启动时清理一次过期日志，之后由定时器定期清理（保留最近 N 天）
namespace AppLogger {

// 安装消息处理器、打开当天的日志文件并启动清理定时器。
// daysToKeep 是日志保留天数，早于它的按日期命名的日志文件会被删掉。
// 必须在 QApplication 构造之后调用（要用 applicationDirPath）
void init(int daysToKeep = 7);

}  // namespace AppLogger
