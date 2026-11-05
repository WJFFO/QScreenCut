#include "EntryDetailDialog.h"

#include "AppStyle.h"
#include "ThumbnailUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>

namespace {

// 一条文件信息的排版：名字、大小、路径各占一行，条目之间空一行
QString describeFile(const QString &path)
{
    const QFileInfo info(path);
    QString description = info.fileName() + QLatin1Char('\n');

    if (!info.exists()) {
        description += QObject::tr("文件已不存在");
    } else if (info.isDir()) {
        description += QObject::tr("文件夹");
    } else {
        description += formatFileSize(info.size());
    }

    description += QLatin1Char('\n') + QDir::toNativeSeparators(path);
    return description;
}

}

EntryDetailDialog::EntryDetailDialog(qint64 entryId, QWidget *parent)
    : QDialog(parent)
{
    entry = loadClipboardEntry(entryId);

    setWindowTitle(tr("详情"));
    setFixedWidth(480);

    // Fluent 浅色：灰底窗口，内容放在白色卡片控件上
    setStyleSheet(QStringLiteral("QDialog { background-color: %1; }")
                      .arg(AppStyle::windowBackground.name()));

    mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 14);
    mainLayout->setSpacing(10);

    buildHeader();

    if (entry.kind == QStringLiteral("text")) {
        buildTextPage(entry.content);
    } else if (entry.kind == QStringLiteral("image")) {
        buildImagePage(entry.content);
    } else if (entry.kind == QStringLiteral("files")) {
        buildFilesPage(entry.content);
    }

    buildButtons();
}

void EntryDetailDialog::buildHeader()
{
    QString kindLabel = tr("文本");
    if (entry.kind == QStringLiteral("image")) {
        kindLabel = tr("图片");
    } else if (entry.kind == QStringLiteral("files")) {
        kindLabel = tr("文件");
    }

    auto headerLabel = new QLabel(
        QStringLiteral("%1 · %2").arg(kindLabel,
                                      entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
        this);
    headerLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 600;")
                                   .arg(AppStyle::textPrimary.name()));
    mainLayout->addWidget(headerLabel);
}

void EntryDetailDialog::buildTextPage(const QByteArray &content)
{
    auto textEdit = new QPlainTextEdit(this);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(QString::fromUtf8(content));
    textEdit->setFixedHeight(300);
    textEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
        "                  border-radius: 8px; padding: 8px; font-size: 13px; }")
        .arg(AppStyle::cardBackground.name(), AppStyle::textPrimary.name(),
             AppStyle::cardStroke.name()));
    mainLayout->addWidget(textEdit, 1);
}

void EntryDetailDialog::buildImagePage(const QByteArray &content)
{
    const QImage image = QImage::fromData(content, "PNG");

    if (image.isNull()) {
        auto brokenLabel = new QLabel(tr("缩略图数据无法解析"), this);
        mainLayout->addWidget(brokenLabel);
        return;
    }

    auto imageLabel = new QLabel(this);
    imageLabel->setPixmap(QPixmap::fromImage(image));
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet(QStringLiteral(
        "QLabel { background-color: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(AppStyle::cardBackground.name(), AppStyle::cardStroke.name()));
    mainLayout->addWidget(imageLabel);

    auto infoLabel = new QLabel(
        tr("尺寸 %1 × %2\n缩略图数据 %3").arg(image.width()).arg(image.height())
            .arg(formatFileSize(content.size())),
        this);
    infoLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                 .arg(AppStyle::textSecondary.name()));
    mainLayout->addWidget(infoLabel);
}

void EntryDetailDialog::buildFilesPage(const QByteArray &content)
{
    const QJsonArray paths = QJsonDocument::fromJson(content).array();

    QStringList pathList;
    for (const QJsonValue &value : paths) {
        const QString path = value.toString();
        if (!path.isEmpty()) {
            pathList.append(path);
        }
    }

    // 图片和视频文件每张都给一个预览，列表上只看得到第一张，全貌在这里
    const QStringList previewPaths = findPreviewFiles(pathList);
    if (!previewPaths.isEmpty()) {
        buildPreviewGrid(previewPaths);
    }

    // 每个文件一段：名字、大小、路径。文件多时可以滚动
    QString info;
    for (const QString &path : pathList) {
        if (!info.isEmpty()) {
            info += QStringLiteral("\n\n");
        }
        info += describeFile(path);
    }

    auto infoEdit = new QPlainTextEdit(this);
    infoEdit->setReadOnly(true);
    infoEdit->setPlainText(info);
    infoEdit->setFixedHeight(150);
    infoEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3;"
        "                  border-radius: 8px; padding: 8px; font-size: 12px; }")
        .arg(AppStyle::cardBackground.name(), AppStyle::textPrimary.name(),
             AppStyle::cardStroke.name()));
    mainLayout->addWidget(infoEdit, 1);
}

