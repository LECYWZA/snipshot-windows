#pragma once

#include <QString>
#include <QImage>
#include <QJsonObject>

// OpenAI 兼容接口调用：识图（OCR 文字提取）与翻译共用一套 API
class Ai : public QObject {
    Q_OBJECT
public:
    explicit Ai(QObject* parent = nullptr);

    // mode: "ocr" 只提取文字；"translate" 提取并翻译（中文→英文，其他→中文）
    void recognize(const QImage& img, const QString& mode);

    // 逐行翻译（配合本地 OCR 的坐标，用于原位覆盖显示）。顺序与输入一致。
    // target 为空表示自动：中文→英文，其他→中文；否则翻译成 target（如“简体中文”“日文”）。
    void translateLines(const QStringList& lines, const QString& target = QString());

signals:
    void done(const QString& text);
    void linesDone(const QStringList& translations);
    void error(const QString& message);

private:
    QJsonObject baseRequest(const QImage& img, const QString& prompt) const;
};
