#include "capture.h"

#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QPainter>
#include <QDateTime>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#ifdef Q_OS_LINUX
#  include <QDBusConnection>
#  include <QDBusMessage>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#endif

Capture::Capture(QObject* parent) : QObject(parent) {}

#ifdef Q_OS_WIN

// HBITMAP -> QImage（32 位、自上而下）
static QImage bitmapToImage(HBITMAP bmp, HDC dc, int w, int h) {
    if (!bmp || w <= 0 || h <= 0)
        return QImage();
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 负数：自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    QImage img(w, h, QImage::Format_RGB32);
    if (!img.isNull())
        GetDIBits(dc, bmp, 0, UINT(h), img.bits(), &bi, DIB_RGB_COLORS);
    return img;
}

// 抓单个显示器（GDI BitBlt，保留物理分辨率）
static QImage grabMonitor(const QString& device, int w, int h) {
    HDC src = CreateDCW(L"DISPLAY", reinterpret_cast<LPCWSTR>(device.utf16()), nullptr, nullptr);
    if (!src)
        return QImage();
    HDC mem = CreateCompatibleDC(src);
    HBITMAP bmp = CreateCompatibleBitmap(src, w, h);
    if (!mem || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        DeleteDC(src);
        return QImage();
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    // CAPTUREBLT 让分层窗口（部分工具栏/悬浮窗）也能进入截图
    const BOOL ok = BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY | CAPTUREBLT);
    QImage img;
    if (ok)
        img = bitmapToImage(bmp, mem, w, h);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    DeleteDC(src);
    return img;
}

struct MonitorEnumCtx {
    QList<ScreenShot>* shots;
    bool any = false;
};

static int displayIndexFromDevice(const QString& dev) {
    const int i = dev.lastIndexOf(QStringLiteral("DISPLAY"), Qt::CaseInsensitive);
    if (i < 0)
        return -1;
    bool ok = false;
    const int n = dev.mid(i + 7).toInt(&ok);
    return ok ? n : -1;
}

static int displayIndexFromName(const QString& name) {
    const int close = name.lastIndexOf(')');
    const int open = name.lastIndexOf('(', close);
    if (open < 0 || close < 0)
        return -1;
    bool ok = false;
    const int n = name.mid(open + 1, close - open - 1).toInt(&ok);
    return ok ? n : -1;
}

static BOOL CALLBACK enumMonitorProc(HMONITOR hMon, HDC, LPRECT, LPARAM lp) {
    auto* ctx = reinterpret_cast<MonitorEnumCtx*>(lp);
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi))
        return TRUE;

    const QString device = QString::fromWCharArray(mi.szDevice);
    const int w = mi.rcMonitor.right - mi.rcMonitor.left;
    const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (w <= 0 || h <= 0)
        return TRUE;

    const QRect physRect(mi.rcMonitor.left, mi.rcMonitor.top, w, h);
    const int devIndex = displayIndexFromDevice(device);
    const auto screens = QGuiApplication::screens();
    QScreen* screen = nullptr;
    const char* how = "none";

    // 1) Qt 屏幕名末尾的 (n) 与 \\.\DISPLAYn 对应，最可靠
    if (devIndex > 0) {
        for (QScreen* s : screens) {
            if (displayIndexFromName(s->name()) == devIndex) {
                screen = s;
                how = "name-index";
                break;
            }
        }
    }
    // 2) 100% 缩放下物理矩形与逻辑矩形完全一致
    if (!screen) {
        for (QScreen* s : screens) {
            if (s->geometry() == physRect) {
                screen = s;
                how = "geometry";
                break;
            }
        }
    }
    // 3) 主显示器标志
    if (!screen && (mi.dwFlags & MONITORINFOF_PRIMARY)) {
        screen = QGuiApplication::primaryScreen();
        how = "primary";
    }
    // 4) 兜底：未被占用的屏幕
    if (!screen) {
        for (QScreen* s : screens) {
            bool used = false;
            for (const ScreenShot& shot : *ctx->shots)
                used = used || shot.screen == s;
            if (!used) {
                screen = s;
                how = "fallback";
                break;
            }
        }
    }
    if (!screen)
        return TRUE;
    for (const ScreenShot& existing : *ctx->shots) {
        if (existing.screen == screen)
            return TRUE;   // 该屏已由 Qt 抓到
    }

    QImage phys = grabMonitor(device, w, h);
    if (phys.isNull()) {
        // 兜底：Qt 自己的抓屏
        const QPixmap pm = screen->grabWindow(0);
        if (!pm.isNull()) {
            phys = pm.toImage();
            if (screen->devicePixelRatio() > 1.0)
                phys.setDevicePixelRatio(1.0);
        }
    }
    if (phys.isNull())
        return TRUE;

    ScreenShot shot;
    shot.screen = screen;
    shot.physical = phys;
    shot.logicalRect = screen->geometry();
    shot.scale = shot.logicalRect.width() > 0
                     ? double(phys.width()) / double(shot.logicalRect.width())
                     : 1.0;
    ctx->shots->append(shot);
    qInfo() << "monitor" << device << "-> screen" << screen->name() << "by" << how
            << "logical" << shot.logicalRect << "physical" << phys.size() << "scale" << shot.scale;
    ctx->any = true;
    return TRUE;
}

