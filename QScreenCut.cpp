#include "QScreenCut.h"

#include "AppSettings.h"
#include "AppStyle.h"
#include "ClipboardStore.h"
#include "EntryDetailDialog.h"
#include "HistoryListWidget.h"
#include "IconUtils.h"
#include "ScreenshotOverlay.h"
#include "SettingsWindow.h"
#include "ThumbnailUtils.h"

#include <QtCore/qt_windows.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <QWKWidgets/widgetwindowagent.h>

namespace {

// 标题栏与系统按钮的尺寸集中在这里，改一处就能整体调整外观
constexpr int TITLE_BAR_HEIGHT = 40;
constexpr int SYSTEM_BUTTON_WIDTH = 46;

// 全局热键编号，只要在程序内唯一即可
constexpr int HOTKEY_ID_CAPTURE = 0x1001;

// 文本最多保存的长度。超长内容（比如整份日志）截断后入库，数据库体积才可控
constexpr int MAX_TEXT_BYTES = 64 * 1024;

// 图片只存缩略图，最长边限制在这个值以内
constexpr int THUMBNAIL_MAX_SIDE = 320;

// 列表里显示的摘要长度
constexpr int PREVIEW_MAX_CHARS = 120;

// 历史列表每一行的高度：两行文字加上下内边距。
// 固定下来之后窗口缩放时不用再为每条记录重新计算文本尺寸
constexpr int HISTORY_ROW_HEIGHT = 56;

// 历史列表每次从数据库取的条数。
// 滚动到接近底部时再取下一页，窗口里只保留已经看过的内容
constexpr int HISTORY_PAGE_SIZE = 50;

// 拉伸结束后多久恢复列表刷新。120 毫秒刚好落在"松手"之后，
// 连续拖动时不会每来一次消息就解冻重绘一遍
constexpr int RESIZE_SETTLE_DELAY = 120;

// 按扩展名给出徽章底色：同一类文件一个色系，扫一眼就能分辨类型。
// 色值沿用各家办公软件的品牌色，认知成本最低
QColor colorForExtension(const QString &extension)
{
    if (extension == QStringLiteral("doc") || extension == QStringLiteral("docx")
        || extension == QStringLiteral("rtf") || extension == QStringLiteral("odt")) {
        return QColor(0x2b, 0x57, 0x9a);   // Word 蓝
    }

    if (extension == QStringLiteral("xls") || extension == QStringLiteral("xlsx")
        || extension == QStringLiteral("csv") || extension == QStringLiteral("ods")) {
        return QColor(0x21, 0x73, 0x46);   // Excel 绿
    }

    if (extension == QStringLiteral("ppt") || extension == QStringLiteral("pptx")
        || extension == QStringLiteral("odp")) {
        return QColor(0xc4, 0x3e, 0x1c);   // PPT 橙红
    }

    if (extension == QStringLiteral("pdf")) {
        return QColor(0xd1, 0x34, 0x38);   // PDF 红
    }

    if (extension == QStringLiteral("zip") || extension == QStringLiteral("rar")
        || extension == QStringLiteral("7z") || extension == QStringLiteral("tar")
        || extension == QStringLiteral("gz") || extension == QStringLiteral("xz")) {
        return QColor(0xb0, 0x6d, 0x00);   // 压缩包 棕黄
    }

    if (extension == QStringLiteral("png") || extension == QStringLiteral("jpg")
        || extension == QStringLiteral("jpeg") || extension == QStringLiteral("gif")
        || extension == QStringLiteral("bmp") || extension == QStringLiteral("webp")
        || extension == QStringLiteral("ico") || extension == QStringLiteral("tif")
        || extension == QStringLiteral("tiff") || extension == QStringLiteral("svg")
        || extension == QStringLiteral("heic")) {
        return QColor(0x00, 0x82, 0x72);   // 图片 青
    }

    if (extension == QStringLiteral("mp3") || extension == QStringLiteral("wav")
        || extension == QStringLiteral("flac") || extension == QStringLiteral("aac")
        || extension == QStringLiteral("ogg") || extension == QStringLiteral("m4a")
        || extension == QStringLiteral("wma")) {
        return QColor(0x87, 0x64, 0xb8);   // 音频 紫
    }

    if (extension == QStringLiteral("mp4") || extension == QStringLiteral("avi")
        || extension == QStringLiteral("mkv") || extension == QStringLiteral("mov")
        || extension == QStringLiteral("wmv") || extension == QStringLiteral("flv")
        || extension == QStringLiteral("webm") || extension == QStringLiteral("m4v")) {
        return QColor(0xc2, 0x39, 0x8e);   // 视频 玫红
    }

    if (extension == QStringLiteral("cpp") || extension == QStringLiteral("h")
        || extension == QStringLiteral("hpp") || extension == QStringLiteral("c")
        || extension == QStringLiteral("py") || extension == QStringLiteral("js")
        || extension == QStringLiteral("ts") || extension == QStringLiteral("java")
        || extension == QStringLiteral("cs") || extension == QStringLiteral("go")
        || extension == QStringLiteral("rs") || extension == QStringLiteral("php")
        || extension == QStringLiteral("rb") || extension == QStringLiteral("html")
        || extension == QStringLiteral("css") || extension == QStringLiteral("json")
        || extension == QStringLiteral("xml") || extension == QStringLiteral("sql")
        || extension == QStringLiteral("sh") || extension == QStringLiteral("bat")
        || extension == QStringLiteral("ps1")) {
        return QColor(0x44, 0x54, 0x6a);   // 代码 灰蓝
    }

    if (extension == QStringLiteral("txt") || extension == QStringLiteral("md")
        || extension == QStringLiteral("log") || extension == QStringLiteral("ini")
        || extension == QStringLiteral("conf") || extension == QStringLiteral("cfg")
        || extension == QStringLiteral("yml") || extension == QStringLiteral("yaml")) {
        return QColor(0x54, 0x6e, 0x7a);   // 纯文本 青灰
    }

    if (extension == QStringLiteral("exe") || extension == QStringLiteral("dll")
        || extension == QStringLiteral("msi")) {
        return QColor(0x00, 0x39, 0x66);   // 可执行 深蓝
    }

    // 认不出来的类型统一用中性灰
    return QColor(0x5c, 0x5c, 0x5c);
}

// 生成"后缀文字 + 圆角彩色底"的徽章图标，同一 (文字, 颜色) 组合只渲染一次。
// 按 2 倍尺寸渲染再交给列表缩放，高分屏上文字不会发虚
QIcon extensionBadgeIcon(const QString &text, const QColor &background)
{
    // 渲染 68px，列表里按 34px 显示（devicePixelRatio = 2）
    constexpr int RENDER_SIZE = 68;
    constexpr int LOGICAL_SIZE = 34;

    static QHash<QString, QIcon> cache;

    const QString cacheKey = text + QLatin1Char('|') + background.name();
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.constEnd()) {
        return cached.value();
    }

