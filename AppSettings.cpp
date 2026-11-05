#include "AppSettings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QSettings>

namespace {

// 注册表里开机启动项的位置和值名。值名同时用作启动项在任务管理器里的显示名
const QString AUTO_START_KEY =
    QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString AUTO_START_VALUE = QStringLiteral("QScreenCut");

// 程序从 QTest 改名而来：老用户注册表里还留着旧值名的启动项，启动时要顺手迁走
const QString LEGACY_AUTO_START_VALUE = QStringLiteral("QTest");

// 配置项的键名，集中在这里方便对照
const QString HOTKEY_KEY = QStringLiteral("capture/hotkey");
const QString HOTKEY_ENABLED_KEY = QStringLiteral("capture/hotkeyEnabled");
const QString AI_BASE_URL_KEY = QStringLiteral("ai/baseUrl");
const QString AI_API_KEY = QStringLiteral("ai/apiKey");
const QString AI_MODEL_KEY = QStringLiteral("ai/model");
const QString TRANSLATE_TARGET_KEY = QStringLiteral("ai/translateTarget");
const QString TRANSLATE_ENGINE_KEY = QStringLiteral("translate/engine");
const QString BAIDU_APP_ID_KEY = QStringLiteral("baidu/appId");
const QString BAIDU_SECRET_KEY_KEY = QStringLiteral("baidu/secretKey");

// 接口配置的默认值。
// 地址和模型给一个 OpenAI 的默认值只是为了让输入框里有个样子可循，
// 用户换成国内服务商的兼容地址也一样能用；Key 必须是用户自己的，留空
const QString DEFAULT_AI_BASE_URL = QStringLiteral("https://api.openai.com/v1");
const QString DEFAULT_AI_MODEL = QStringLiteral("gpt-4o-mini");
const QString DEFAULT_TRANSLATE_TARGET = QStringLiteral("中文");

// 默认继续用大模型：之前已经配好接口的用户升级后行为不变，
// 想用百度图片翻译的到设置界面切一下即可
const QString DEFAULT_TRANSLATE_ENGINE = QStringLiteral("ai");

} // namespace

AppSettings &AppSettings::instance()
{
    // 函数内静态对象：第一次调用时构造，进程退出时自动析构。
    // C++11 起这种初始化是线程安全的，不需要自己加锁。
    static AppSettings settings;
    return settings;
}

AppSettings::AppSettings()
{
    // 配置文件和剪贴板数据库放在程序目录下的 data 子目录。
    // 这样整个工具连同数据可以直接整个目录拷走，不会留下散落在系统里的痕迹。
    const QString folder = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    QDir().mkpath(folder);

    ini = new QSettings(folder + QStringLiteral("/settings.ini"), QSettings::IniFormat);
}

AppSettings::~AppSettings()
{
    // 单例在进程退出时析构，这里释放配置对象
    delete ini;
}

QKeySequence AppSettings::captureHotkey() const
{
    const QString saved = ini->value(HOTKEY_KEY, QStringLiteral("Ctrl+Alt+A")).toString();
    return QKeySequence(saved);
}

void AppSettings::setCaptureHotkey(const QKeySequence &sequence)
{
    // 用可移植写法保存（Ctrl+Alt+A），换个系统语言读回来也还是同一个组合键
    ini->setValue(HOTKEY_KEY, sequence.toString(QKeySequence::PortableText));
    ini->sync();
}

bool AppSettings::captureHotkeyEnabled() const
{
    // 没写过这个键时按启用处理，老配置文件升级后行为不变
    return ini->value(HOTKEY_ENABLED_KEY, true).toBool();
}

void AppSettings::setCaptureHotkeyEnabled(bool enabled)
{
    ini->setValue(HOTKEY_ENABLED_KEY, enabled);
    ini->sync();
}

QString AppSettings::aiBaseUrl() const
{
    return ini->value(AI_BASE_URL_KEY, DEFAULT_AI_BASE_URL).toString();
}

