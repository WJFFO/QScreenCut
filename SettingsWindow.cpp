#include "SettingsWindow.h"

#include "AppSettings.h"
#include "AppStyle.h"
#include "BaiduTranslateClient.h"
#include "IconUtils.h"

#include <QtGui/QCursor>
#include <QtGui/QKeyEvent>
#include <QtGui/QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QKeySequenceEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <QWKWidgets/widgetwindowagent.h>

namespace {

// 与主窗口一致的标题栏尺寸
constexpr int TITLE_BAR_HEIGHT = 40;
constexpr int SYSTEM_BUTTON_WIDTH = 46;

// 提示文字固定宽度：自动换行的标签要靠定宽才能把高度算准，
// 否则窗口高度会按一行估算，底部按钮被挤出可视区域
constexpr int HINT_WIDTH = 448;

// 内容区整套样式：背景 + 文字 + 输入框 + 按钮。
// 全部写在 contentArea 上（而不是逐个控件 setStyleSheet），
// 里面的选择器只匹配 contentArea 的子控件，不会影响别的窗口
QString contentAreaQss()
{
    QString qss = QStringLiteral(
        "#contentArea { background-color: %1; }"
        "#contentArea QLabel { color: %2; font-size: 13px; background: transparent; }"
        "#contentArea QLabel[hint=\"true\"] { color: %3; font-size: 12px; }"
        "#contentArea QCheckBox { color: %2; font-size: 13px; background: transparent;"
        "    spacing: 8px; }"
        // 勾选框指示器完全自绘：原生样式在样式表环境下会被裁得显示不完整。
        // 方框 + 明显描边，选中变强调色蓝底白勾，悬停描边跟着变
        "#contentArea QCheckBox::indicator { width: 18px; height: 18px;"
        "    border: 2px solid %4; border-radius: 4px; background-color: %5; }"
        "#contentArea QCheckBox::indicator:hover { border-color: %6; }"
        "#contentArea QCheckBox::indicator:checked { border-color: %6;"
        "    background-color: %6;"
        "    image: url(:/QScreenCut/images/fileicons/check.svg); }"
        "#contentArea QCheckBox::indicator:disabled { border-color: %4;"
        "    background-color: %7; }")
        .arg(AppStyle::windowBackground.name(),
             AppStyle::textPrimary.name(),
             AppStyle::textSecondary.name(),
             AppStyle::controlStroke.name(),
             AppStyle::cardBackground.name(),
             AppStyle::accent.name(),
             AppStyle::controlFill.name());

    // 输入框、普通按钮的规则由 AppStyle 统一给出，嵌进来就限定了作用域
    qss += AppStyle::inputQss();
    qss += AppStyle::standardButtonQss();

    // 保存按钮是强调色（蓝底白字），用 id 选择器盖过上面的普通按钮规则
    qss += QStringLiteral(
        "QPushButton#saveButton { background-color: %1; color: #ffffff; border: none;"
        "    border-radius: 4px; padding: 0 18px; font-size: 13px; }"
        "QPushButton#saveButton:hover { background-color: %2; }"
        "QPushButton#saveButton:pressed { background-color: %3; }")
        .arg(AppStyle::accent.name(), AppStyle::accentHover.name(),
             AppStyle::accentPressed.name());

    return qss;
}

}

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(tr("设置"));
    setWindowIcon(QIcon(QStringLiteral(":/QScreenCut/QScreenCut.ico")));
    setFixedSize(520, 700);

    // 多屏时出现在鼠标所在的那块屏幕上：设置一般是从托盘或主窗口点开的，
    // 鼠标就在用户正在用的屏幕上，比固定弹到主屏更顺手
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        move(screen->availableGeometry().center() - rect().center());
    }

    // 无边框窗口的窗口代理：拖动、边缘缩放、系统按钮都由它接管，和主窗口一样
    windowAgent = new QWK::WidgetWindowAgent(this);
    windowAgent->setup(this);

    titleBarWidget = buildTitleBar();
    contentAreaWidget = buildContentArea();

    // 纵向排布：标题栏固定高度，内容占满剩余空间
    auto windowLayout = new QVBoxLayout(this);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(titleBarWidget);
    windowLayout->addWidget(contentAreaWidget, 1);

    // 标题栏样式：半透明感的浅底 + 关闭按钮悬停红
    titleBarWidget->setStyleSheet(QStringLiteral(
        "#titleBar { background-color: %1; border-bottom: 1px solid %2; }"
        "#titleLabel { color: %3; font-size: 12px; }"
        "#titleBar QPushButton { border: none; background-color: transparent; }"
        "#titleBar QPushButton:hover { background-color: %4; }"
        "#titleBar QPushButton#closeButton:hover { background-color: %5; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::titleBarHover.name(),
             AppStyle::dangerHover.name()));
    contentAreaWidget->setStyleSheet(contentAreaQss());

    // 标题栏图标：剪贴板图形 + 关闭按钮，统一用深灰着色
    const QIcon clipboardIcon = loadTintedSvgIcon(
        QStringLiteral(":/QScreenCut/images/fileicons/clipboard-line.svg"),
        AppStyle::iconTint, QSize(64, 64));
    titleIconLabel->setPixmap(clipboardIcon.pixmap(QSize(18, 18)));
    closeButton->setIcon(loadTintedIcon(QStringLiteral(":/QScreenCut/images/关闭1.png"),
                                        AppStyle::iconTint));

    // 关闭即销毁：设置没有"未保存就恢复"之类的状态要保留
    setAttribute(Qt::WA_DeleteOnClose);
}