    QImage image(RENDER_SIZE, RENDER_SIZE, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.0);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRect(0, 0, LOGICAL_SIZE, LOGICAL_SIZE), 8, 8);

    // 后缀文字白色加粗居中；四个字母（DOCX、XLSX）比两三个字母缩小一号才放得下
    QFont badgeFont(QStringLiteral("Microsoft YaHei UI"));
    badgeFont.setBold(true);
    badgeFont.setPixelSize(text.size() <= 3 ? 13 : 11);
    painter.setFont(badgeFont);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, LOGICAL_SIZE, LOGICAL_SIZE), Qt::AlignCenter, text);
    painter.end();

    const QIcon icon(QPixmap::fromImage(image));
    cache.insert(cacheKey, icon);
    return icon;
}

// 把一张图裁成正方形圆角缩略图，用来当列表图标。
// 按 2 倍尺寸渲染再交给列表缩放，高分屏上不会发虚
QIcon imageThumbnailIcon(const QImage &image)
{
    constexpr int RENDER_SIZE = 68;
    constexpr int LOGICAL_SIZE = 34;

    if (image.isNull()) {
        return QIcon();
    }

    // 先等比放大到能盖住整个正方形，再从中间裁一块：
    // 这样图片不会变形，也不会出现上下或左右的白边
    QImage square = image.scaled(RENDER_SIZE, RENDER_SIZE, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    square = square.copy((square.width() - RENDER_SIZE) / 2,
                         (square.height() - RENDER_SIZE) / 2,
                         RENDER_SIZE, RENDER_SIZE);
    square = square.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    square.setDevicePixelRatio(2.0);

    // 用 DestinationIn 擦掉圆角外的部分，得到和徽章一致的圆角外框
    QPainter painter(&square);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawRoundedRect(QRectF(0, 0, LOGICAL_SIZE, LOGICAL_SIZE), 8, 8);
    painter.end();

    return QIcon(QPixmap::fromImage(square));
}

// 列表图标：图片和视频显示第一张缩略图，其它类型留给调用方画后缀徽章。
// 缩略图带缓存，重新加载列表时不用再解码一遍、也不用再向系统要一次
QIcon thumbnailIconForEntry(const ClipboardEntry &entry)
{
    static QHash<QString, QIcon> cache;

    // 一次会话里攒太多没意义，超过上限整体清空，按需重建
    if (cache.size() > 400) {
        cache.clear();
    }

    QString cacheKey;
    QImage image;

    if (entry.kind == QStringLiteral("image")) {
        // 图片条目的 content 本身就是入库时存下的缩略图，按记录 id 缓存
        cacheKey = QStringLiteral("image:%1").arg(entry.id);
    } else if (!entry.previewFilePath.isEmpty()) {
        // 文件按"路径 + 修改时间 + 大小"缓存，文件内容变了缓存自然失效
        const QFileInfo info(entry.previewFilePath);
        if (!info.exists() || !info.isFile()) {
            return QIcon();
        }

        cacheKey = QStringLiteral("file:%1|%2|%3")
                       .arg(QDir::toNativeSeparators(entry.previewFilePath))
                       .arg(info.lastModified().toMSecsSinceEpoch())
                       .arg(info.size());
    } else {
        return QIcon();
    }

    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.constEnd()) {
        return cached.value();
    }

    if (entry.kind == QStringLiteral("image")) {
        image = QImage::fromData(entry.content, "PNG");
    } else {
        // 和资源管理器用同一个接口：视频拿系统缩略图，图片直接读内容。
        // 这里不要文件图标兜底，提取不出来就退回后缀徽章
        image = loadFilePreview(entry.previewFilePath, 136, false);
    }

    // 取不到也把空图标记下来，免得每次刷新都为同一批文件重试一次耗时的提取
    const QIcon icon = imageThumbnailIcon(image);
    cache.insert(cacheKey, icon);
    return icon;
}

// 把多行文本压成单行摘要，用于列表显示
QString makePreview(const QString &text)
{
    QString singleLine = text;
    singleLine.replace(QLatin1Char('\r'), QLatin1Char(' '));
    singleLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
    singleLine = singleLine.simplified();

    if (singleLine.size() <= PREVIEW_MAX_CHARS) {
        return singleLine;
    }

    return singleLine.left(PREVIEW_MAX_CHARS) + QStringLiteral("…");
}

// 生成图片缩略图并编码成 PNG，数据库里存的就是这份小图
QByteArray makeThumbnail(const QImage &image)
{
    QImage thumbnail = image;
    if (image.width() > THUMBNAIL_MAX_SIDE || image.height() > THUMBNAIL_MAX_SIDE) {
        thumbnail = image.scaled(THUMBNAIL_MAX_SIDE, THUMBNAIL_MAX_SIDE,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    thumbnail.save(&buffer, "PNG");
    return data;
}

// 把 Qt 的按键值换算成 Windows 虚拟键码。
// 只需要覆盖设置界面里常用的键，返回 0 表示这个键不能注册成全局热键。
UINT toVirtualKey(int key)
{
    // 字母和数字的 Qt 键值本身就等于对应的 ASCII 码，和虚拟键码一致
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<UINT>(key);
    }

    // F1~F24 的虚拟键码是连续的，从一个起点加偏移就能算出来
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    }

    switch (key) {
    case Qt::Key_Space:     return VK_SPACE;
    case Qt::Key_Tab:       return VK_TAB;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return VK_RETURN;
    case Qt::Key_Escape:    return VK_ESCAPE;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Insert:    return VK_INSERT;
    case Qt::Key_Delete:    return VK_DELETE;
    case Qt::Key_Home:      return VK_HOME;
    case Qt::Key_End:       return VK_END;
    case Qt::Key_PageUp:    return VK_PRIOR;
    case Qt::Key_PageDown:  return VK_NEXT;
    case Qt::Key_Left:      return VK_LEFT;
    case Qt::Key_Right:     return VK_RIGHT;
    case Qt::Key_Up:        return VK_UP;
    case Qt::Key_Down:      return VK_DOWN;
    case Qt::Key_Print:     return VK_SNAPSHOT;
    case Qt::Key_Pause:     return VK_PAUSE;
    default:                return 0;
    }
}

