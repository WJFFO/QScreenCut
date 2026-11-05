#include "QScreenCut.h"
#include "AppLogger.h"
#include <QtCore/QString>
#include <QtGui/QFont>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <QtWidgets/QApplication>

namespace {

// 单实例用的本地套接字名字。同一台机器上只能有一个程序监听这个名字，
// 第二个实例连上来说明"我来了"，然后自己退出
const QString SINGLE_INSTANCE_KEY = QStringLiteral("QScreenCut.SingleInstance");

// 通知已经在跑的实例把主窗口叫出来。
// 返回 true 表示确实有实例在跑（连接成功），调用方应该直接退出
bool notifyRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(SINGLE_INSTANCE_KEY);
    if (!socket.waitForConnected(300)) {
        return false;
    }

    // 内容是固定的，对方收到连接就会把窗口提上来，这里不需要传参数
    socket.write("show");
    socket.flush();
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

}

// 程序入口：创建应用对象后进入事件循环。
// 主窗口只能从托盘菜单的"显示剪切板"打开——这是个常驻后台的剪贴板工具，
// 任何其它入口（包括启动参数）都不主动弹窗，避免打断正在做的事。
int main(int argc, char *argv[])
{
    // 无边框窗口的必备前提：不让 Qt 为子控件额外创建原生窗口容器，
    // 否则命中测试（拖动、缩放）和绘制都会异常。必须在 QApplication 之前设置。
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

    // QApplication 管理字体、剪贴板、事件分发等全局资源，必须早于任何窗口构造
    QApplication app(argc, argv);

    // 日志：之后所有 qDebug/qWarning 输出都会进 exe 目录下的 logs 文件夹，
    // 按日期分文件，保留最近 7 天。要尽早装上，启动过程的问题也能记下来
    AppLogger::init(7);
    qInfo() << "程序启动，版本构建于" << __DATE__ << __TIME__;

    // 单实例：剪贴板监听、全局热键、数据库都只能有一份，
    // 多开会互相抢热键、抢同一个 sqlite 文件。
    // 已经有实例在跑时把它的窗口叫出来，自己直接退出
    if (notifyRunningInstance()) {
        qInfo() << "已有实例在运行，已唤醒它，本次启动退出";
        return 0;
    }

    // 走到这里说明刚才没连上。上一次异常退出可能留下了同名套接字，
    // 先清掉再监听，否则这台机器上会永远启动不起来
    QLocalServer::removeServer(SINGLE_INSTANCE_KEY);

    QLocalServer singleInstanceServer;
    QString listenError;
    if (!singleInstanceServer.listen(SINGLE_INSTANCE_KEY)) {
        listenError = singleInstanceServer.errorString();
    }

    // Fluent 风格的全局字体：Windows 上的标准 UI 字体就是雅黑 UI 9pt，
    // 显式设一遍是为了不跟随系统主题变化，各窗口看起来一致
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    // 常驻托盘的程序必须关掉这个默认行为：
    // 主窗口平时是隐藏的，设置窗口之类临时窗口关闭时会被当作
    // "最后一个窗口关闭了"而直接退出整个程序。
    // 退出只走托盘菜单里的"退出"（qApp->quit()）
    app.setQuitOnLastWindowClosed(false);

    QScreenCut window;

    // 监听成功了才算"我是唯一实例"：第二个实例一连接，就把主窗口叫到前面来
    if (!listenError.isEmpty()) {
        qWarning() << "单实例监听失败，可能已有实例在运行:" << listenError;
    } else {
        QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, &app, [&]() {
            while (QLocalSocket *socket = singleInstanceServer.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected,
                                 socket, &QLocalSocket::deleteLater);
                socket->readAll();
                window.showMainWindow();
                socket->disconnectFromServer();
            }
        });
    }

    // 主窗口保持隐藏，只驻留托盘；打开入口只有托盘菜单。
    // exec() 会阻塞在这里，直到托盘菜单选"退出"才返回
    return app.exec();
}
