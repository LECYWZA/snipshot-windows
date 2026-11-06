#include "ocr_win.h"

#ifdef Q_OS_WIN

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

#  include <winrt/base.h>
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Foundation.Collections.h>
#  include <winrt/Windows.Graphics.Imaging.h>
#  include <winrt/Windows.Globalization.h>
#  include <winrt/Windows.Media.Ocr.h>
#  include <winrt/Windows.Storage.Streams.h>

#  include <QBuffer>
#  include <QDebug>
#  include <QMetaObject>
#  include <thread>

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using OcrEngine = winrt::Windows::Media::Ocr::OcrEngine;

namespace {

// 把 QImage 编码成 PNG 再交给 WinRT 解码，避免直接操作 IMemoryBufferByteAccess
SoftwareBitmap toSoftwareBitmap(const QImage& src) {
    QByteArray png;
    QBuffer qbuf(&png);
    qbuf.open(QIODevice::WriteOnly);
    src.save(&qbuf, "PNG");
    qbuf.close();

    Windows::Storage::Streams::InMemoryRandomAccessStream stream;
    Windows::Storage::Streams::DataWriter writer(stream);
    const auto* begin = reinterpret_cast<const uint8_t*>(png.constData());
    writer.WriteBytes(array_view<const uint8_t>(begin, begin + png.size()));
    writer.StoreAsync().get();
    writer.FlushAsync().get();
    stream.Seek(0);
    const auto decoder = BitmapDecoder::CreateAsync(stream).get();
    return decoder.GetSoftwareBitmapAsync().get();
}

OcrEngine createEngine() {
    OcrEngine engine = nullptr;
    try {
        engine = OcrEngine::TryCreateFromLanguage(
            Windows::Globalization::Language(L"zh-Hans-CN"));
    } catch (...) {
    }
    if (!engine)
        engine = OcrEngine::TryCreateFromUserProfileLanguages();
    return engine;
}

static bool isCjk(QChar c) {
    const ushort u = c.unicode();
    return (u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF) ||
           (u >= 0x3000 && u <= 0x303F) || (u >= 0xFF00 && u <= 0xFFEF);
}

// Windows OCR 会给中文逐字插空格（"用 电 预 测"），去掉中文字符之间的空格
static QString cleanOcrText(const QString& input) {
    QString out;
    out.reserve(input.size());
    for (int i = 0; i < input.size(); ++i) {
        const QChar c = input.at(i);
        if (c == u' ' && i > 0 && i + 1 < input.size() &&
            isCjk(input.at(i - 1)) && isCjk(input.at(i + 1)))
            continue;
        out.append(c);
    }
    return out.trimmed();
}

static void ensureApartment() {
    try {
        init_apartment(apartment_type::multi_threaded);
    } catch (const hresult_error& e) {
        if (e.code() != RPC_E_CHANGED_MODE)
            throw;
        // 线程已是 STA：继续用现有 apartment
    }
}

}  // namespace

namespace WindowsOcr {

QString engineLanguage() {
    try {
        ensureApartment();
        OcrEngine engine = createEngine();
        if (engine)
            return QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
    } catch (...) {
    }
    return QString();
}

QList<OcrLine> recognize(const QImage& img, QString* error) {
    QList<OcrLine> lines;
    try {
        ensureApartment();
        OcrEngine engine = createEngine();
        if (!engine) {
            if (error)
                *error = QStringLiteral("系统没有可用的 OCR 语言包（设置→时间和语言→语言→中文添加“光学字符识别”）");
            return lines;
        }
        const SoftwareBitmap bmp = toSoftwareBitmap(img);
        const auto result = engine.RecognizeAsync(bmp).get();
        for (const auto& line : result.Lines()) {
            QRect box;
            for (const auto& word : line.Words()) {
                const auto r = word.BoundingRect();
                const QRect wr(qRound(r.X), qRound(r.Y), qRound(r.Width), qRound(r.Height));
                box = box.isNull() ? wr : box.united(wr);
            }
            const QString text = cleanOcrText(QString::fromWCharArray(line.Text().c_str()));
            if (box.isValid() && !text.isEmpty())
                lines.append({box, text});
        }
        qInfo() << "Windows OCR:" << lines.size() << "行, 引擎"
                << QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
    } catch (const hresult_error& e) {
        const QString msg = QString::fromWCharArray(e.message().c_str());
        qWarning() << "Windows OCR 失败:" << msg;
        if (error)
            *error = msg;
    } catch (...) {
        if (error)
            *error = QStringLiteral("Windows OCR 未知错误");
    }
    return lines;
}

void recognizeAsync(const QImage& img, QObject* ctx,
                    const std::function<void(QList<OcrLine>, QString)>& done) {
    std::thread th([img, ctx, done]() {
        QString err;
        const QList<OcrLine> lines = recognize(img, &err);
        QMetaObject::invokeMethod(ctx, [done, lines, err]() { done(lines, err); },
                                  Qt::QueuedConnection);
    });
    th.detach();
}

}  // namespace WindowsOcr

#endif