// 把 Qt 的修饰键换算成 RegisterHotKey 需要的 MOD_ 标志
UINT toWindowsModifiers(Qt::KeyboardModifiers modifiers)
{
    UINT result = 0;

    if (modifiers & Qt::ControlModifier) {
        result |= MOD_CONTROL;
    }
    if (modifiers & Qt::AltModifier) {
        result |= MOD_ALT;
    }
    if (modifiers & Qt::ShiftModifier) {
        result |= MOD_SHIFT;
    }
    if (modifiers & Qt::MetaModifier) {
        result |= MOD_WIN;
    }

    return result;
}

}

QScreenCut::QScreenCut(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("QScreenCut"));

    // 程序图标用同一份多尺寸 ico：任务栏、Alt+Tab 和托盘都从这里取，
    // 系统会按需要的尺寸自己挑一页，不会把大图缩糊
    setWindowIcon(QIcon(QStringLiteral(":/QScreenCut/QScreenCut.ico")));

    // 拉伸结束后恢复列表刷新的定时器。窗口构造过程中就可能收到 resize，
    // 所以先建好，避免 resizeEvent 里拿到空指针
    resizeSettleTimer = new QTimer(this);
    resizeSettleTimer->setSingleShot(true);
    resizeSettleTimer->setInterval(RESIZE_SETTLE_DELAY);
    connect(resizeSettleTimer, &QTimer::timeout, this, [this]() {
        if (historyList != nullptr) {
            historyList->releaseFrame();
        }
    });

    resize(400, 600);

    // 第一步：装窗口代理。setup() 内部会把窗口改成无边框，并接管
    // 拖动、边缘缩放、双击最大化、右键系统菜单等原生行为。
    windowAgent = new QWK::WidgetWindowAgent(this);
    windowAgent->setup(this);

    // 第二步：画自己的标题栏
    buildTitleBar();

    // 第三步：搭内容区（历史列表、清空按钮）
    buildContentArea();

    // 第四步：纵向排布，标题栏固定高度，正文占满剩余空间
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(titleBar);
    mainLayout->addWidget(contentArea, 1);

    // 第五步：打开数据库、刷新列表，并开始监听剪贴板
    if (!openClipboardDatabase()) {
        // 数据库不可用时窗口仍然能开，只是历史记录存不下来
        qWarning() << "剪贴板数据库不可用，历史记录将不会保存";
    } else {
        qInfo() << "剪贴板数据库已打开";
    }
    reloadHistoryList();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
            this, &QScreenCut::onClipboardChanged);

    // 第五步：托盘图标，保证关窗后程序仍在后台监听
    QIcon trayImage = windowIcon();
    if (trayImage.isNull()) {
        trayImage = style()->standardIcon(QStyle::SP_ComputerIcon);
    }

    trayIcon = new QSystemTrayIcon(trayImage, this);
    trayIcon->setToolTip(tr("QScreenCut"));

    auto *trayMenu = new QMenu(this);
    trayMenu->addAction(tr("显示剪切板"), this, [this]() { showMainWindow(); });
    // 截图项的文字后面由 refreshHotkeyTexts() 补上当前快捷键，这里先留空
    captureAction = trayMenu->addAction(tr("开始截图"), this, &QScreenCut::startCapture);
    // 快捷键开关：勾选状态即当前是否注册了全局热键，托盘里就能直接禁用/启用
    captureEnabledAction = trayMenu->addAction(tr("启用截图快捷键"));
    captureEnabledAction->setCheckable(true);
    captureEnabledAction->setChecked(AppSettings::instance().captureHotkeyEnabled());
    connect(captureEnabledAction, &QAction::toggled, this, &QScreenCut::applyHotkeyEnabled);
    trayMenu->addAction(tr("设置"), this, &QScreenCut::openSettings);
    trayMenu->addSeparator();
    trayMenu->addAction(tr("退出"), this, [this]() {
        // 先藏掉托盘图标，closeEvent 才不会把退出拦下来
        trayIcon->hide();
        qApp->quit();
    });
    trayIcon->setContextMenu(trayMenu);

    // 单击托盘图标直接开始截图（最常用的动作放在最顺手的位置）；
    // 打开主窗口走托盘菜单的"显示剪切板"，双击不再有专门行为
    connect(trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    startCapture();
                }
            });
    trayIcon->show();

    // 全局热键注册在线程上，而不是绑到窗口句柄：QWindowKit 接管窗口时可能重建句柄，
    // 绑在窗口上的热键会跟着失效。热键消息统一由 nativeEventFilter 接收。
    qApp->installNativeEventFilter(this);

    // 配置里的快捷键可能在某次启动前被改成了别的组合，这里按配置注册；
    // 开关是关着的就不注册，省一次注定要被注销的注册
    if (AppSettings::instance().captureHotkeyEnabled()) {
        applyCaptureHotkey(AppSettings::instance().captureHotkey());
    }

    // 开机自启的启动项里存的是 exe 全路径，程序换过目录时要跟着更新
    AppSettings::instance().refreshAutoStartPath();

    refreshHotkeyTexts();
}

QScreenCut::~QScreenCut()
{
    // windowAgent、titleBar 和各个按钮的 parent 都是 this，由 Qt 统一销毁
    qApp->removeNativeEventFilter(this);
    if (hotkeyRegistered) {
        UnregisterHotKey(nullptr, HOTKEY_ID_CAPTURE);
    }
    clearClipboardEntries();
    closeClipboardDatabase();
}

