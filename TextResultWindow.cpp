#include "TextResultWindow.h"

#include "AppSettings.h"
#include "AppStyle.h"
#include "BaiduTranslateClient.h"
#include "IconUtils.h"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {

// 顶部标题栏的高度。这个区域按住可以拖动窗口，按钮不受影响
constexpr int TITLE_BAR_HEIGHT = 40;

constexpr int WINDOW_WIDTH = 620;
constexpr int WINDOW_HEIGHT = 460;

}

TextResultWindow::TextResultWindow(const QImage &image, AiClient::Task task, QWidget *parent)
    : QWidget(parent)
{
    // 无边框 + 置顶 + Tool：浮在当前工作内容上方，又不占任务栏的位置
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("resultWindow"));

    // Fluent 浅色主题：浅灰窗口、白底文本卡片、强调色主按钮
    setStyleSheet(QStringLiteral(
        "#resultWindow { background-color: %1; border: 1px solid %2; }"
        "#resultWindow QLabel#titleLabel { color: %3; font-size: 13px; font-weight: 600; }"
        "#resultWindow QPushButton#closeButton { border: none; border-radius: 4px;"
        "                                        background-color: transparent; }"
        "#resultWindow QPushButton#closeButton:hover { background-color: #c42b1c; }"
        "#resultWindow QPlainTextEdit { background-color: %4; color: %3; border: 1px solid %2;"
        "                                border-radius: 8px; padding: 10px; font-size: 13px;"
        "                                selection-background-color: %5; }"
        "#resultWindow QPushButton#copyButton { background-color: %5; color: #ffffff;"
        "                                       border: none; border-radius: 4px;"
        "                                       padding: 0 20px; font-size: 13px; }"
        "#resultWindow QPushButton#copyButton:hover { background-color: %6; }"
        "#resultWindow QLabel#statusLabel { color: %7; font-size: 12px; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::cardBackground.name(),
             AppStyle::accent.name(), AppStyle::accentHover.name(),
             AppStyle::textSecondary.name()));

    const bool translating = task == AiClient::Task::Translate;

    // 窗口本身没有系统标题栏，这个标题只用于任务切换器和调试工具里辨认
    setWindowTitle(translating ? tr("翻译结果") : tr("识别的文字"));

    auto titleLabel = new QLabel(translating ? tr("翻译结果") : tr("识别的文字"), this);
    titleLabel->setObjectName(QStringLiteral("titleLabel"));

    auto closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setFixedSize(28, 28);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setIcon(loadTintedIcon(QStringLiteral(":/QScreenCut/images/关闭1.png"),
                                        AppStyle::iconTint));
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setToolTip(tr("关闭（Esc）"));

    auto titleBar = new QHBoxLayout;
    titleBar->setContentsMargins(16, 10, 10, 6);
    titleBar->setSpacing(0);
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    titleBar->addWidget(closeButton);

    textEdit = new QPlainTextEdit(this);
    textEdit->setReadOnly(true);
    textEdit->setPlaceholderText(translating ? tr("等待翻译结果…") : tr("等待识别结果…"));

    // 文本区左右各留 16 像素，和标题栏对齐
    auto textArea = new QWidget(this);
    auto textLayout = new QVBoxLayout(textArea);
    textLayout->setContentsMargins(16, 0, 16, 0);
    textLayout->addWidget(textEdit);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);

    auto copyButton = new QPushButton(tr("复制"), this);
    copyButton->setObjectName(QStringLiteral("copyButton"));
    copyButton->setCursor(Qt::PointingHandCursor);
    copyButton->setFixedHeight(28);

    auto bottomBar = new QHBoxLayout;
    bottomBar->setContentsMargins(16, 8, 16, 12);
    bottomBar->setSpacing(12);
    bottomBar->addWidget(statusLabel, 1);
    bottomBar->addWidget(copyButton);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(titleBar);
    layout->addWidget(textArea, 1);
    layout->addLayout(bottomBar);

    resize(WINDOW_WIDTH, WINDOW_HEIGHT);

    // 出现在鼠标所在的那块屏幕上，正好是用户刚才框选的位置
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen != nullptr) {
        move(screen->availableGeometry().center() - rect().center());
    }

    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        const QString text = textEdit->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        showStatus(tr("已复制到剪贴板"), false);
    });

    // 保存当前任务
    this->task = task;

    const AppSettings &settings = AppSettings::instance();
    const bool aiReady = !settings.aiApiKey().trimmed().isEmpty();
    const bool baiduReady = !settings.baiduAppId().trimmed().isEmpty()
                            && !settings.baiduSecretKey().trimmed().isEmpty();
    const bool preferBaidu = settings.translateEngine() == QStringLiteral("baidu");

    if (task == AiClient::Task::RecognizeText) {
        // 识别优先用大模型。没配大模型时用百度图片翻译顶上：
        // 它返回的 sumSrc 就是识别出的原文，正好当识别结果用
        if (aiReady) {
            showStatus(tr("正在识别文字，请稍候…"), false);
            ensureClient();
            client->send(image, task);
            return;
        }
        if (baiduReady) {
            showStatus(tr("正在识别文字（百度图片翻译）…"), false);
            ensureBaiduClient();
            baiduClient->send(image, true);
            return;
        }
        showFailure(tr("还没有配置文字识别服务：在设置里填写大模型接口，"
                       "或者百度翻译的 APP ID 和密钥。"));
        return;
    }

    // 翻译：按设置里的引擎走，选中的那条没配好就自动换另一条
    if (preferBaidu && baiduReady) {
        showStatus(tr("正在翻译，请稍候…"), false);
        ensureBaiduClient();
        baiduClient->send(image);
        return;
    }
    if (aiReady) {
        showStatus(tr("正在翻译，请稍候…"), false);
        ensureClient();
        client->send(image, task);
        return;
    }
    if (baiduReady) {
        showStatus(tr("正在翻译（百度图片翻译）…"), false);
        ensureBaiduClient();
        baiduClient->send(image);
        return;
    }
    showFailure(tr("还没有配置翻译服务：在设置里填写大模型接口，"
                   "或者百度翻译的 APP ID 和密钥。"));
}

