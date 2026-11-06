#include "ai.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QBuffer>
#include <QUrl>
#include "settings.h"

Ai::Ai(QObject* parent) : QObject(parent) {}

QJsonObject Ai::baseRequest(const QImage& img, const QString& prompt) const {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    const QString dataUri = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());

    QJsonObject textPart{{"type", "text"}, {"text", prompt}};
    QJsonObject imagePart{{"type", "image_url"}, {"image_url", QJsonObject{{"url", dataUri}}}};
    QJsonArray content{textPart, imagePart};
    QJsonObject msg{{"role", "user"}, {"content", content}};
    QJsonArray messages{msg};
    return QJsonObject{
        {"model", Settings::load().model},
        {"messages", messages},
        {"temperature", 0.1},
        {"max_tokens", 2000}
    };
}

void Ai::translateLines(const QStringList& lines, const QString& target) {
    if (lines.isEmpty()) {
        emit linesDone({});
        return;
    }
    Settings st = Settings::load();
    QJsonArray input;
    for (const QString& line : lines)
        input.append(line);
    const QString head = target.isEmpty()
        ? QStringLiteral("你是截图翻译引擎。输入是一个 JSON 字符串数组，每个元素是画面中一行文字（按从上到下顺序）。\n"
                         "请逐条翻译：中文翻译成英文，其他语言翻译成简体中文；数字、百分比、代码、专有名词保持原样。\n")
        : QStringLiteral("你是截图翻译引擎。输入是一个 JSON 字符串数组，每个元素是画面中一行文字（按从上到下顺序）。\n"
                         "请逐行翻译成%1，自动识别源语言；如果某行本来就是%1就原样返回；"
                         "数字、百分比、代码、专有名词保持原样。\n").arg(target);
    const QString prompt = head +
        QStringLiteral("只输出一个 JSON 字符串数组，长度和顺序必须与输入完全一致，不要 markdown 代码块，不要任何解释。\n\n输入：")
        + QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));

    QJsonArray messages{QJsonObject{{"role", "user"}, {"content", prompt}}};
    QJsonObject body{
        {"model", st.model},
        {"messages", messages},
        {"temperature", 0.1},
        {"max_tokens", 4000}
    };

    auto* nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(st.apiBase + "/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + st.apiKey.toUtf8());
    QNetworkReply* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, lines]() {
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("翻译接口失败: ") + reply->errorString());
        } else {
            const QJsonObject obj = QJsonDocument::fromJson(raw).object();
            const QJsonArray choices = obj.value("choices").toArray();
            QString text = choices.isEmpty()
                               ? QString()
                               : choices.first().toObject().value("message").toObject().value("content").toString();
            text = text.trimmed();
            // 去掉模型偶尔加的 markdown 代码块
            if (text.startsWith("```")) {
                const int nl = text.indexOf('\n');
                if (nl > 0)
                    text = text.mid(nl + 1);
                if (text.endsWith("```"))
                    text.chop(3);
                text = text.trimmed();
            }
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
                emit error(QStringLiteral("翻译结果解析失败: ") + text.left(200));
            } else {
                QStringList out;
                for (const QJsonValue& v : doc.array())
                    out << v.toString();
                while (out.size() < lines.size())
                    out << lines.at(out.size());
                out = out.mid(0, lines.size());
                qInfo() << "translateLines 完成:" << out.size() << "条";
                emit linesDone(out);
            }
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

void Ai::recognize(const QImage& img, const QString& mode) {
    Settings st = Settings::load();
    QString prompt;
    if (mode == "ocr") {
        prompt = QStringLiteral(
            "请提取图片中的全部文字，保持原来的段落与顺序。直接输出文字内容，不要任何解释、不要 markdown 代码块。");
    } else {
        prompt = QStringLiteral(
            "请识别图片中的文字并翻译：原文是中文则翻译成英文，原文是其他语言则翻译成简体中文。"
            "只输出译文，不要输出原文，不要任何解释、不要 markdown 代码块。");
    }

    auto* nam = new QNetworkAccessManager(this);
    QUrl url(st.apiBase + "/chat/completions");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Bearer " + st.apiKey.toUtf8());
    const QJsonObject body = baseRequest(img, prompt);
    QNetworkReply* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, mode]() {
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit error(QString("API 请求失败: ") + reply->errorString());
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            const QJsonObject obj = doc.object();
            const QJsonArray choices = obj.value("choices").toArray();
            if (choices.isEmpty()) {
                emit error("API 返回为空: " + QString::fromUtf8(raw.left(300)));
            } else {
                const QJsonObject m = choices.first().toObject().value("message").toObject();
                QString text = m.value("content").toString();
                if (text.trimmed().isEmpty())
                    text = m.value("reasoning_content").toString();
                emit done(text.trimmed());
            }
        }
        reply->deleteLater();
        nam->deleteLater();
    });
    Q_UNUSED(mode);
}