void QScreenCut::showMainWindow()
{
    // 窗口可能是隐藏到托盘的状态，先还原再往前放。
    // showNormal 同时也处理"最小化了"的情况，比单纯 show 更稳
    showNormal();
    raise();
    activateWindow();
}

void QScreenCut::buildTitleBar()
{
    titleBar = new QWidget(this);
    titleBar->setObjectName("titleBar");
    titleBar->setFixedHeight(TITLE_BAR_HEIGHT);

    // 样式写在标题栏上，让所有按钮共用同一套规则；
    // 关闭按钮单独给一个红色悬停效果，区分危险操作。
    // 置顶按钮在开启时整块变成强调色（Fluent 蓝），一眼就能看出当前是否置顶
    titleBar->setStyleSheet(QStringLiteral(
        "#titleBar { background-color: %1; border-bottom: 1px solid %2; }"
        "#titleLabel { color: %3; font-size: 12px; }"
        "#titleBar QPushButton { border: none; background-color: transparent;"
        "                         color: %3; font-size: 14px; }"
        "#titleBar QPushButton:hover { background-color: %4; }"
        "#titleBar QPushButton#stayOnTopButton[active=\"true\"]"
        "                        { background-color: %5; color: #ffffff; }"
        "#titleBar QPushButton#closeButton:hover"
        "                        { background-color: %6; color: #ffffff; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::titleBarHover.name(),
             AppStyle::accent.name(), AppStyle::dangerHover.name()));

    auto titleLabel = new QLabel(tr("剪贴板"), titleBar);
    titleLabel->setObjectName("titleLabel");

    // 标题左边的剪贴板图标：和托盘、任务栏用的是同一个图形，
    // 缩小到 18 像素也还认得出来，比纯文字更容易在一排窗口里定位
    titleIconLabel = new QLabel(titleBar);
    titleIconLabel->setFixedSize(18, 18);

    // 标题栏是浅色底，图标用深灰，和标题文字的颜色接近
    const QSize iconSize(16, 16);
    const QColor iconColor(43, 43, 43);
    const QString iconRoot = QStringLiteral(":/QScreenCut/images/");

    // 置顶开关放在系统按钮左边。它不属于系统按钮，所以不注册给窗口代理，
    // 否则会被当成最小化/最大化/关闭之一处理。
    stayOnTopButton = new QPushButton(titleBar);
    stayOnTopButton->setObjectName("stayOnTopButton");
    stayOnTopButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    stayOnTopButton->setFocusPolicy(Qt::NoFocus);
    stayOnTopButton->setToolTip(tr("让窗口始终显示在最前"));
    stayOnTopButton->setIconSize(iconSize);
    stayOnTopButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("图钉.png"), iconColor));

    // 设置入口，和置顶按钮一样是普通按钮，不注册成系统按钮
    //settingsButton = new QPushButton(tr("设"), titleBar);
    //settingsButton->setObjectName("settingsButton");
    //settingsButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    //settingsButton->setFocusPolicy(Qt::NoFocus);
    //settingsButton->setToolTip(tr("设置快捷键和开机启动"));

    minimizeButton = new QPushButton(titleBar);
    minimizeButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    minimizeButton->setFocusPolicy(Qt::NoFocus);
    minimizeButton->setToolTip(tr("最小化"));
    minimizeButton->setIconSize(iconSize);
    minimizeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("最小化.png"), iconColor));

    maximizeButton = new QPushButton(titleBar);
    maximizeButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    maximizeButton->setFocusPolicy(Qt::NoFocus);
    maximizeButton->setToolTip(tr("最大化"));
    maximizeButton->setIconSize(iconSize);
    maximizeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("最大化.png"), iconColor));

    closeButton = new QPushButton(titleBar);
    closeButton->setObjectName("closeButton");
    closeButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setToolTip(tr("关闭到托盘"));
    closeButton->setIconSize(iconSize);
    closeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("关闭1.png"), iconColor));

    auto titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(14, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(titleIconLabel);
    titleLayout->addSpacing(8);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(stayOnTopButton);
    //titleLayout->addWidget(settingsButton);
    titleLayout->addWidget(minimizeButton);
    titleLayout->addWidget(maximizeButton);
    titleLayout->addWidget(closeButton);

    // 把标题栏交给窗口代理：空白处和标题文字都会变成可拖动区域
    windowAgent->setTitleBar(titleBar);

    // 标题栏上的普通按钮必须显式声明“可命中”。
    // 窗口代理默认把整片标题栏都当成拖动区，没声明的控件会被系统按 HTCAPTION 处理，
    // 鼠标点击根本到不了按钮上（表现为按钮点着没反应）。
    windowAgent->setHitTestVisible(stayOnTopButton);
    //windowAgent->setHitTestVisible(settingsButton);

    // 注册系统按钮。这一步不只是标记身份：
    // 1. Windows 11 悬停最大化按钮弹出的贴靠布局，依赖这个注册关系才生效；
    // 2. 注册后按钮会自动排除在拖动区之外，点击事件才能正常落到按钮上。
    windowAgent->setSystemButton(QWK::WindowAgentBase::Minimize, minimizeButton);
    windowAgent->setSystemButton(QWK::WindowAgentBase::Maximize, maximizeButton);
    windowAgent->setSystemButton(QWK::WindowAgentBase::Close, closeButton);

    // 按钮的点击行为仍由我们自己接管，系统只负责贴靠布局等外观行为
    connect(stayOnTopButton, &QPushButton::clicked, this, &QScreenCut::toggleStayOnTop);
    //connect(settingsButton, &QPushButton::clicked, this, &QScreenCut::openSettings);
    connect(minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(maximizeButton, &QPushButton::clicked, this, [this]() {
        if (isMaximized()) {
            showNormal();
            return;
        }
        showMaximized();
    });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    // 图标统一在这里着色（置顶状态会单独换色）
    refreshTitleBarIcons();
}

void QScreenCut::toggleStayOnTop()
{
    stayOnTop = !stayOnTop;

    // 置顶时把窗口插到最顶层，取消时退回普通层级
    HWND insertAfter = HWND_NOTOPMOST;
    if (stayOnTop) {
        insertAfter = HWND_TOPMOST;
    }

    // 直接用 Win32 改置顶属性，不调用 setWindowFlag：
    // 改 Qt 的窗口标志会让 Qt 销毁并重建原生窗口句柄，QWindowKit 的窗口代理、
    // 拖动区域和注册过的系统按钮都会跟着失效。
    SetWindowPos(reinterpret_cast<HWND>(winId()), insertAfter,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 按钮高亮由样式表的动态属性控制，属性变了要重新应用样式才会重绘
    stayOnTopButton->setProperty("active", stayOnTop);
    stayOnTopButton->style()->unpolish(stayOnTopButton);
    stayOnTopButton->style()->polish(stayOnTopButton);

    // 置顶后按钮变成蓝底，深色图标会糊在背景里，所以要换成白色版本；
    // 图标颜色统一由 refreshTitleBarIcons() 决定
    refreshTitleBarIcons();
}

// 标题栏上的图标着色。
// 图标用深灰，和标题文字的颜色接近；置顶按钮激活时固定用白色，
// 因为那时它的背景是强调色蓝
void QScreenCut::refreshTitleBarIcons()
{
    const QString iconRoot = QStringLiteral(":/QScreenCut/images/");
    const QColor iconColor(43, 43, 43);

    const QIcon clipboardIcon =
        loadTintedSvgIcon(QStringLiteral(":/QScreenCut/images/fileicons/clipboard-line.svg"),
                          iconColor, QSize(64, 64));
    titleIconLabel->setPixmap(clipboardIcon.pixmap(QSize(18, 18)));

    const QString pinIconPath = iconRoot + QStringLiteral("图钉.png");
    stayOnTopButton->setIcon(loadTintedIcon(pinIconPath, stayOnTop ? Qt::white
                                                                   : iconColor));
    stayOnTopButton->setToolTip(stayOnTop ? tr("取消窗口置顶")
                                          : tr("让窗口始终显示在最前"));

    minimizeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("最小化.png"),
                                           iconColor));
    closeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("关闭1.png"),
                                        iconColor));

    // 最大化按钮的图标跟窗口状态有关（最大化 / 还原两种）
    refreshMaximizeButton();
}