void TextResultWindow::ensureClient()
{
    if (client != nullptr) {
        return;
    }

    client = new AiClient(this);
    connect(client, &AiClient::finished, this, [this](const QString &text) {
        setResultText(text);
    });
    connect(client, &AiClient::failed, this, [this](const QString &message) {
        showFailure(message);
    });
}

void TextResultWindow::ensureBaiduClient()
{
    if (baiduClient != nullptr) {
        return;
    }

    baiduClient = new BaiduTranslateClient(this);
    connect(baiduClient, &BaiduTranslateClient::finished, this, [this](const QString &text) {
        setResultText(text);
    });
    connect(baiduClient, &BaiduTranslateClient::failed, this, [this](const QString &message) {
        showFailure(message);
    });
}

void TextResultWindow::setResultText(const QString &text)
{
    textEdit->setPlainText(text);
    showStatus(tr("完成，共 %1 个字符").arg(text.size()), false);
}

void TextResultWindow::showFailure(const QString &message)
{
    showStatus(message, true);

    // 出错原因可能比较长，也放进文本区，方便直接复制出来排查
    if (textEdit->toPlainText().isEmpty()) {
        textEdit->setPlainText(message);
    }
}

void TextResultWindow::showStatus(const QString &text, bool isError)
{
    statusLabel->setText(text);

    // 出错用红色，正常状态用灰色，一眼能分清是结果还是问题
    if (isError) {
        statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                       .arg(AppStyle::dangerHover.name()));
        return;
    }
    statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                   .arg(AppStyle::textSecondary.name()));
}

void TextResultWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || event->pos().y() > TITLE_BAR_HEIGHT) {
        QWidget::mousePressEvent(event);
        return;
    }

    dragging = true;
    dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
}

void TextResultWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    move(event->globalPosition().toPoint() - dragOffset);
}

void TextResultWindow::mouseReleaseEvent(QMouseEvent *event)
{
    dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void TextResultWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    QWidget::keyPressEvent(event);
}