QWidget *SettingsWindow::buildTitleBar()
{
    auto titleBar = new QWidget(this);
    titleBar->setObjectName("titleBar");
    titleBar->setFixedHeight(TITLE_BAR_HEIGHT);

    auto titleLabel = new QLabel(tr("设置"), titleBar);
    titleLabel->setObjectName("titleLabel");

    // 标题左边的程序小图标，和主窗口用同一个图形
    titleIconLabel = new QLabel(titleBar);
    titleIconLabel->setFixedSize(18, 18);

    closeButton = new QPushButton(titleBar);
    closeButton->setObjectName("closeButton");
    closeButton->setFixedSize(SYSTEM_BUTTON_WIDTH, TITLE_BAR_HEIGHT);
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setToolTip(tr("关闭"));
    closeButton->setIconSize(QSize(16, 16));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(14, 0, 0, 0);
    titleLayout->setSpacing(0);
    titleLayout->addWidget(titleIconLabel);
    titleLayout->addSpacing(8);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(closeButton);

    // 空白区域可拖动；关闭按钮必须显式声明"可命中"，否则点击会被当成拖动
    windowAgent->setTitleBar(titleBar);
    windowAgent->setSystemButton(QWK::WindowAgentBase::Close, closeButton);

    return titleBar;
}