void QScreenCut::openSettings()
{
    // 设置是独立窗口，不再是模态弹窗：打开后主窗口照常可用。
    // 每次点开都新建一个，窗口关闭时自己销毁
    auto *window = new SettingsWindow;

    // 记住当前设置窗口
    settingsWindow = window;

    connect(window, &SettingsWindow::settingsApplied, this,
            [this](bool hotkeyEnabled, const QKeySequence &hotkey, bool autoStart,
                   const QString &aiBaseUrl, const QString &aiApiKey,
                   const QString &aiModel, const QString &translateTarget,
                   const QString &translateEngine, const QString &baiduAppId,
                   const QString &baiduSecretKey) {
                applySettings(hotkeyEnabled, hotkey, autoStart,
                              aiBaseUrl, aiApiKey, aiModel, translateTarget,
                              translateEngine, baiduAppId, baiduSecretKey);
            });
    window->show();
    window->raise();
    window->activateWindow();
}

void QScreenCut::applySettings(bool hotkeyEnabled, const QKeySequence &newHotkey, bool autoStart,
                          const QString &aiBaseUrl, const QString &aiApiKey,
                          const QString &aiModel, const QString &translateTarget,
                          const QString &translateEngine, const QString &baiduAppId,
                          const QString &baiduSecretKey)
{
    AppSettings &settings = AppSettings::instance();

    if (hotkeyEnabled) {
        const QKeySequence oldHotkey = settings.captureHotkey();

        // 快捷键没改就不用重新注册，免得白白注销一次再注册一次
        if (newHotkey != oldHotkey) {
            if (applyCaptureHotkey(newHotkey)) {
                settings.setCaptureHotkey(newHotkey);
            } else {
                // 注册不上（多半是被别的程序占了）就退回原来的组合，
                // 否则用户会落到既没有新热键、也没有旧热键的状态
                applyCaptureHotkey(oldHotkey);
                QMessageBox::warning(this, tr("快捷键不可用"),
                                     tr("%1 无法注册，可能已被其它程序占用。\n已保留原来的快捷键。")
                                         .arg(newHotkey.toString(QKeySequence::NativeText)));
            }
        }
    } else {
        // 禁用：只注销热键，保存的组合键原样保留，重新启用时直接用
        if (hotkeyRegistered) {
            UnregisterHotKey(nullptr, HOTKEY_ID_CAPTURE);
            hotkeyRegistered = false;
        }
        settings.setCaptureHotkey(newHotkey);
    }

    settings.setCaptureHotkeyEnabled(hotkeyEnabled);
    settings.setAutoStart(autoStart);

    // 接口配置只是存起来，等截图里点识别或翻译时才会用到
    settings.setAiBaseUrl(aiBaseUrl);
    settings.setAiApiKey(aiApiKey);
    settings.setAiModel(aiModel);
    settings.setTranslateTarget(translateTarget);
    settings.setTranslateEngine(translateEngine);
    settings.setBaiduAppId(baiduAppId);
    settings.setBaiduSecretKey(baiduSecretKey);

    qInfo() << "设置已保存";

    // 设置窗口和托盘菜单两边都要反映出最新状态
    if (captureEnabledAction != nullptr) {
        QSignalBlocker blocker(captureEnabledAction);
        captureEnabledAction->setChecked(hotkeyEnabled);
    }

    refreshHotkeyTexts();
}

void QScreenCut::applyHotkeyEnabled(bool enabled)
{
    AppSettings &settings = AppSettings::instance();
    settings.setCaptureHotkeyEnabled(enabled);

    if (enabled) {
        // 注册失败（多半被别的程序占了）就退回禁用状态，
        // 不让用户落到"开关开着却没热键"的困惑局面
        if (!applyCaptureHotkey(settings.captureHotkey())) {
            settings.setCaptureHotkeyEnabled(false);
            if (captureEnabledAction != nullptr) {
                QSignalBlocker blocker(captureEnabledAction);
                captureEnabledAction->setChecked(false);
            }
            QMessageBox::warning(this, tr("快捷键不可用"),
                                 tr("快捷键注册失败，可能已被其它程序占用，已保持禁用状态。"));
        }
    } else {
        // 禁用：只注销，保存的组合键原样保留
        if (hotkeyRegistered) {
            UnregisterHotKey(nullptr, HOTKEY_ID_CAPTURE);
            hotkeyRegistered = false;
        }
    }

    refreshHotkeyTexts();
}

