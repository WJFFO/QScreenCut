#include "BaiduTranslateClient.h"

#include "AppSettings.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>
#include <QtDebug>

namespace {

const QString API_URL = QStringLiteral("https://fanyi-api.baidu.com/api/trans/sdk/picture");

// 文档规定的固定值，缺一个就会报 54000 / 69008
const QString FIXED_CUID = QStringLiteral("APICUID");
const QString FIXED_MAC = QStringLiteral("mac");
const QString FIXED_VERSION = QStringLiteral("3");

// 接口限制：最长边不超过 4096、最短边不少于 30、长宽比 3:1 以内、体积 4M 以内。
// 这里先缩到 2000 像素：翻译用不上更高分辨率，体积也稳稳低于上限
constexpr int MAX_IMAGE_EDGE = 2000;
constexpr int MIN_IMAGE_EDGE = 40;
constexpr int MAX_ASPECT_DIVISOR = 3;
constexpr int JPEG_QUALITY = 92;
constexpr int REQUEST_TIMEOUT = 60000;

// 接口支持的目标语种：名称和语种代码一一对应。
// 设置界面的下拉框直接用这份名字，避免两边各维护一份、改了一处漏了另一处
const QVector<QPair<QString, QString>> &languageTable()
{
    static const QVector<QPair<QString, QString>> table = {
        {QStringLiteral("中文"), QStringLiteral("zh")},
        {QStringLiteral("英文"), QStringLiteral("en")},
        {QStringLiteral("日文"), QStringLiteral("jp")},
        {QStringLiteral("韩文"), QStringLiteral("kor")},
        {QStringLiteral("法文"), QStringLiteral("fra")},
        {QStringLiteral("西班牙文"), QStringLiteral("spa")},
        {QStringLiteral("俄文"), QStringLiteral("ru")},
        {QStringLiteral("葡萄牙文"), QStringLiteral("pt")},
        {QStringLiteral("德文"), QStringLiteral("de")},
        {QStringLiteral("意大利文"), QStringLiteral("it")},
        {QStringLiteral("丹麦语"), QStringLiteral("dan")},
        {QStringLiteral("荷兰语"), QStringLiteral("nl")},
        {QStringLiteral("马来语"), QStringLiteral("may")},
        {QStringLiteral("瑞典语"), QStringLiteral("swe")},
        {QStringLiteral("印尼语"), QStringLiteral("id")},
        {QStringLiteral("波兰语"), QStringLiteral("pl")},
        {QStringLiteral("罗马尼亚语"), QStringLiteral("rom")},
        {QStringLiteral("土耳其语"), QStringLiteral("tr")},
        {QStringLiteral("希腊语"), QStringLiteral("el")},
        {QStringLiteral("匈牙利语"), QStringLiteral("hu")},
    };
    return table;
}

// 老配置里可能存过"英语""简体中文"这类说法，认一下，免得升级后翻译突然用不了
QString aliasLanguageCode(const QString &name)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("简体中文"), QStringLiteral("zh")},
        {QStringLiteral("英语"), QStringLiteral("en")},
        {QStringLiteral("日语"), QStringLiteral("jp")},
        {QStringLiteral("韩语"), QStringLiteral("kor")},
        {QStringLiteral("法语"), QStringLiteral("fra")},
        {QStringLiteral("西班牙语"), QStringLiteral("spa")},
        {QStringLiteral("俄语"), QStringLiteral("ru")},
        {QStringLiteral("葡萄牙语"), QStringLiteral("pt")},
        {QStringLiteral("德语"), QStringLiteral("de")},
        {QStringLiteral("意大利语"), QStringLiteral("it")},
    };
    return aliases.value(name);
}

// 设置界面里的语种名称 -> 百度语种代码。
// 名称不认识时返回空，上层会给出明确提示，不猜
QString languageCode(const QString &target)
{
    const QString key = target.trimmed();

    for (const auto &entry : languageTable()) {
        if (entry.first == key) {
            return entry.second;
        }
    }

    const QString alias = aliasLanguageCode(key);
    if (!alias.isEmpty()) {
        return alias;
    }

    // 用户直接填了语种代码（zh / en / jp …）时原样使用，接口自己会校验
    if (key.size() == 2 || key.size() == 3) {
        return key.toLower();
    }

    return QString();
}