QWidget *SettingsWindow::buildContentArea()
{
    auto contentArea = new QWidget(this);
    contentArea->setObjectName("contentArea");

    auto layout = new QVBoxLayout(contentArea);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(8);

    // —— 截图快捷键 ——
    auto hotkeyLabel = new QLabel(tr("截图快捷键"), contentArea);

    // 启用开关：关掉后不注册全局热键，组合键输入框一起置灰。
    // 已保存的组合键原样保留，重新打开开关就直接恢复
    hotkeyEnabledCheck = new QCheckBox(tr("启用截图快捷键"), contentArea);
    hotkeyEnabledCheck->setChecked(AppSettings::instance().captureHotkeyEnabled());

    // 录制框：限制成只录一个组合键，避免录出 Ctrl+Alt+A 之后再接一段的多步序列
    hotkeyEdit = new QKeySequenceEdit(contentArea);
    hotkeyEdit->setMaximumSequenceLength(1);
    hotkeyEdit->setKeySequence(AppSettings::instance().captureHotkey());
    hotkeyEdit->setFixedHeight(32);

    auto hotkeyHint = new QLabel(tr("点进输入框后直接按下想要的组合键，例如 Ctrl+Alt+A。"), contentArea);
    hotkeyHint->setProperty("hint", true);
    hotkeyHint->setWordWrap(true);
    hotkeyHint->setFixedWidth(HINT_WIDTH);

    // 开关状态联动：关掉时输入框和提示都置灰，一眼能看出当前没在用快捷键
    auto syncHotkeyEditState = [&]() {
        const bool enabled = hotkeyEnabledCheck->isChecked();
        hotkeyEdit->setEnabled(enabled);
        hotkeyHint->setEnabled(enabled);
    };
    syncHotkeyEditState();
    connect(hotkeyEnabledCheck, &QCheckBox::toggled, this, syncHotkeyEditState);

    layout->addWidget(hotkeyLabel);
    layout->addWidget(hotkeyEnabledCheck);
    layout->addWidget(hotkeyEdit);
    layout->addWidget(hotkeyHint);
    layout->addSpacing(14);

    // —— 开机自启 ——
    autoStartCheck = new QCheckBox(tr("开机时自动启动"), contentArea);
    autoStartCheck->setChecked(AppSettings::instance().autoStart());

    auto autoStartHint = new QLabel(
        tr("开机后程序自动驻留在托盘，继续监听剪贴板并响应截图快捷键。"), contentArea);
    autoStartHint->setProperty("hint", true);
    autoStartHint->setWordWrap(true);
    autoStartHint->setFixedWidth(HINT_WIDTH);

    layout->addWidget(autoStartCheck);
    layout->addWidget(autoStartHint);
    layout->addSpacing(14);

    // —— 文字识别与翻译 ——
    auto aiLabel = new QLabel(tr("文字识别与翻译"), contentArea);

    // 翻译引擎：只有"翻译"这条链路需要选，文字提取固定走大模型
    translateEngineBox = new QComboBox(contentArea);
    translateEngineBox->addItem(tr("在线大模型"), QStringLiteral("ai"));
    translateEngineBox->addItem(tr("百度图片翻译"), QStringLiteral("baidu"));
    const int engineIndex =
        translateEngineBox->findData(AppSettings::instance().translateEngine());
    if (engineIndex >= 0) {
        translateEngineBox->setCurrentIndex(engineIndex);
    }
    translateEngineBox->setFixedHeight(32);

    // 语种只给下拉选择：名称和接口的语种代码一一对应，
    // 让用户自己打字很容易敲出接口不认识的写法，翻译时才报错
    translateTargetBox = new QComboBox(contentArea);
    translateTargetBox->addItems(BaiduTranslateClient::targetLanguageNames());
    const int savedTargetIndex =
        translateTargetBox->findText(AppSettings::instance().translateTarget());
    translateTargetBox->setCurrentIndex(savedTargetIndex >= 0 ? savedTargetIndex : 0);
    translateTargetBox->setFixedHeight(32);

    auto commonForm = new QFormLayout;
    commonForm->setContentsMargins(0, 0, 0, 0);
    commonForm->setSpacing(10);
    commonForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    commonForm->addRow(tr("翻译引擎"), translateEngineBox);
    commonForm->addRow(tr("翻译成"), translateTargetBox);

    aiGroup = buildAiGroup();
    baiduGroup = buildBaiduGroup();

    // 切换引擎时只显示对应的配置区，避免窗口里堆一堆用不上的输入框
    connect(translateEngineBox, &QComboBox::currentIndexChanged, this,
            [this]() { syncTranslateEngineUi(); });
    syncTranslateEngineUi();

    layout->addWidget(aiLabel);
    layout->addLayout(commonForm);
    layout->addWidget(aiGroup);
    layout->addWidget(baiduGroup);
    layout->addSpacing(14);

    // —— 底部按钮 ——
    layout->addStretch();

    auto cancelButton = new QPushButton(tr("取消"), contentArea);
    cancelButton->setFixedHeight(32);
    cancelButton->setCursor(Qt::PointingHandCursor);
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);

    auto saveButton = new QPushButton(tr("保存"), contentArea);
    saveButton->setFixedHeight(32);
    saveButton->setCursor(Qt::PointingHandCursor);
    saveButton->setDefault(true);
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        // 校验不通过就留在窗口上，让用户改完再存
        if (!validate()) {
            return;
        }

        emit settingsApplied(hotkeyEnabledCheck->isChecked(),
                             hotkeyEdit->keySequence(),
                             autoStartCheck->isChecked(),
                             aiBaseUrlEdit->text().trimmed(), aiApiKeyEdit->text(),
                             aiModelEdit->text().trimmed(), currentTranslateTarget(),
                             translateEngineBox->currentData().toString(),
                             baiduAppIdEdit->text().trimmed(),
                             baiduSecretKeyEdit->text().trimmed());

        // 保存后窗口保持打开：改动能立刻在设置窗口上核对，
        // 用户可以继续调整，关闭由取消按钮或右上角 X 负责
    });

    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(10);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(saveButton);
    layout->addLayout(buttonLayout);

    return contentArea;
}

