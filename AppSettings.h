#pragma once

#include <QtGui/QKeySequence>

class QSettings;

// 程序配置的唯一入口。
//
// 用单例是因为配置会被多个地方读到：主窗口启动时注册热键、设置界面打开时
// 回填当前值、托盘菜单显示快捷键文字。如果每处都自己 new 一个 QSettings，
// 很容易出现某个地方改了、别的地方还拿着旧值的情况。
//
// 采用函数内静态对象的写法（Meyers 单例）：第一次调用 instance() 时才构造，
// 进程结束时自动析构，不需要手动 new/delete，也不需要额外加锁。
class AppSettings
{
public:
    static AppSettings &instance();

    // 截图快捷键，默认 Ctrl+Alt+A
    QKeySequence captureHotkey() const;
    void setCaptureHotkey(const QKeySequence &sequence);

    // 截图快捷键是否启用。禁用后不注册全局热键，但保存的组合键原样保留，
    // 重新启用时直接用。托盘单击截图不受它影响
    bool captureHotkeyEnabled() const;
    void setCaptureHotkeyEnabled(bool enabled);

    // 文字识别 / 翻译用的接口配置。
    // 只支持 OpenAI 兼容的 /chat/completions 格式，地址填到 /v1 这一层就行，
    // 结尾要不要带 /chat/completions 都能用
    QString aiBaseUrl() const;
    void setAiBaseUrl(const QString &url);

    QString aiApiKey() const;
    void setAiApiKey(const QString &key);

    // 模型名。必须是支持图片输入的视觉模型，纯文本模型读不了截图
    QString aiModel() const;
    void setAiModel(const QString &model);

    // 翻译的目标语言，默认中文
    QString translateTarget() const;
    void setTranslateTarget(const QString &language);

    // 翻译引擎：ai（在线大模型，默认）或 baidu（百度图片翻译）。
    // 文字提取固定走大模型，这里只影响"翻译"
    QString translateEngine() const;
    void setTranslateEngine(const QString &engine);

    // 百度图片翻译的凭据。APP ID 和密钥在百度翻译开放平台的应用信息里，
    // 注意密钥不是"API Key"，两者不能混用
    QString baiduAppId() const;
    void setBaiduAppId(const QString &appId);

    QString baiduSecretKey() const;
    void setBaiduSecretKey(const QString &key);

    // 开机自动启动。以注册表里的启动项为准，不额外存一份配置，避免两边不一致
    bool autoStart() const;
    void setAutoStart(bool enabled);

    // 程序被移到别的目录后，启动项还指着旧路径，启动时顺手改正
    void refreshAutoStartPath();

private:
    AppSettings();
    ~AppSettings();

    // 单例不允许复制，避免出现第二份配置对象
    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    QSettings *ini = nullptr;
};
