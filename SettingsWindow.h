#pragma once

#include <QtWidgets/QWidget>
#include <QKeySequence>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;

namespace QWK {
    class WidgetWindowAgent;
}

// 设置窗口：和主窗口同款的独立无边框窗口，标题栏自绘、拖动交给 QWindowKit。
// 不再是模态弹窗——打开后可以同时操作主窗口。
// 界面上的值改完点"保存"，通过 settingsApplied 信号把整份配置交给主窗口去应用。
class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr);

signals:
    // 用户点了保存且通过校验，参数是界面上的当前值。
    // hotkeyEnabled 为 false 时界面上保存的组合键原样保留但不注册
    void settingsApplied(bool hotkeyEnabled, const QKeySequence &hotkey, bool autoStart,
                         const QString &aiBaseUrl, const QString &aiApiKey,
                         const QString &aiModel, const QString &translateTarget,
                         const QString &translateEngine, const QString &baiduAppId,
                         const QString &baiduSecretKey);

protected:
    // Esc 关闭窗口
    void keyPressEvent(QKeyEvent *event) override;

private:
    QWidget *buildTitleBar();
    QWidget *buildContentArea();
    QWidget *buildAiGroup();      // 在线大模型的配置区
    QWidget *buildBaiduGroup();   // 百度图片翻译的配置区
    bool validate();              // 保存前检查，不通过就弹提示并返回 false

    // 语种下拉框的当前值；用户清空输入时退回默认的"中文"
    QString currentTranslateTarget() const;

    // 按当前选的翻译引擎显示对应的配置区
    void syncTranslateEngineUi();

    QWK::WidgetWindowAgent *windowAgent = nullptr;

    QWidget *titleBarWidget = nullptr;
    QWidget *contentAreaWidget = nullptr;
    QLabel *titleIconLabel = nullptr;
    QPushButton *closeButton = nullptr;

    QCheckBox *hotkeyEnabledCheck = nullptr;
    QKeySequenceEdit *hotkeyEdit = nullptr;
    QCheckBox *autoStartCheck = nullptr;
    QComboBox *translateEngineBox = nullptr;
    QWidget *aiGroup = nullptr;
    QWidget *baiduGroup = nullptr;
    QLineEdit *aiBaseUrlEdit = nullptr;
    QLineEdit *aiApiKeyEdit = nullptr;
    QLineEdit *aiModelEdit = nullptr;
    QLineEdit *baiduAppIdEdit = nullptr;
    QLineEdit *baiduSecretKeyEdit = nullptr;
    QComboBox *translateTargetBox = nullptr;
};
