#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// 调用 OpenAI 兼容的 /chat/completions 接口。
//
// 本程序只用到两件事：把截图里的文字读出来，或者读出来直接翻译。
// 两件事都交给视觉模型一次性完成，不需要先 OCR 再单独调翻译接口——
// 少一次请求，也少一份把两段结果拼错的麻烦。
class AiClient : public QObject
{
    Q_OBJECT

public:
    enum class Task
    {
        RecognizeText,   // 只提取文字
        Translate,       // 提取文字并翻译成设置里的目标语言
    };

    explicit AiClient(QObject *parent = nullptr);

    // 发起请求。结果通过 finished / failed 信号返回，界面不会被卡住
    void send(const QImage &image, Task task);

signals:
    void finished(const QString &text);
    void failed(const QString &message);

private:
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager *network = nullptr;
};