bool Capture::captureWindows() {
    m_shots.clear();

    // 以 Qt 自己的屏幕枚举为准逐屏抓图：
    // QScreen::grabWindow(0) 返回的就是该 QScreen 的像素，
    // 与逻辑坐标天然一致，不会出现 Win32 设备名和 Qt 屏幕对不上的左右颠倒。
    for (QScreen* s : QGuiApplication::screens()) {
        const QPixmap pm = s->grabWindow(0);
        if (pm.isNull()) {
            qWarning() << "Qt 抓屏失败:" << s->name();
            continue;
        }
        ScreenShot shot;
        shot.screen = s;
        shot.physical = pm.toImage();
        shot.physical.setDevicePixelRatio(1.0);
        shot.logicalRect = s->geometry();
        shot.scale = shot.logicalRect.width() > 0
                         ? double(shot.physical.width()) / double(shot.logicalRect.width())
                         : 1.0;
        m_shots.append(shot);
        qInfo() << "screen" << s->name() << "logical" << shot.logicalRect
                << "physical" << shot.physical.size() << "scale" << shot.scale;
    }

    // 个别屏幕 Qt 抓不到时，用 Win32 BitBlt 按设备名兜底补齐
    if (m_shots.size() < QGuiApplication::screens().size()) {
        MonitorEnumCtx ctx{&m_shots};
        EnumDisplayMonitors(nullptr, nullptr, enumMonitorProc, reinterpret_cast<LPARAM>(&ctx));
    }
    if (m_shots.isEmpty())
        return false;

    QRect desk;
    for (const ScreenShot& s : m_shots)
        desk = desk.united(s.logicalRect);
    if (desk.isEmpty())
        return false;

    // 逻辑分辨率合成图：叠加层按 Qt 逻辑坐标工作，直接与鼠标事件对齐。
    QImage full(desk.size(), QImage::Format_ARGB32_Premultiplied);
    full.fill(Qt::black);
    QPainter p(&full);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const ScreenShot& s : m_shots) {
        const QRect r = s.logicalRect.translated(-desk.topLeft());
        const QImage scaled = s.physical.scaled(r.size(), Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation);
        p.drawImage(r.topLeft(), scaled);
    }
    p.end();
    full.setDevicePixelRatio(1.0);

    qInfo() << "Windows 截图: 屏幕数" << m_shots.size() << "桌面" << desk << "合成" << full.size();
    emit captured(full);
    return true;
}

void Capture::captureScreen() {
    if (!captureWindows())
        emit failed(QStringLiteral("抓屏失败（BitBlt 与 grabWindow 均不可用）"));
}

#else  // ---------------- Linux ----------------

bool Capture::tryExtensionShot() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.snipshot.Shot", "/org/snipshot/Shot", "org.snipshot.Shot", "Take");
    msg << false;
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 4000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return false;
    const QString path = reply.arguments().value(0).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    QImage img(path);
    if (img.isNull())
        return false;
    qInfo() << "扩展截图:" << img.size() << path;
    emit captured(img);
    return true;
}

void Capture::captureScreen() {
    if (tryExtensionShot())
        return;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(QStringLiteral("D-Bus session 不可用"));
        return;
    }

    const QString token = "snipshot_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QString senderPart = bus.baseService();
    if (senderPart.startsWith(':'))
        senderPart.remove(0, 1);
    senderPart.replace('.', '_');
    m_requestPath = "/org/freedesktop/portal/desktop/request/" + senderPart + "/" + token;

    bus.connect("org.freedesktop.portal.Desktop",
                m_requestPath,
                "org.freedesktop.portal.Request",
                "Response",
                this, SLOT(onPortalResponse(uint, QVariantMap)));

    QVariantMap opts;
    opts.insert("handle_token", token);
    opts.insert("interactive", false);
    opts.insert("modal", true);
    opts.insert("permission_store_checked", true);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot",
        "Screenshot");
    msg << QString() << opts;

    QDBusPendingCall call = bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qWarning() << "portal Screenshot 调用失败:" << reply.error().message();
            captureX11();
            w->deleteLater();
            return;
        }
        const QString handle = reply.value().path();
        qInfo() << "portal 返回 handle:" << handle;
        if (handle != m_requestPath) {
            QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_requestPath,
                "org.freedesktop.portal.Request", "Response", this, SLOT(onPortalResponse(uint, QVariantMap)));
            m_requestPath = handle;
            QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", m_requestPath,
                "org.freedesktop.portal.Request", "Response", this, SLOT(onPortalResponse(uint, QVariantMap)));
        }
        w->deleteLater();
    });
}

void Capture::onPortalResponse(uint code, const QVariantMap& results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop",
                                             m_requestPath,
                                             "org.freedesktop.portal.Request",
                                             "Response",
                                             this, SLOT(onPortalResponse(uint, QVariantMap)));
    if (code != 0) {
        emit failed(QString("截图被拒绝或取消 (code=%1)").arg(code));
        return;
    }
    const QString uri = results.value("uri").toString();
    if (uri.isEmpty()) {
        emit failed(QStringLiteral("portal 未返回截图"));
        return;
    }
    QImage img(QUrl(uri).toLocalFile());
    if (img.isNull()) {
        emit failed("无法读取截图文件: " + uri);
        return;
    }
    qInfo() << "portal 截图尺寸:" << img.size() << "文件:" << uri;
    emit captured(img);
}

void Capture::captureX11() {
    QScreen* scr = QGuiApplication::primaryScreen();
    if (!scr) {
        emit failed(QStringLiteral("找不到屏幕"));
        return;
    }
    QPixmap pm = scr->grabWindow(0);
    if (pm.isNull()) {
        emit failed(QStringLiteral("grabWindow 失败"));
        return;
    }
    qInfo() << "X11 截图尺寸:" << pm.size();
    emit captured(pm.toImage());
}

#endif