// 在线大模型的配置区：只有选了"在线大模型"引擎时才显示
QWidget *SettingsWindow::buildAiGroup()
{
    auto group = new QWidget(this);

    aiBaseUrlEdit = new QLineEdit(AppSettings::instance().aiBaseUrl(), group);
    aiBaseUrlEdit->setPlaceholderText(QStringLiteral("https://api.deepseek.com"));
    aiBaseUrlEdit->setFixedHeight(32);

    aiApiKeyEdit = new QLineEdit(AppSettings::instance().aiApiKey(), group);
    aiApiKeyEdit->setPlaceholderText(tr("在这里粘贴你的 API Key"));
    aiApiKeyEdit->setEchoMode(QLineEdit::Password);
    aiApiKeyEdit->setFixedHeight(32);

    aiModelEdit = new QLineEdit(AppSettings::instance().aiModel(), group);
    aiModelEdit->setPlaceholderText(QStringLiteral("deepseek-flash"));
    aiModelEdit->setFixedHeight(32);

    auto form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("API 地址"), aiBaseUrlEdit);
    form->addRow(tr("API Key"), aiApiKeyEdit);
    form->addRow(tr("模型"), aiModelEdit);

    auto hint = new QLabel(
        tr("文字提取和翻译都由大模型直接看图完成。接口示例（https://api.deepseek.com），"
           "模型必须支持图片输入（例如 deepseek-flash、qwen-vl-max）。"
         ),
        group);
    hint->setProperty("hint", true);
    hint->setWordWrap(true);
    hint->setFixedWidth(HINT_WIDTH);

    auto layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addLayout(form);
    layout->addWidget(hint);

    return group;
}

// 百度图片翻译的配置区：只有选了"百度图片翻译"引擎时才显示
QWidget *SettingsWindow::buildBaiduGroup()
{
    auto group = new QWidget(this);

    baiduAppIdEdit = new QLineEdit(AppSettings::instance().baiduAppId(), group);
    baiduAppIdEdit->setPlaceholderText(tr("百度翻译开放平台里的 APP ID"));
    baiduAppIdEdit->setFixedHeight(32);

    baiduSecretKeyEdit = new QLineEdit(AppSettings::instance().baiduSecretKey(), group);
    baiduSecretKeyEdit->setPlaceholderText(tr("应用信息里的密钥"));
    baiduSecretKeyEdit->setEchoMode(QLineEdit::Password);
    baiduSecretKeyEdit->setFixedHeight(32);

    auto form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(tr("APP ID"), baiduAppIdEdit);
    form->addRow(tr("密钥"), baiduSecretKeyEdit);

    // 在百度翻译开放平台创建应用后可以看到 APP ID 和密钥。
    auto hint = new QLabel(
        tr(""),
        group);
    hint->setProperty("hint", true);
    hint->setWordWrap(true);
    hint->setFixedWidth(HINT_WIDTH);

    auto layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addLayout(form);
    layout->addWidget(hint);

    return group;
}

void SettingsWindow::syncTranslateEngineUi()
{
    const bool useBaidu =
        translateEngineBox->currentData().toString() == QStringLiteral("baidu");
    aiGroup->setVisible(!useBaidu);
    baiduGroup->setVisible(useBaidu);
}

// 用户把语种输入框清空时退回默认值，免得翻译请求里的目标语言是空的
QString SettingsWindow::currentTranslateTarget() const
{
    const QString target = translateTargetBox->currentText().trimmed();
    if (target.isEmpty()) {
        return QStringLiteral("中文");
    }
    return target;
}

bool SettingsWindow::validate()
{
    // 接口配置一律不拦：先存下来，等真正要用的时候再填。
    // 请求时如果缺东西，会在结果窗口里说清楚缺哪一项
    if (!hotkeyEnabledCheck->isChecked()) {
        return true;
    }

    const QKeySequence sequence = hotkeyEdit->keySequence();

    if (sequence.isEmpty()) {
        QMessageBox::warning(this, tr("快捷键为空"), tr("请先按下要使用的组合键。"));
        return false;
    }

    // 只有单个按键（比如 A）时，注册成全局热键会把这个键从所有程序里抢走，
    // 连打字都受影响，所以必须至少带一个修饰键
    const Qt::KeyboardModifiers modifiers =
        sequence[0].keyboardModifiers()
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    if (modifiers == Qt::NoModifier) {
        QMessageBox::warning(this, tr("快捷键不完整"),
                             tr("请至少配合 Ctrl、Alt、Shift 或 Win 键使用。"));
        return false;
    }

    return true;
}

void SettingsWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    QWidget::keyPressEvent(event);
}