// 按接口要求整理图片：缩到合适尺寸、补足长宽比、统一转成 JPEG。
// 返回的字节就是要上传的内容，签名也是对这份字节做 md5
QByteArray prepareImage(const QImage &source)
{
    QImage image = source;

    // 超大截图先等比缩下来
    if (image.width() > MAX_IMAGE_EDGE || image.height() > MAX_IMAGE_EDGE) {
        image = image.scaled(MAX_IMAGE_EDGE, MAX_IMAGE_EDGE,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // 长条形的选区（整行文字、侧边栏）会被 3:1 的限制拒掉，某条边太短也不行。
    // 用白底补齐既能过校验，又不影响取文字
    int targetWidth = image.width();
    int targetHeight = image.height();
    const int minWidth = qMax(image.height() / MAX_ASPECT_DIVISOR, MIN_IMAGE_EDGE);
    const int minHeight = qMax(image.width() / MAX_ASPECT_DIVISOR, MIN_IMAGE_EDGE);
    if (targetWidth < minWidth) {
        targetWidth = minWidth;
    }
    if (targetHeight < minHeight) {
        targetHeight = minHeight;
    }

    if (targetWidth != image.width() || targetHeight != image.height()) {
        QImage padded(targetWidth, targetHeight, QImage::Format_RGB32);
        padded.fill(Qt::white);
        QPainter painter(&padded);
        painter.drawImage(0, 0, image);
        painter.end();
        image = padded;
    }

    // JPEG 不支持透明通道，带 alpha 的截图直接存会出黑底
    if (image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", JPEG_QUALITY);
    buffer.close();

    // 编码失败时把原因写进日志：JPEG 编码依赖发布目录里的 imageformats/qjpeg.dll，
    // 少了它只会得到一句"截图编码失败"，看不出是部署漏了插件
    if (bytes.isEmpty()) {
        qWarning() << "截图编码成 JPEG 失败，检查发布目录是否缺少 imageformats/qjpeg.dll";
    }

    return bytes;
}

// 错误码转成人话。百度返回的 error_msg 大多是英文短语，
// 用户看不懂也不知道该改什么，常见几种在这里直接说明白
QString describeError(const QString &code, const QString &message)
{
    if (code == QStringLiteral("52003")) {
        return QObject::tr("APP ID 不正确，或者图片翻译服务还没有开通。请到百度翻译开放平台确认。");
    }
    if (code == QStringLiteral("54001")) {
        return QObject::tr("签名错误，密钥可能填错了。注意这里要填应用的密钥，不是 API Key。");
    }
    if (code == QStringLiteral("54003")) {
        return QObject::tr("调用太频繁了，稍等一会儿再试。");
    }
    if (code == QStringLiteral("54004")) {
        return QObject::tr("账户余额不足，请到百度翻译控制台充值后再用。");
    }
    if (code == QStringLiteral("58001")) {
        return QObject::tr("目标语种不支持，请换一个翻译语言。");
    }
    if (code == QStringLiteral("69004")) {
        return QObject::tr("图片里没有识别到文字。");
    }
    if (code == QStringLiteral("69005")) {
        return QObject::tr("图片超过 4M，接口拒收。");
    }
    if (code == QStringLiteral("69006")) {
        return QObject::tr("图片尺寸不符合要求（最短边至少 30px，最长边最多 4096px）。");
    }
    if (code == QStringLiteral("69007")) {
        return QObject::tr("图片格式不支持，只接受 png 和 jpg。");
    }

    return QObject::tr("百度接口返回错误 %1：%2").arg(code, message);
}

}

BaiduTranslateClient::BaiduTranslateClient(QObject *parent)
    : QObject(parent)
    , network(new QNetworkAccessManager(this))
{
}

QStringList BaiduTranslateClient::targetLanguageNames()
{
    QStringList names;
    for (const auto &entry : languageTable()) {
        names.append(entry.first);
    }
    return names;
}

void BaiduTranslateClient::send(const QImage &image, bool extractSourceOnly)
{
    if (image.isNull()) {
        emit failed(tr("截图内容为空，没法处理"));
        return;
    }

    const AppSettings &settings = AppSettings::instance();

    const QString appId = settings.baiduAppId().trimmed();
    if (appId.isEmpty()) {
        emit failed(tr("还没有填写百度翻译的 APP ID，请打开设置界面填写后再试"));
        return;
    }

    const QString secretKey = settings.baiduSecretKey().trimmed();
    if (secretKey.isEmpty()) {
        emit failed(tr("还没有填写百度翻译的密钥，请打开设置界面填写后再试"));
        return;
    }

    QString to = languageCode(settings.translateTarget());
    if (to.isEmpty()) {
        if (!extractSourceOnly) {
            emit failed(tr("百度图片翻译不支持“%1”这个语言，请换一个，"
                           "或者把翻译引擎切换成在线大模型。")
                            .arg(settings.translateTarget()));
            return;
        }
        // 只取原文时目标语种用不上，给个接口认的默认值就行
        to = QStringLiteral("en");
    }

    const QByteArray imageBytes = prepareImage(image);
    if (imageBytes.isEmpty()) {
        emit failed(tr("截图编码失败，没法上传"));
        return;
    }

    // salt 用毫秒时间戳：接口只要求是个随机数，时间戳天然不重复
    const QString salt = QString::number(QDateTime::currentMSecsSinceEpoch());

    // 签名规则：md5(appid + md5(image) + salt + cuid + mac + 密钥)，全部 32 位小写
    const QString imageMd5 = QString::fromLatin1(
        QCryptographicHash::hash(imageBytes, QCryptographicHash::Md5).toHex());
    const QString raw = appId + imageMd5 + salt + FIXED_CUID + FIXED_MAC + secretKey;
    const QString sign = QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex());

    QUrl url(API_URL);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from"), QStringLiteral("auto"));
    query.addQueryItem(QStringLiteral("to"), to);
    query.addQueryItem(QStringLiteral("appid"), appId);
    query.addQueryItem(QStringLiteral("salt"), salt);
    query.addQueryItem(QStringLiteral("sign"), sign);
    query.addQueryItem(QStringLiteral("cuid"), FIXED_CUID);
    query.addQueryItem(QStringLiteral("mac"), FIXED_MAC);
    query.addQueryItem(QStringLiteral("version"), FIXED_VERSION);
    url.setQuery(query);

    // 图片必须走 multipart/form-data 上传，字段名固定为 image
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"image\"; filename=\"screenshot.jpg\""));
    imagePart.setBody(imageBytes);
    multiPart->append(imagePart);

    QNetworkRequest request(url);
    request.setTransferTimeout(REQUEST_TIMEOUT);

    QNetworkReply *reply = network->post(request, multiPart);

    // 请求体跟着回复对象一起释放，省得单独记一份生命周期
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, extractSourceOnly]() {
        handleReply(reply, extractSourceOnly);
    });
}

