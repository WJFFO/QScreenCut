#pragma once

#include <QColor>
#include <QString>
#include <QtWidgets/QApplication>

// Fluent Design 配色统一管理（WinUI 3 默认浅色主题）。
// 各窗口的样式表都从这里取颜色，保证风格一致；
// 改配色只动这一个文件，所有界面跟着变。
namespace AppStyle {

// 强调色：Windows 11 默认蓝
inline const QColor accent = QColor(0x00, 0x78, 0xd4);
inline const QColor accentHover = QColor(0x10, 0x6e, 0xbe);
inline const QColor accentPressed = QColor(0x00, 0x5a, 0x9e);

// 关闭按钮悬停红（Windows 11 标准色）
inline const QColor dangerHover = QColor(0xc4, 0x2b, 0x1c);

// 窗口底色、标题栏、卡片等基础颜色
inline const QColor windowBackground = QColor(0xf3, 0xf3, 0xf3);
inline const QColor titleBarFill = QColor(0xf9, 0xf9, 0xf9);
inline const QColor cardBackground = QColor(0xff, 0xff, 0xff);
inline const QColor cardStroke = QColor(0xe5, 0xe5, 0xe5);
inline const QColor controlFill = QColor(0xfd, 0xfd, 0xfd);
inline const QColor controlStroke = QColor(0xd1, 0xd1, 0xd1);
inline const QColor controlHover = QColor(0xf6, 0xf6, 0xf6);
inline const QColor textPrimary = QColor(0x1b, 0x1b, 0x1b);
inline const QColor textSecondary = QColor(0x61, 0x61, 0x61);
inline const QColor itemHover = QColor(0xf5, 0xf5, 0xf5);
inline const QColor itemSelected = QColor(0xcf, 0xe8, 0xf6);      // 强调色 20% 左右的浅蓝
inline const QColor titleBarHover = QColor(0xe9, 0xe9, 0xe9);
inline const QColor listSeparator = QColor(0xf0, 0xf0, 0xf0);
inline const QColor iconTint = QColor(0x2b, 0x2b, 0x2b);          // 标题栏、工具条图标的着色

// 强调色按钮（主操作）：蓝底白字
inline QString accentButtonQss()
{
    return QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none;"
        "              border-radius: 4px; padding: 0 18px; font-size: 13px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }")
        .arg(accent.name(), accentHover.name(), accentPressed.name());
}

// 普通按钮（次级操作）：面板底色 + 灰边
inline QString standardButtonQss()
{
    return QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3;"
        "              border-radius: 4px; padding: 0 18px; font-size: 13px; }"
        "QPushButton:hover { background-color: %4; }")
        .arg(controlFill.name(), textPrimary.name(), controlStroke.name(), controlHover.name());
}

// 输入类控件：面板底色 + 圆角，聚焦时描边换成强调色。
// 下拉框的弹出列表要单独覆盖：原生样式会把它画成白色、和面板风格脱节，
// 所以这里连弹出容器（QComboBoxPrivateContainer）、列表视图和列表项一起定死颜色
inline QString inputQss()
{
    return QStringLiteral(
        "QLineEdit, QComboBox, QKeySequenceEdit { background-color: %1; color: %2;"
        "    border: 1px solid %3; border-radius: 4px; padding: 2px 10px; font-size: 13px; }"
        "QLineEdit:focus, QComboBox:focus, QKeySequenceEdit:focus { border: 1px solid %4; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox QAbstractItemView, QComboBoxPrivateContainer"
        "    { background-color: %1; color: %2; border: 1px solid %3;"
        "      outline: none; }"
        "QComboBox QAbstractItemView::item { min-height: 26px; padding: 2px 8px; }"
        "QComboBox QAbstractItemView::item:hover { background-color: %6; }"
        "QComboBox QAbstractItemView::item:selected { background-color: %5; color: %2; }")
        .arg(cardBackground.name(), textPrimary.name(), cardStroke.name(),
             accent.name(), itemSelected.name(), itemHover.name());
}

}  // namespace AppStyle