bool QScreenCut::applyCaptureHotkey(const QKeySequence &sequence)
{
    // 同一个热键编号不能重复注册，先把旧的注销掉
    if (hotkeyRegistered) {
        UnregisterHotKey(nullptr, HOTKEY_ID_CAPTURE);
        hotkeyRegistered = false;
    }

    if (sequence.isEmpty()) {
        return false;
    }

    const UINT virtualKey = toVirtualKey(sequence[0].key());
    const UINT modifiers = toWindowsModifiers(sequence[0].keyboardModifiers());

    // 没有修饰键的单个键不能当全局热键，否则会把所有程序里的这个键都抢走
    if (virtualKey == 0 || modifiers == 0) {
        qWarning() << "这个组合键不支持注册成全局热键:" << sequence.toString();
        return false;
    }

    if (!RegisterHotKey(nullptr, HOTKEY_ID_CAPTURE, modifiers, virtualKey)) {
        qWarning() << "注册全局热键失败，可能已被其它程序占用:" << sequence.toString();
        return false;
    }

    hotkeyRegistered = true;
    qInfo() << "已注册全局热键" << sequence.toString();
    return true;
}

void QScreenCut::refreshHotkeyTexts()
{
    if (captureAction == nullptr) {
        return;
    }

    // 快捷键被禁用时托盘菜单上直接注明，避免用户以为组合键坏了
    if (!AppSettings::instance().captureHotkeyEnabled()) {
        captureAction->setText(tr("开始截图（快捷键已禁用）"));
        return;
    }

    const QString text = AppSettings::instance().captureHotkey().toString(QKeySequence::NativeText);
    captureAction->setText(tr("开始截图  %1").arg(text));
}

QWidget *QScreenCut::buildContentArea()
{
    contentArea = new QWidget(this);
    contentArea->setObjectName("contentArea");

    // Fluent 风格整体配色：灰底上浮白底圆角列表卡片，
    // 悬停浅灰、选中强调色的浅蓝。
    // 样式表里刻意不给 item 设上下内边距：一旦设了，Qt 每次都要按文本
    // 重新算每一项的尺寸，记录一多拖动窗口边缘就掉帧
    contentArea->setStyleSheet(QStringLiteral(
        "#contentArea { background-color: %1; }"
        "#historyList { border: 1px solid %2; border-radius: 8px;"
        "              background-color: %3; outline: none; color: %4; }"
        "#historyList::item { padding: 0px 12px;"
        "                    border-bottom: 1px solid %5; }"
        "#historyList::item:hover { background-color: %6; }"
        "#historyList::item:selected { background-color: %7; color: %4; }")
        .arg(AppStyle::windowBackground.name(), AppStyle::cardStroke.name(),
             AppStyle::cardBackground.name(), AppStyle::textPrimary.name(),
             AppStyle::listSeparator.name(), AppStyle::itemHover.name(),
             AppStyle::itemSelected.name()));

    // 顶部操作条：左边唤起截图，右边清空历史。
    // 截图按钮上的快捷键文字由 refreshHotkeyTexts() 按当前配置补上
    //captureButton = new QPushButton(tr("截图"), contentArea);
    //captureButton->setFixedHeight(34);
    //captureButton->setCursor(Qt::PointingHandCursor);
    //captureButton->setStyleSheet(
    //    "QPushButton { background-color: #0a84ff; color: #ffffff; border: none;"
    //    "              border-radius: 6px; padding: 0 18px; font-size: 13px; }"
    //    "QPushButton:hover { background-color: #3a9bff; }");

    auto clearButton = new QPushButton(tr("清空历史"), contentArea);
    clearButton->setFixedHeight(34);
    clearButton->setCursor(Qt::PointingHandCursor);
    clearButton->setStyleSheet(AppStyle::standardButtonQss());

    auto actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(10);
    //actionLayout->addWidget(captureButton);
    //actionLayout->addStretch();
    actionLayout->addWidget(clearButton);

    // 历史列表：一行一条，显示类型、摘要和时间
    historyList = new HistoryListWidget(contentArea);
    historyList->setObjectName("historyList");

    // 每条记录固定显示两行（摘要 + 时间），所以关掉自动换行、把行高也定下来。
    // 这两件事直接决定了拖窗口边缘时的流畅度：
    // 打开自动换行时每一行的高度都要按文本重新算，窗口一缩放，
    // 列表就要为几百条记录重排一次文本，拖动就明显掉帧。
    historyList->setWordWrap(false);
    historyList->setUniformItemSizes(true);
    // 分批布局：窗口缩放导致重新排布时，把工作拆成小块做，
    // 不会一口气卡住界面（记录多的时候尤其明显）
    historyList->setLayoutMode(QListView::Batched);
    historyList->setBatchSize(40);

    // 每条记录左边是一块"后缀 + 彩色圆角底"徽章，34 像素在 56 行高里大小合适
    historyList->setIconSize(QSize(34, 34));

    auto contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(12);
    contentLayout->addLayout(actionLayout);
    contentLayout->addWidget(historyList, 0);

    //connect(captureButton, &QPushButton::clicked, this, &QScreenCut::startCapture);
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        clearClipboardEntries();
        reloadHistoryList();
    });

    // 单击一条历史，弹窗显示详情；恢复到剪贴板的动作放在弹窗里
    connect(historyList, &QListWidget::itemClicked, this, &QScreenCut::showEntryDetail);

    // 滚动到接近底部时再取下一页：数据库按分页查询，
    // 界面上只保留加载过的内容，几千条记录也不用一次性塞进列表
    connect(historyList->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (!historyHasMore) {
            return;
        }
        const QScrollBar *bar = historyList->verticalScrollBar();
        if (value >= bar->maximum() - 20) {
            appendHistoryPage();
        }
    });

    // 右键可以删掉单条记录
    historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &position) {
                QListWidgetItem *item = historyList->itemAt(position);
                if (item == nullptr) {
                    return;
                }

                QMenu menu(this);
                QAction *deleteAction = menu.addAction(tr("删除这条记录"));
                if (menu.exec(historyList->mapToGlobal(position)) != deleteAction) {
                    return;
                }

                deleteClipboardEntry(item->data(Qt::UserRole).toLongLong());
                reloadHistoryList();
            });

    return contentArea;
}

