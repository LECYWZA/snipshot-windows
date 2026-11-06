#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QList>
#include <QString>

class QScreen;

// 单屏原始截图：物理像素图 + 它在 Qt 逻辑桌面里的位置。
// 叠加层绘制与高分辨率输出都依赖这份映射，不能只保留合成后的逻辑图。
struct ScreenShot {
    QScreen* screen = nullptr;
    QRect logicalRect;      // Qt 逻辑坐标（与鼠标事件一致）
    QImage physical;        // 物理像素（原始分辨率）
    qreal scale = 1.0;      // 物理 / 逻辑
};

// 抓屏：
// - Windows：逐显示器 GDI BitBlt（CAPTUREBLT），保持物理分辨率；
// - Linux：xdg-desktop-portal / GNOME 扩展。
class Capture : public QObject {
    Q_OBJECT
public:
    explicit Capture(QObject* parent = nullptr);

    // 异步抓取整个虚拟桌面，成功发 captured(QImage)
    void captureScreen();
    const QList<ScreenShot>& shots() const { return m_shots; }

signals:
    void captured(const QImage& img);
    void failed(const QString& error);

#ifdef Q_OS_LINUX
private slots:
    void onPortalResponse(uint code, const QVariantMap& results);
#endif

private:
#ifdef Q_OS_WIN
    bool captureWindows();
#else
    bool tryExtensionShot();
    void captureX11();
#endif
    QList<ScreenShot> m_shots;
    QString m_requestPath;
};
