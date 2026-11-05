#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// 调百度图片翻译接口（https://fanyi-api.baidu.com/api/trans/sdk/picture）。
//
// 整张截图直接交给接口，接口自己识别文字、自己翻译，不需要先做 OCR。
// 返回值里 sumDst 是译文、sumSrc 是识别出的原文——所以它既能当翻译用，
// 也能在大模型没配置时当"识别文字"用。
// 签名规则：sign = md5(appid + md5(image) + salt + cuid + mac + 密钥)，32 位小写
class BaiduTranslateClient : public QObject
{
    Q_OBJECT

public:
    explicit BaiduTranslateClient(QObject *parent = nullptr);

    // 接口支持的目标语种（下拉框用这份名字，和内部的语种代码一一对应）
    static QStringList targetLanguageNames();

    // 发起请求。extractSourceOnly 为 true 时只取识别出的原文，
    // 用来在没有大模型的情况下做文字识别；否则返回译文
    void send(const QImage &image, bool extractSourceOnly = false);

signals:
    void finished(const QString &text);
    void failed(const QString &message);

private:
    void handleReply(QNetworkReply *reply, bool extractSourceOnly);

    QNetworkAccessManager *network = nullptr;
};