void QScreenCut::onClipboardChanged()
{
    // 刚刚是我们自己把历史记录写回剪贴板，这次变化不入库
    if (ignoreClipboardChange) {
        ignoreClipboardChange = false;
        return;
    }

    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if (mimeData == nullptr) {
        return;
    }

    ClipboardEntry entry;
    entry.createdAt = QDateTime::currentDateTime();

    // 文件复制优先判断：Windows 复制文件时剪贴板里同时带文本（文件名），
    // 如果先判文本，存下来的就只是文件名，还原不出文件列表。
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : mimeData->urls()) {
            if (url.isLocalFile()) {
                paths.append(url.toLocalFile());
            }
        }

        if (!paths.isEmpty()) {
            entry.kind = QStringLiteral("files");
            entry.content = QJsonDocument(QJsonArray::fromStringList(paths))
                                .toJson(QJsonDocument::Compact);

            // 列表上只显示名字：单个文件用文件名，多个文件带上总数
            const QFileInfo firstInfo(paths.first());
            entry.preview = paths.size() == 1
                ? firstInfo.fileName()
                : tr("%1 等 %2 项").arg(firstInfo.fileName()).arg(paths.size());
        }
    }

    if (entry.kind.isEmpty() && mimeData->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            entry.kind = QStringLiteral("image");
            entry.content = makeThumbnail(image);
            entry.preview = tr("图片 %1 × %2").arg(image.width()).arg(image.height());
        }
    }

    if (entry.kind.isEmpty() && mimeData->hasText()) {
        const QString text = mimeData->text();
        if (!text.isEmpty()) {
            QByteArray rawText = text.toUtf8();
            // 超长文本截断后入库，保证数据库体积可控
            if (rawText.size() > MAX_TEXT_BYTES) {
                rawText = rawText.left(MAX_TEXT_BYTES);
            }

            entry.kind = QStringLiteral("text");
            entry.content = rawText;
            entry.preview = makePreview(text);
        }
    }

    // 图片、文件、文本都没命中（比如只复制了自定义格式），不记录
    if (entry.kind.isEmpty()) {
        return;
    }

    qDebug() << "剪贴板更新：" << entry.kind << entry.preview.left(60);

    insertClipboardEntry(entry);
    reloadHistoryList();
}

void QScreenCut::reloadHistoryList()
{
    if (historyList == nullptr) {
        return;
    }

    // 内容要整体换掉了，冻住的旧画面立刻作废，免得解冻前显示的是过期内容
    historyList->releaseFrame();

    // 重置分页状态：列表清空，从头加载第一页
    historyList->clear();
    historyLoadedCount = 0;
    appendHistoryPage();
}

void QScreenCut::appendHistoryPage()
{
    const QVector<ClipboardEntry> entries =
        loadClipboardEntries(HISTORY_PAGE_SIZE, historyLoadedCount);

    for (const ClipboardEntry &entry : entries) {
        appendEntryItem(entry);
    }

    // 取到的条数不满一页，说明数据库已经见底，不用再监听滚动
    historyLoadedCount += entries.size();
    historyHasMore = entries.size() == HISTORY_PAGE_SIZE;
}

void QScreenCut::appendEntryItem(const ClipboardEntry &entry)
{
    // 列表上只放名字：文本是部分原文，图片是"图片 W×H"，
    // 文件用第一个文件的名字（旧记录存的是整串路径，这里统一重算成文件名）
    QString title = entry.preview;
    if (entry.kind == QStringLiteral("files") && !entry.firstFilePath.isEmpty()) {
        title = QFileInfo(entry.firstFilePath).fileName();
    }

    const QString text = QStringLiteral("%1\n%2")
                             .arg(title,
                                  entry.createdAt.toString(QStringLiteral("MM-dd HH:mm:ss")));

    // 图片和视频优先显示缩略图；取不到再退回"后缀文字 + 颜色背景"的徽章
    QIcon itemIcon = thumbnailIconForEntry(entry);

    if (itemIcon.isNull()) {
        // 徽章：文件用后缀文字，其它类型用代表字，底色按类型区分
        QString badgeText;
        QColor badgeColor;
        if (entry.kind == QStringLiteral("text")) {
            badgeText = QStringLiteral("TXT");
            badgeColor = colorForExtension(QStringLiteral("txt"));
        } else if (entry.kind == QStringLiteral("image")) {
            badgeText = QStringLiteral("IMG");
            badgeColor = colorForExtension(QStringLiteral("png"));
        } else if (entry.kind == QStringLiteral("files")) {
            // 没有扩展名的多半是文件夹，本地路径直接问一下文件系统就知道了
            if (entry.fileExtension.isEmpty() && QFileInfo(entry.firstFilePath).isDir()) {
                badgeText = QStringLiteral("DIR");
                badgeColor = QColor(0xb8, 0x74, 0x00);   // 文件夹 黄褐
            } else {
                badgeText = entry.fileExtension.toUpper().left(4);
                if (badgeText.isEmpty()) {
                    badgeText = QStringLiteral("FILE");
                }
                badgeColor = colorForExtension(entry.fileExtension);
            }
        } else {
            badgeText = QStringLiteral("FILE");
            badgeColor = colorForExtension(QString());
        }

        itemIcon = extensionBadgeIcon(badgeText, badgeColor);
    }

    auto *item = new QListWidgetItem(text, historyList);
    item->setIcon(itemIcon);
    item->setSizeHint(QSize(0, HISTORY_ROW_HEIGHT));
    item->setData(Qt::UserRole, entry.id);
    item->setData(Qt::UserRole + 1, entry.kind);
}

void QScreenCut::showEntryDetail(QListWidgetItem *item)
{
    if (item == nullptr) {
        return;
    }

    const qint64 id = item->data(Qt::UserRole).toLongLong();

    EntryDetailDialog dialog(id, this);
    connect(&dialog, &EntryDetailDialog::restoreRequested, this, &QScreenCut::restoreEntryById);
    dialog.exec();
}

