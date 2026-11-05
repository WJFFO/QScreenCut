#include "AiClient.h"

#include "AppSettings.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtDebug>

namespace {

// 发给模型的截图先压到这个尺寸以内。
// 2K 分辨率的 PNG 转成 base64 有十几兆，多数接口会直接拒掉，
// 而识别文字本来也用不上那个分辨率
constexpr int MAX_IMAGE_SIZE = 1600;
constexpr int JPEG_QUALITY = 85;

// 单次请求最多等多久（毫秒）。识图和翻译通常几秒就有结果，
// 网络卡住时不要让窗口一直停在“正在识别”
constexpr int REQUEST_TIMEOUT = 90000;

// 跟模型说的话。
// 提取文字时特意强调“不要翻译、不要总结”：不加这句，模型经常顺手把内容整理一遍，
// 用户拿到的就不是原文了
QString buildPrompt(AiClient::Task task, const QString &targetLanguage)
{
    if (task == AiClient::Task::Translate) {
        return QStringLiteral(
                   "请把图片中的所有文字翻译成%1。"
                   "如果原文本来就是%1，则翻译成英文。"
                   "只输出译文，不要输出原文，也不要添加任何解释或括号说明。")
            .arg(targetLanguage);
    }

    return QStringLiteral(
        "请提取图片中的所有文字，按图片里的阅读顺序输出，保留原有的换行和分段。"
        "不要翻译，不要总结，不要补充任何说明。"
        "如果图片里没有文字，只输出“没有识别到文字”。");
}

// 把截图转成接口能接受的 data URL。
// 先按最长边缩到 MAX_IMAGE_SIZE 以内再压成 JPEG，体积通常只有原来的几十分之一
QString imageToDataUrl(const QImage &image)
{
    QImage scaled = image;
    if (scaled.width() > MAX_IMAGE_SIZE || scaled.height() > MAX_IMAGE_SIZE) {
        scaled = scaled.scaled(MAX_IMAGE_SIZE, MAX_IMAGE_SIZE,
                               Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    scaled.save(&buffer, "JPEG", JPEG_QUALITY);
    buffer.close();

    // 编码失败时提前说清楚原因：JPEG 编码依赖发布目录里的 imageformats/qjpeg.dll，
    // 少了它只能得到一段空数据，接口那边只会回一句莫名其妙的错误
    if (bytes.isEmpty()) {
        qWarning() << "截图编码成 JPEG 失败，检查发布目录是否缺少 imageformats/qjpeg.dll";
    }

    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(bytes.toBase64());
}

// 接口报错时会把原因放在 JSON 的 error.message 里，
// 比如“invalid api key”“model does not exist”，比 Qt 的英文网络错误有用得多
QString extractErrorMessage(const QByteArray &payload)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return QString();
    }

    const QJsonObject error = document.object().value(QStringLiteral("error")).toObject();
    return error.value(QStringLiteral("message")).toString().trimmed();
}

}

AiClient::AiClient(QObject *parent)
    : QObject(parent)
    , network(new QNetworkAccessManager(this))
{
}

void AiClient::send(const QImage &image, Task task)
{
    if (image.isNull()) {
        emit failed(tr("截图内容为空，没法识别"));
        return;
    }

    const AppSettings &settings = AppSettings::instance();

    const QString apiKey = settings.aiApiKey().trimmed();
    if (apiKey.isEmpty()) {
        emit failed(tr("还没有填写 API Key，请打开设置界面填写后再试"));
        return;
    }

    // 地址允许两种写法：填到 /v1 这层，或者直接填完整的 /v1/chat/completions。
    // 结尾多写的斜杠先去掉，避免拼出双斜杠
    QString endpoint = settings.aiBaseUrl().trimmed();
    while (endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.chop(1);
    }
    if (endpoint.isEmpty()) {
        emit failed(tr("还没有填写 API 地址，请打开设置界面填写后再试"));
        return;
    }
    if (!endpoint.endsWith(QStringLiteral("/chat/completions"))) {
        endpoint += QStringLiteral("/chat/completions");
    }

    const QString model = settings.aiModel().trimmed();
    if (model.isEmpty()) {
        emit failed(tr("还没有填写模型名称，请打开设置界面填写后再试"));
        return;
    }

    // 消息体按 OpenAI 视觉接口的格式拼：一条 user 消息，内容里同时放提示词和图片
    QJsonObject promptPart;
    promptPart[QStringLiteral("type")] = QStringLiteral("text");
    promptPart[QStringLiteral("text")] = buildPrompt(task, settings.translateTarget());

    QJsonObject imageUrl;
    imageUrl[QStringLiteral("url")] = imageToDataUrl(image);

    QJsonObject imagePart;
    imagePart[QStringLiteral("type")] = QStringLiteral("image_url");
    imagePart[QStringLiteral("image_url")] = imageUrl;

    QJsonArray content;
    content.append(promptPart);
    content.append(imagePart);

    QJsonObject message;
    message[QStringLiteral("role")] = QStringLiteral("user");
    message[QStringLiteral("content")] = content;

    QJsonArray messages;
    messages.append(message);

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setTransferTimeout(REQUEST_TIMEOUT);

    QNetworkReply *reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void AiClient::handleReply(QNetworkReply *reply)
{
    // 先安排删除：后面所有分支都不再需要这个对象，信号也已经发完了
    reply->deleteLater();

    const QByteArray payload = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        const QString detail = extractErrorMessage(payload);
        if (!detail.isEmpty()) {
            emit failed(detail);
            return;
        }
        emit failed(reply->errorString());
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        emit failed(tr("接口返回的内容不是 JSON，可能是地址填错了"));
        return;
    }

    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        // 少数服务在出错时也返回 200，把原因放在 error 字段里
        const QString detail = extractErrorMessage(payload);
        emit failed(detail.isEmpty() ? tr("接口没有返回结果") : detail);
        return;
    }

    const QJsonObject replyMessage =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString text = replyMessage.value(QStringLiteral("content")).toString().trimmed();

    if (text.isEmpty()) {
        emit failed(tr("接口返回了空内容，换个模型或换张图再试试"));
        return;
    }

    emit finished(text);
}
