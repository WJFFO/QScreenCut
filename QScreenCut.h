#pragma once

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>
#include <QPointer>
//#include "ui_QScreenCut.h"

struct ClipboardEntry;

class QAction;
class QKeySequence;
class QListWidgetItem;
class HistoryListWidget;
class QPushButton;
class QSystemTrayIcon;
class QTimer;
class ScreenshotOverlay;

// 这里只做前置声明，避免把 QWindowKit 的头文件带给所有包含本头文件的地方
namespace QWK {
    class WidgetWindowAgent;
}
class SettingsWindow;

// 主窗口：上半部分是剪贴板历史，顶部可以唤起截图。
// 窗口本身是无边框样式，标题栏由程序自己绘制，拖动、缩放、系统菜单
// 等原生行为交给 QWindowKit 的窗口代理处理。
class QScreenCut : public QWidget, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    QScreenCut(QWidget *parent = nullptr);
    ~QScreenCut();

    // 把主窗口显示出来并提到最前。托盘菜单、以及用户重复启动程序时都走这里
    void showMainWindow();

protected:
    // 最大化、还原会改变窗口状态，需要同步刷新最大化按钮的图标
    void changeEvent(QEvent *event) override;

    // 拉伸窗口时先冻住列表画面，尺寸稳定后再恢复刷新
    void resizeEvent(QResizeEvent *event) override;

    // 关闭窗口时收进托盘，剪贴板监听和快捷键继续生效
    void closeEvent(QCloseEvent *event) override;

public:
    // 接收 Windows 的 WM_HOTKEY 消息。
    // 热键注册在线程上而不是窗口上，所以走事件过滤器，窗口重建也不会失效。
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void buildTitleBar();
    QWidget *buildContentArea();
    void refreshMaximizeButton();

    // 标题栏图标统一着色（置顶状态时置顶按钮单独用白色）
    void refreshTitleBarIcons();

    // 切换主窗口是否始终显示在最前
    void toggleStayOnTop();

    // 打开设置窗口（独立窗口，非模态）
    void openSettings();

    // 把设置窗口交回来的配置落到 AppSettings，并处理快捷键换绑。
    // hotkeyEnabled 为 false 时只注销热键，组合键原样保留
    void applySettings(bool hotkeyEnabled, const QKeySequence &hotkey, bool autoStart,
                       const QString &aiBaseUrl, const QString &aiApiKey,
                       const QString &aiModel, const QString &translateTarget,
                       const QString &translateEngine, const QString &baiduAppId,
                       const QString &baiduSecretKey);

    // 托盘菜单里切换"启用截图快捷键"时调用：注册或注销热键并同步设置
    void applyHotkeyEnabled(bool enabled);

    // 注册截图全局热键。注册失败（被别的程序占用）返回 false
    bool applyCaptureHotkey(const QKeySequence &sequence);

    // 把当前快捷键显示到按钮、托盘菜单上
    void refreshHotkeyTexts();

    // 剪贴板相关
    void onClipboardChanged();                  // 剪贴板变化：分析内容并写入数据库
    void reloadHistoryList();                   // 清空列表并重新加载第一页
    void appendHistoryPage();                   // 从数据库取下一页追加到列表末尾
    void appendEntryItem(const ClipboardEntry &entry);  // 把一条记录加进列表
    void showEntryDetail(QListWidgetItem *item);  // 弹出条目详情窗口
    void restoreEntryById(qint64 entryId);      // 把某条历史重新放回剪贴板

    // 截图
    void startCapture();

    // 关掉除 keep 以外的遮罩。多屏时每块屏一层，用哪块就把其它的收掉
    void closeCaptureOverlaysExcept(ScreenshotOverlay *keep);

    // 窗口代理的 parent 是 this，随窗口一起销毁，不需要手动释放
    QWK::WidgetWindowAgent *windowAgent = nullptr;

    QWidget *titleBar = nullptr;
    QWidget *contentArea = nullptr;           // 内容区，换肤时要重新套样式
    QPushButton *stayOnTopButton = nullptr;   // 标题栏上的置顶开关
    //QPushButton *settingsButton = nullptr;    // 标题栏上的设置入口
    QPushButton *minimizeButton = nullptr;
    QPushButton *maximizeButton = nullptr;
    QPushButton *closeButton = nullptr;

    //QPushButton *captureButton = nullptr;     // 内容区顶部唤起截图的按钮
    QAction *captureAction = nullptr;         // 托盘菜单里的截图项，文字要跟着快捷键变
    QAction *captureEnabledAction = nullptr;  // 托盘菜单里的"启用截图快捷键"开关
    HistoryListWidget *historyList = nullptr;  // 历史列表，拉伸时会先冻住画面
    QSystemTrayIcon *trayIcon = nullptr;
    bool hotkeyRegistered = false;

    // 拉伸结束后多久恢复列表刷新。太短会在连续拖动时反复解冻，太长会显得迟钝
    QTimer *resizeSettleTimer = nullptr;

    // 本次截图在各块屏幕上铺的遮罩。用 QPointer：它们是 DeleteOnClose 的，
    // 自己销毁后这里会自动变成空指针
    QVector<QPointer<ScreenshotOverlay>> captureOverlays;
    bool captureOverlaysClosing = false;   // 正在批量关闭，避免 close 信号互相递归

    // 历史列表的分页状态：已加载的条数就是下一页的偏移。
    // 数据库还有剩余时才会在滚动到底部时继续取
    int historyLoadedCount = 0;
    bool historyHasMore = false;

    // 主窗口是否处于置顶状态，和按钮的高亮保持同步
    bool stayOnTop = false;

    // 标题栏上的程序小图标，换肤时要重新着色
    class QLabel *titleIconLabel = nullptr;

    // 当前打开的设置窗口（如果有）。保存外观后要让它也跟着换肤；
    // 窗口是 DeleteOnClose 的，用 QPointer 自动感知销毁
    QPointer<SettingsWindow> settingsWindow;

    // 恢复历史记录时会写剪贴板，Windows 的剪贴板通知是异步送达的，
    // 同步的信号阻断挡不住，这里用一个短时标志跳过那一次变化
    bool ignoreClipboardChange = false;
};