void QScreenCut::restoreEntryById(qint64 entryId)
{
    const ClipboardEntry entry = loadClipboardEntry(entryId);
    const QString kind = entry.kind;
    const QByteArray content = entry.content;
    if (content.isEmpty()) {
        return;
    }

    // 这次写剪贴板是我们自己做的，通知到达时跳过入库。
    // Windows 的剪贴板通知是异步的，同步的信号阻断挡不住，所以用标志位；
    // 再挂一个定时器兜底，保证标志不会一直挂着影响后续真实的复制。
    ignoreClipboardChange = true;
    QTimer::singleShot(300, this, [this]() { ignoreClipboardChange = false; });

    QClipboard *clipboard = QGuiApplication::clipboard();

    if (kind == QStringLiteral("text")) {
        clipboard->setText(QString::fromUtf8(content));
        return;
    }

    if (kind == QStringLiteral("files")) {
        QList<QUrl> urls;
        const QJsonArray paths = QJsonDocument::fromJson(content).array();
        for (const QJsonValue &path : paths) {
            urls.append(QUrl::fromLocalFile(path.toString()));
        }

        auto *mimeData = new QMimeData();
        mimeData->setUrls(urls);
        clipboard->setMimeData(mimeData);
        return;
    }

    // 图片恢复的是数据库里的缩略图，原图当时没有保存
    const QImage image = QImage::fromData(content, "PNG");
    if (!image.isNull()) {
        clipboard->setImage(image);
    }
}

void QScreenCut::startCapture()
{
    // 上一次截图还没结束就别再开一波遮罩：热键连按时很容易触发
    for (const QPointer<ScreenshotOverlay> &overlay : captureOverlays) {
        if (overlay != nullptr) {
            return;
        }
    }

    qInfo() << "开始截图";
    // 只有窗口正显示在屏幕上时才需要临时藏起来。
    // 最小化的窗口、已经收进托盘的窗口本来就不在画面里，
    // 截图结束后也不应该把它们弹出来——那会打断用户正在做的事。
    const bool wasOnScreen = isVisible() && !isMinimized();
    if (wasOnScreen) {
        //hide();
    }

    // 等窗口真正从屏幕上消失再抓屏，否则容易抓到残影
    QTimer::singleShot(150, this, [this, wasOnScreen]() {
        // 每块屏幕各抓一张、各铺一层遮罩。
        // 一块屏幕上只有一个缩放比例，混合 DPI 的多屏也不会坐标错位、截图发虚；
        // 用一个大窗口横跨所有屏幕反而做不到这一点
        const QList<QScreen *> screens = QGuiApplication::screens();

        captureOverlays.clear();
        captureOverlaysClosing = false;

        for (QScreen *screen : screens) {
            const QPixmap shot = screen->grabWindow(0);
            if (shot.isNull()) {
                qWarning() << "抓屏失败:" << screen->name();
                continue;
            }

            auto *overlay = new ScreenshotOverlay(shot.toImage(), screen->geometry(),
                                                  screen->devicePixelRatio());
            captureOverlays.append(overlay);

            // 在某一屏上开始拖选：其它屏的遮罩让位，只留正在用的这块
            connect(overlay, &ScreenshotOverlay::selectionStarted, this, [this, overlay]() {
                closeCaptureOverlaysExcept(overlay);
            });

            // 任意一块遮罩结束（复制、保存、置顶、取消），其余的一起收掉
            connect(overlay, &ScreenshotOverlay::captureClosed, this, [this, overlay, wasOnScreen]() {
                closeCaptureOverlaysExcept(overlay);

                // 覆盖层关闭后，只把原本就显示着的窗口还原回屏幕
                if (wasOnScreen && isHidden()) {
                    show();
                    raise();
                }
            });

            overlay->show();
        }

        // 键盘焦点交给最后一块，Esc 能直接取消这次截图
        if (!captureOverlays.isEmpty() && captureOverlays.last() != nullptr) {
            captureOverlays.last()->activateWindow();
            captureOverlays.last()->setFocus();
        }
    });
}

void QScreenCut::closeCaptureOverlaysExcept(ScreenshotOverlay *keep)
{
    // 关闭别的遮罩会再次触发 captureClosed，用标志挡住递归
    if (captureOverlaysClosing) {
        return;
    }

    captureOverlaysClosing = true;
    for (const QPointer<ScreenshotOverlay> &overlay : captureOverlays) {
        if (overlay != nullptr && overlay != keep) {
            overlay->close();
        }
    }
    captureOverlaysClosing = false;
}

void QScreenCut::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange) {
        refreshMaximizeButton();
    }
}

void QScreenCut::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (historyList == nullptr) {
        return;
    }

    // 拖窗口边缘时，每变一次尺寸列表都要重排并整块重绘（每行两段文字，
    // 开销不小）。这里先把当前画面冻住，期间只贴图；拖完 120 毫秒后再
    // 一次性重排重绘，拖动过程就跟手了
    historyList->freezeFrame();
    if (resizeSettleTimer != nullptr) {
        resizeSettleTimer->start();
    }
}

void QScreenCut::closeEvent(QCloseEvent *event)
{
    // 有托盘时，关闭只是收进托盘，真正退出走托盘菜单
    if (trayIcon != nullptr && trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }

    QWidget::closeEvent(event);
}

bool QScreenCut::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    // 只关心热键消息，其余消息原样放行
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY) {
        return false;
    }

    if (static_cast<int>(msg->wParam) != HOTKEY_ID_CAPTURE) {
        return false;
    }

    startCapture();
    return true;
}

void QScreenCut::refreshMaximizeButton()
{
    if (maximizeButton == nullptr) {
        return;
    }

    const QString iconRoot = QStringLiteral(":/QScreenCut/images/");
    const QColor iconColor(43, 43, 43);

    if (isMaximized()) {
        // 两个叠加的方框表示"还原"
        maximizeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("最大化 (1).png"),
                                               iconColor));
        maximizeButton->setToolTip(tr("还原"));
        return;
    }

    maximizeButton->setIcon(loadTintedIcon(iconRoot + QStringLiteral("最大化.png"),
                                           iconColor));
    maximizeButton->setToolTip(tr("最大化"));
}
