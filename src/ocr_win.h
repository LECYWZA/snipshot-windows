#pragma once

#include <QString>
#include <QRect>
#include <QList>
#include <QImage>
#include <functional>

// 一行 OCR 结果：像素坐标框 + 文字
struct OcrLine {
    QRect box;      // 原图像素坐标
    QString text;
};

// Windows 10/11 自带 OCR（Windows.Media.Ocr）
namespace WindowsOcr {
// 同步调用：必须在非 UI 线程（内部 init_apartment(MTA)）
QList<OcrLine> recognize(const QImage& img, QString* error = nullptr);

// 异步调用：内部起独立线程，结果回到 ctx 所在线程
void recognizeAsync(const QImage& img, QObject* ctx,
                    const std::function<void(QList<OcrLine>, QString)>& done);

QString engineLanguage();   // 例如 "zh-Hans-CN"
}