void AppSettings::setAiBaseUrl(const QString &url)
{
    ini->setValue(AI_BASE_URL_KEY, url.trimmed());
    ini->sync();
}

QString AppSettings::aiApiKey() const
{
    // Key 不做任何加工，原样读回来：有的服务会把 Key 前后空格也算作内容
    return ini->value(AI_API_KEY, QString()).toString();
}

void AppSettings::setAiApiKey(const QString &key)
{
    ini->setValue(AI_API_KEY, key);
    ini->sync();
}

QString AppSettings::aiModel() const
{
    return ini->value(AI_MODEL_KEY, DEFAULT_AI_MODEL).toString();
}

void AppSettings::setAiModel(const QString &model)
{
    ini->setValue(AI_MODEL_KEY, model.trimmed());
    ini->sync();
}

QString AppSettings::translateTarget() const
{
    return ini->value(TRANSLATE_TARGET_KEY, DEFAULT_TRANSLATE_TARGET).toString();
}

void AppSettings::setTranslateTarget(const QString &language)
{
    ini->setValue(TRANSLATE_TARGET_KEY, language.trimmed());
    ini->sync();
}

QString AppSettings::translateEngine() const
{
    return ini->value(TRANSLATE_ENGINE_KEY, DEFAULT_TRANSLATE_ENGINE).toString();
}

void AppSettings::setTranslateEngine(const QString &engine)
{
    ini->setValue(TRANSLATE_ENGINE_KEY, engine.trimmed());
    ini->sync();
}

QString AppSettings::baiduAppId() const
{
    // APP ID 是一串数字，去掉用户复制时带上的空白
    return ini->value(BAIDU_APP_ID_KEY, QString()).toString().trimmed();
}

void AppSettings::setBaiduAppId(const QString &appId)
{
    ini->setValue(BAIDU_APP_ID_KEY, appId.trimmed());
    ini->sync();
}

QString AppSettings::baiduSecretKey() const
{
    // 密钥参与签名，任何空白都会让签名对不上，读回来时统一去掉首尾空格
    return ini->value(BAIDU_SECRET_KEY_KEY, QString()).toString().trimmed();
}

void AppSettings::setBaiduSecretKey(const QString &key)
{
    ini->setValue(BAIDU_SECRET_KEY_KEY, key.trimmed());
    ini->sync();
}

bool AppSettings::autoStart() const
{
    QSettings autoStartKey(AUTO_START_KEY, QSettings::NativeFormat);
    return autoStartKey.contains(AUTO_START_VALUE);
}

void AppSettings::setAutoStart(bool enabled)
{
    QSettings autoStartKey(AUTO_START_KEY, QSettings::NativeFormat);

    if (!enabled) {
        autoStartKey.remove(AUTO_START_VALUE);
        autoStartKey.sync();
        return;
    }

    // 程序路径里通常带空格，不加引号时 Windows 会把空格前的那段当成程序名
    const QString path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    autoStartKey.setValue(AUTO_START_VALUE, QStringLiteral("\"%1\"").arg(path));
    autoStartKey.sync();
}

void AppSettings::refreshAutoStartPath()
{
    // 改名迁移：旧值名的启动项先搬到新值名下（老路径原样保留，
    // 下面 setAutoStart 会用当前 exe 路径重写），避免新旧两个启动项并存
    QSettings autoStartKey(AUTO_START_KEY, QSettings::NativeFormat);
    if (autoStartKey.contains(LEGACY_AUTO_START_VALUE)) {
        const QString legacyPath = autoStartKey.value(LEGACY_AUTO_START_VALUE).toString();
        autoStartKey.remove(LEGACY_AUTO_START_VALUE);
        if (!autoStartKey.contains(AUTO_START_VALUE)) {
            autoStartKey.setValue(AUTO_START_VALUE, legacyPath);
        }
        autoStartKey.sync();
    }

    // 没有开启自启就没什么可校正的
    if (!autoStart()) {
        return;
    }

    // 直接重写一遍，值里就是当前真实的 exe 路径
    setAutoStart(true);
}