void BaiduTranslateClient::handleReply(QNetworkReply *reply, bool extractSourceOnly)
{
    // 先安排删除：后面所有分支都不再需要这个对象，信号也已经发完了
    reply->deleteLater();

    const QByteArray payload = reply->readAll();

    // 接口出错时也会返回 HTTP 200，真正的结果要看 error_code，
    // 所以网络层错误只处理连不上、超时这类情况
    if (reply->error() != QNetworkReply::NoError && payload.isEmpty()) {
        emit failed(reply->errorString());
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        emit failed(tr("百度接口返回的内容不是 JSON，请检查网络或代理设置"));
        return;
    }

    const QJsonObject root = document.object();
    const QString errorCode = root.value(QStringLiteral("error_code")).toString();
    if (errorCode != QStringLiteral("0")) {
        emit failed(describeError(errorCode,
                                  root.value(QStringLiteral("error_msg")).toString()));
        return;
    }

    const QJsonObject data = root.value(QStringLiteral("data")).toObject();

    // 只取原文时用 sumSrc（识别结果），否则用 sumDst（译文）。
    // 万一接口没给整段内容，就按分段把对应字段拼起来
    const QString summaryKey = extractSourceOnly ? QStringLiteral("sumSrc")
                                                 : QStringLiteral("sumDst");
    const QString segmentKey = extractSourceOnly ? QStringLiteral("src")
                                                 : QStringLiteral("dst");

    QString text = data.value(summaryKey).toString().trimmed();
    if (text.isEmpty()) {
        QStringList lines;
        const QJsonArray segments = data.value(QStringLiteral("content")).toArray();
        for (const QJsonValue &segment : segments) {
            const QString line = segment.toObject().value(segmentKey).toString();
            if (!line.isEmpty()) {
                lines.append(line);
            }
        }
        text = lines.join(QLatin1Char('\n')).trimmed();
    }

    if (text.isEmpty()) {
        if (extractSourceOnly) {
            emit failed(tr("图片里没有识别到文字"));
            return;
        }
        emit failed(tr("接口没有返回译文，图片里可能没有可翻译的文字"));
        return;
    }

    emit finished(text);
}