void EntryDetailDialog::buildButtons()
{
    auto buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(10);

    statusLabel = new QLabel(this);
    statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;")
                                   .arg(AppStyle::textSecondary.name()));

    auto restoreButton = new QPushButton(tr("复制到剪贴板"), this);
    restoreButton->setFixedHeight(32);
    restoreButton->setCursor(Qt::PointingHandCursor);
    restoreButton->setStyleSheet(AppStyle::accentButtonQss());

    auto closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setFixedHeight(32);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(AppStyle::standardButtonQss());

    buttonLayout->addWidget(statusLabel, 1);
    buttonLayout->addWidget(restoreButton);
    buttonLayout->addWidget(closeButton);

    mainLayout->addLayout(buttonLayout);

    connect(restoreButton, &QPushButton::clicked, this, [this]() {
        emit restoreRequested(entry.id);
        statusLabel->setText(tr("已复制到剪贴板"));
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

QStringList EntryDetailDialog::findPreviewFiles(const QStringList &paths) const
{
    QStringList previewPaths;

    for (const QString &path : paths) {
        if (isPreviewableFile(path)) {
            previewPaths.append(path);
        }
    }

    return previewPaths;
}

void EntryDetailDialog::buildPreviewGrid(const QStringList &paths)
{
    constexpr int COLUMNS = 4;
    constexpr int CELL_SIZE = 96;
    constexpr int CELL_PADDING = 4;

    // 最多显示两行，多了就滚动：弹窗不会被一堆预览图撑得比屏幕还高
    const int visibleRows = qMin(2, (paths.size() + COLUMNS - 1) / COLUMNS);

    auto scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFixedHeight(visibleRows * CELL_SIZE + (visibleRows - 1) * 8);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto gridHost = new QWidget(scrollArea);
    auto grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    // 多余宽度全给右边空列，四列预览保持固定宽度、间距一致
    grid->setColumnStretch(COLUMNS, 1);

    for (int index = 0; index < paths.size(); ++index) {
        const QString path = paths.at(index);

        auto cell = new QLabel(gridHost);
        cell->setFixedSize(CELL_SIZE, CELL_SIZE);
        cell->setAlignment(Qt::AlignCenter);
        cell->setToolTip(QFileInfo(path).fileName());
        // 图片四周留出内边距，卡片的底色和圆角边框才露得出来
        cell->setStyleSheet(QStringLiteral(
            "QLabel { background-color: %1; border: 1px solid %2; border-radius: 8px;"
            "         padding: %3px; color: %4; font-size: 12px; }")
            .arg(AppStyle::cardBackground.name(), AppStyle::cardStroke.name())
            .arg(CELL_PADDING)
            .arg(AppStyle::textSecondary.name()));

        const QImage preview = loadFilePreview(path, CELL_SIZE * 2);
        if (preview.isNull()) {
            cell->setText(tr("无预览"));
        } else {
            const int contentSize = CELL_SIZE - CELL_PADDING * 2 - 2;
            cell->setPixmap(QPixmap::fromImage(preview.scaled(
                contentSize, contentSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        }

        grid->addWidget(cell, index / COLUMNS, index % COLUMNS);
    }

    scrollArea->setWidget(gridHost);
    mainLayout->addWidget(scrollArea);
}
