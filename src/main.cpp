#include <QApplication>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QLocalServer>
#include <QLocalSocket>
#include <QClipboard>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QMutex>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// 无控制台（WIN32 子系统）时把日志落到 %LOCALAPPDATA%\SnipShot\snipshot.log，
// 排障时直接看这个文件。
static void snipLog(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const char* level = "INFO";
    if (type == QtWarningMsg) level = "WARN";
    else if (type == QtCriticalMsg) level = "ERR ";
    else if (type == QtFatalMsg) level = "FATAL";
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile f(dir + "/snipshot.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write(QDateTime::currentDateTime().toString("MM-dd HH:mm:ss.zzz ").toUtf8());
        f.write(level);
        f.write(" ");
        f.write(msg.toUtf8());
        f.write("\n");
    }
#ifdef Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<LPCWSTR>((QString(msg) + "\n").utf16()));
#endif
}

#include "settings.h"
#include "capture.h"
#include "session.h"
#include "view.h"
#include "pin.h"
#include "ai.h"
#include "settingsdialog.h"
#include "hotkeys.h"
#include "screenutil.h"
#include "clipboard.h"
#include "recorder.h"

class App : public QObject {
    Q_OBJECT
public:
    explicit App(QObject* parent = nullptr) : QObject(parent) {}

    void init(const QStringList& args);

public slots:
    void handleCommand(const QString& cmd);
    void snip();
    void translate();
    void ocr();
    void pinLast();
    void togglePins();
    void settings();
    void showClipboard();
    void toggleRecording();
    void shutdown();

private:
    void startOverlay(CaptureSession::Mode mode);
    void startSession(const QImage& img, const QList<ScreenShot>& shots, CaptureSession::Mode mode);
    void closeSession();
    void setActiveView(OverlayView* view);
    void handleAction(int action, const QImage& img, const QRect& rect);
    void showTextPin(const QImage& img, const QRect& rect, const QString& mode);
    void showPinWithOcr(const QImage& img, const QRect& rect, bool translateMode);
    void saveImage(const QImage& img);
    void pinFullDesktop(const QImage& img);
    void preview(const QString& path);
    void debugShot(const QString& path);
    void debugGrab(const QString& path);
    void refreshMenuLabels();

    QSystemTrayIcon* m_tray = nullptr;
    ClipboardManager* m_clipboard = nullptr;
    ScreenRecorder* m_recorder = nullptr;
    QAction* m_recordAction = nullptr;
    QAction* m_actSnip = nullptr;
    QAction* m_actPin = nullptr;
    QAction* m_actToggle = nullptr;
    QAction* m_actTranslate = nullptr;
    QAction* m_actOcr = nullptr;
    QAction* m_actClipboard = nullptr;
    CaptureSession* m_session = nullptr;
    QList<QPointer<OverlayView>> m_views;
    QPointer<OverlayView> m_activeView;
    QImage m_lastF1;
    QRect m_lastF1Rect;
    bool m_capturing = false;
    int m_captureGen = 0;
};

void App::init(const QStringList& args) {
    // 托盘
    m_tray = new QSystemTrayIcon(this);
    QIcon icon = QIcon::fromTheme("camera-photo");
    if (icon.isNull()) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setPen(QPen(QColor(0, 160, 255), 3));
        p.drawRect(4, 4, 24, 24);
        p.end();
        icon = QIcon(pm);
    }
    m_tray->setIcon(icon);
    m_tray->setToolTip(QStringLiteral("SnipShot（截图 / 贴图 / 翻译 / 文字提取）"));

    m_clipboard = new ClipboardManager(this);
    connect(m_clipboard, &ClipboardManager::error, this, [this](const QString& e) {
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot 剪贴板"), e,
                                QSystemTrayIcon::Warning, 4000);
    });
    if (Settings::load().clipboardEnabled)
        m_clipboard->start();

    m_recorder = new ScreenRecorder(this);
    connect(m_recorder, &ScreenRecorder::started, this, [this](const QString& file) {
        if (m_recordAction)
            m_recordAction->setText(QStringLiteral("停止录屏 (%1)").arg(Settings::load().hkRecord));
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot 录屏"),
                                QStringLiteral("开始录屏：") + file,
                                QSystemTrayIcon::Information, 2500);
    });
    connect(m_recorder, &ScreenRecorder::finished, this, [this](const QString& file) {
        if (m_recordAction)
            m_recordAction->setText(QStringLiteral("开始录屏 (%1)").arg(Settings::load().hkRecord));
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot 录屏"),
                                QStringLiteral("录屏已保存：") + file,
                                QSystemTrayIcon::Information, 4000);
    });
    connect(m_recorder, &ScreenRecorder::error, this, [this](const QString& e) {
        if (m_recordAction)
            m_recordAction->setText(QStringLiteral("开始录屏 (%1)").arg(Settings::load().hkRecord));
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot 录屏"), e,
                                QSystemTrayIcon::Warning, 4000);
    });

    auto* menu = new QMenu;
    m_actSnip = menu->addAction(QStringLiteral("截图"), this, &App::snip);
    m_actPin = menu->addAction(QStringLiteral("固定贴图"), this, &App::pinLast);
    m_actToggle = menu->addAction(QStringLiteral("隐藏/显示贴图"), this, &App::togglePins);
    menu->addSeparator();
    m_actTranslate = menu->addAction(QStringLiteral("截屏翻译"), this, &App::translate);
    m_actOcr = menu->addAction(QStringLiteral("文字提取"), this, &App::ocr);
    menu->addSeparator();
    m_actClipboard = menu->addAction(QStringLiteral("剪贴板历史"), this, &App::showClipboard);
    m_recordAction = menu->addAction(QStringLiteral("开始录屏"), this, &App::toggleRecording);
    menu->addSeparator();
    menu->addAction(QStringLiteral("设置…"), this, &App::settings);
    menu->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
    connect(menu, &QMenu::aboutToShow, this, &App::refreshMenuLabels);
    refreshMenuLabels();
    m_tray->setContextMenu(menu);
    m_tray->show();

#ifdef Q_OS_WIN
    QStringList hkProblems;
    Hotkeys::installWindows(Settings::load(),
                            [this](const QString& cmd) { handleCommand(cmd); },
                            &hkProblems);
    if (!hkProblems.isEmpty() && m_tray) {
        m_tray->showMessage(QStringLiteral("SnipShot 快捷键"),
                            QStringLiteral("以下快捷键未能注册：\n") + hkProblems.join('\n'),
                            QSystemTrayIcon::Warning, 8000);
    }
#endif

    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == "--preview") {
            if (i + 1 < args.size())
                preview(args[++i]);
            continue;
        }
        handleCommand(a);
    }

    qInfo() << "SnipShot 已常驻. 平台:" << QGuiApplication::platformName();
}

void App::refreshMenuLabels() {
    const Settings st = Settings::load();
    auto label = [](const QString& name, const QString& key, bool enabled) {
        return (enabled && !key.trimmed().isEmpty())
                   ? QStringLiteral("%1 (%2)").arg(name, key)
                   : name;
    };
    if (m_actSnip)
        m_actSnip->setText(label(QStringLiteral("截图"), st.hkSnip, st.hkSnipEnabled));
    if (m_actPin)
        m_actPin->setText(label(QStringLiteral("固定贴图"), st.hkPin, st.hkPinEnabled));
    if (m_actToggle)
        m_actToggle->setText(label(QStringLiteral("隐藏/显示贴图"), st.hkTogglePins, st.hkTogglePinsEnabled));
    if (m_actTranslate)
        m_actTranslate->setText(label(QStringLiteral("截屏翻译"), st.hkTranslate, st.hkTranslateEnabled));
    if (m_actOcr)
        m_actOcr->setText(label(QStringLiteral("文字提取"), st.hkOcr, st.hkOcrEnabled));
    if (m_actClipboard)
        m_actClipboard->setText(label(QStringLiteral("剪贴板历史"), st.hkClipboard, st.hkClipboardEnabled));
    if (m_recordAction) {
        const QString base = (m_recorder && m_recorder->isRecording()) ? QStringLiteral("停止录屏")
                                                                      : QStringLiteral("开始录屏");
        m_recordAction->setText(label(base, st.hkRecord, st.hkRecordEnabled));
    }
}

void App::handleCommand(const QString& cmd) {
    if (cmd == "--snip")
        snip();
    else if (cmd == "--translate")
        translate();
    else if (cmd == "--ocr")
        ocr();
    else if (cmd == "--pin")
        pinLast();
    else if (cmd == "--toggle-pins")
        togglePins();
    else if (cmd == "--settings")
        settings();
    else if (cmd == "--quit")
        QCoreApplication::quit();
    else if (cmd == "--cancel")
        closeSession();
    else if (cmd.startsWith("--preview="))
        preview(cmd.mid(10));
    else if (cmd.startsWith("--debug-shot="))
        debugShot(cmd.mid(13));
    else if (cmd.startsWith("--debug-grab="))
        debugGrab(cmd.mid(13));
    else if (cmd.startsWith("--debug-screens=")) {
        const QString dir = cmd.mid(16);
        QDir().mkpath(dir);
        for (QScreen* s : QGuiApplication::screens()) {
            const QPixmap pm = s->grabWindow(0);
            QString safe = s->name();
            for (QChar& c : safe) {
                if (!c.isLetterOrNumber())
                    c = '_';
            }
            const QString path = dir + "/qt_" + safe + ".png";
            pm.save(path);
            qInfo() << "debug-screens" << s->name() << s->geometry() << "grab" << pm.size() << path;
        }
    }
    else if (cmd.startsWith("--debug-windows")) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            qInfo() << "toplevel" << w->metaObject()->className() << "visible" << w->isVisible()
                    << "enabled" << w->isEnabled()
                    << "modal" << (w->isWindow() && w->windowModality() != Qt::NonModal)
                    << "geometry" << w->geometry() << "title" << w->windowTitle();
        }
    }
    else if (cmd.startsWith("--debug-border=")) {
        const bool on = cmd.mid(15).trimmed() != QStringLiteral("0");
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* p = qobject_cast<TranslatePinWindow*>(w)) {
                p->setBorderVisible(on);
                qInfo() << "debug-border" << on << p->geometry();
            }
        }
    }
    else if (cmd.startsWith("--debug-raise")) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* p = qobject_cast<TranslatePinWindow*>(w)) {
                p->raise();
                p->activateWindow();
#ifdef Q_OS_WIN
                SetWindowPos(reinterpret_cast<HWND>(p->winId()), HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
                p->update();
                qInfo() << "debug-raise" << p->geometry();
            }
        }
    }
    else if (cmd.startsWith("--debug-hit=")) {
#ifdef Q_OS_WIN
        const QStringList xy = cmd.mid(12).split(',');
        if (xy.size() == 2) {
            POINT pt{xy[0].toInt(), xy[1].toInt()};
            HWND h = WindowFromPoint(pt);
            wchar_t cls[256]{};
            GetClassNameW(h, cls, 256);
            qInfo() << "debug-hit" << pt.x << pt.y << "hwnd" << (quintptr)h
                    << "class" << QString::fromWCharArray(cls);
        }
#endif
    }
    else if (cmd.startsWith("--debug-children")) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* p = qobject_cast<TranslatePinWindow*>(w)) {
                qInfo() << "pin" << p->geometry() << "visible" << p->isVisible();
                for (QObject* c : p->children()) {
                    auto* cw = qobject_cast<QWidget*>(c);
                    if (cw)
                        qInfo() << "  child" << cw->metaObject()->className() << cw->geometry()
                                << "visible" << cw->isVisible();
                }
            }
        }
    }
    else if (cmd.startsWith("--debug-lang=")) {
        const QString lang = cmd.mid(13);
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* p = qobject_cast<TranslatePinWindow*>(w)) {
                p->setTargetLanguage(lang);
                qInfo() << "debug-lang" << lang << p->geometry();
            }
        }
    }
    else if (cmd.startsWith("--debug-panel=")) {
        const bool on = cmd.mid(14).trimmed() != QStringLiteral("0");
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* p = qobject_cast<TranslatePinWindow*>(w)) {
                p->showTextPanel(on);
                qInfo() << "debug-panel" << on;
            }
        }
    }
    else if (cmd.startsWith("--debug-ocr=")) {
        const QString p = cmd.mid(12);
        const QImage img(p);
        if (img.isNull()) {
            qWarning() << "debug-ocr 读图失败:" << p;
        } else {
            WindowsOcr::recognizeAsync(img, this, [](QList<OcrLine> lines, QString err) {
                qInfo() << "debug-ocr 行数:" << lines.size() << "错误:" << err;
                for (const OcrLine& l : lines)
                    qInfo() << "debug-ocr box:" << l.box << "text:" << l.text;
            });
        }
    }
    else if (cmd == "--clipboard")
        showClipboard();
    else if (cmd == "--register-hotkeys") {
#ifdef Q_OS_WIN
        QStringList problems;
        Hotkeys::reinstallWindows(Settings::load(), &problems);
#else
        Hotkeys::installGnome(Settings::load());
#endif
    }
    else if (cmd == "--record-toggle")
        toggleRecording();
    else if (cmd.startsWith("--clip-file=")) {
        const QString v = cmd.mid(12);
        const int p = v.indexOf(':');
        if (p > 0 && m_clipboard)
            m_clipboard->addFile(v.left(p), v.mid(p + 1));
    }
}

void App::closeSession() {
    qInfo() << "closeSession";
    ++m_captureGen;
    m_capturing = false;
    const auto views = m_views;
    m_views.clear();
    for (const auto& v : views) {
        if (v) {
            v->close();
            v->deleteLater();
        }
    }
    m_activeView = nullptr;
    if (m_session) {
        m_session->deleteLater();
        m_session = nullptr;
    }
}

void App::startSession(const QImage& img, const QList<ScreenShot>& shots, CaptureSession::Mode mode) {
    qInfo() << "startSession 屏幕数:" << QGuiApplication::screens().size();
    closeSession();
    m_session = new CaptureSession(img, shots, mode, this);

    connect(m_session, &CaptureSession::aiRequested, this,
            [this](const QImage& im, const QRect& r, int mode) {
                showTextPin(im, r, mode == 0 ? QStringLiteral("translate") : QStringLiteral("ocr"));
                closeSession();
            });
    connect(m_session, &CaptureSession::actionRequested, this,
            [this](int action, const QImage& im, const QRect& r) {
                handleAction(action, im, r);
            });
    // 选区跨屏移动时，让活动视图自动跟到选区所在屏幕，
    // 工具栏和手柄就不会留在另一块屏的边缘。
    connect(m_session, &CaptureSession::changed, this, [this]() {
        if (!m_session || !m_session->hasSelection() || m_session->selectionResizing())
            return;
        const QPoint anchor = m_session->selection().center();
        QScreen* sc = QGuiApplication::screenAt(anchor);
        if (!sc)
            return;
        for (const auto& v : m_views) {
            if (v && v->screen() == sc && m_activeView != v) {
                setActiveView(v);
                break;
            }
        }
    });
    connect(m_session, &CaptureSession::cancelRequested, this, &App::closeSession);

    QScreen* under = QGuiApplication::screenAt(QCursor::pos());
    OverlayView* active = nullptr;
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        auto* v = new OverlayView(m_session, s);
        connect(v, &OverlayView::activated, this, &App::setActiveView);
        connect(v, &OverlayView::cancelRequested, this, &App::closeSession);
        m_views.append(v);
        if (s == under)
            active = v;
    }
    if (!active && !m_views.isEmpty())
        active = m_views.first();
    setActiveView(active);
}

void App::setActiveView(OverlayView* view) {
    if (m_activeView == view)
        return;
    if (m_session && m_session->editingText())
        m_session->commitEditingText();
    for (const auto& v : m_views) {
        if (v)
            v->setActive(v == view);
    }
    m_activeView = view;
}

void App::startOverlay(CaptureSession::Mode mode) {
    // 新截图永远从实时屏幕重新抓，旧会话直接关闭；不弹“上一张没处理完”
    closeSession();
    m_capturing = true;
    const int gen = ++m_captureGen;
    QTimer::singleShot(350, this, [this, gen, mode]() {
        auto* cap = new Capture(this);
        connect(cap, &Capture::captured, this, [this, cap, gen, mode](const QImage& img) {
            if (gen != m_captureGen) {
                cap->deleteLater();
                return;
            }
            m_capturing = false;
            startSession(img, cap->shots(), mode);
            cap->deleteLater();
        });
        connect(cap, &Capture::failed, this, [this, cap, gen](const QString& e) {
            if (gen != m_captureGen) {
                cap->deleteLater();
                return;
            }
            m_capturing = false;
            QMessageBox::warning(nullptr, "SnipShot", QStringLiteral("截图失败：") + e);
            cap->deleteLater();
        });
        cap->captureScreen();
    });
}

void App::showPinWithOcr(const QImage& img, const QRect& rect, bool translateMode) {
#ifdef Q_OS_WIN
    // 截图定格 → 本地 OCR 拿行坐标 → 翻译模式逐行翻译并原位覆盖；
    // 文字提取模式只把行文本填进可编辑的文本栏。
    auto* pin = new TranslatePinWindow(img, rect, translateMode);
    PinManager::instance()->registerPin(pin);

    QPointer<TranslatePinWindow> guard(pin);
    WindowsOcr::recognizeAsync(img, this, [guard](QList<OcrLine> lines, QString err) {
        if (!guard)
            return;
        if (lines.isEmpty()) {
            guard->setMessage(err.isEmpty() ? QStringLiteral("未识别到文字")
                                            : QStringLiteral("识别失败：") + err.left(60));
            return;
        }
        guard->setSourceLines(lines);
    });
#else
    Q_UNUSED(translateMode);
    auto* pin = new TextPinWindow(img, rect, translateMode ? QStringLiteral("translate")
                                                            : QStringLiteral("ocr"));
    PinManager::instance()->registerPin(pin);
#endif
}

void App::showTextPin(const QImage& img, const QRect& rect, const QString& mode) {
    // 翻译/取字结果贴图：立即固定贴出，AI 在贴图内部异步完成；
    // 不占用截图会话，等待期间可以继续 F1/F2。
    if (mode == QStringLiteral("translate")) {
        showPinWithOcr(img, rect, true);
        return;
    }
    showPinWithOcr(img, rect, false);
}

void App::handleAction(int action, const QImage& img, const QRect& rect) {
    switch (action) {
    case 0:
        saveImage(img);
        if (m_session && m_session->mode() == CaptureSession::Snip) {
            m_lastF1 = img;
            m_lastF1Rect = rect;
        }
        closeSession();
        break;
    case 1:
        QApplication::clipboard()->setImage(img);
        if (m_session && m_session->mode() == CaptureSession::Snip) {
            m_lastF1 = img;
            m_lastF1Rect = rect;
        }
        closeSession();
        break;
    case 2:
        PinManager::instance()->addPin(img, rect.topLeft());
        if (m_session && m_session->mode() == CaptureSession::Snip) {
            m_lastF1 = img;
            m_lastF1Rect = rect;
        }
        closeSession();
        break;
    case 3:
        showTextPin(img, rect, QStringLiteral("translate"));
        closeSession();
        break;
    case 4:
        showTextPin(img, rect, QStringLiteral("ocr"));
        closeSession();
        break;
    default:
        break;
    }
}

void App::snip() { startOverlay(CaptureSession::Snip); }
void App::translate() { startOverlay(CaptureSession::Translate); }
void App::ocr() { startOverlay(CaptureSession::Ocr); }

void App::pinFullDesktop(const QImage& img0) {
    QImage img = img0;
    const QRect desk = virtualDesktopRect();
    img.setDevicePixelRatio(double(img.width()) / qMax(1, desk.width()));
    PinManager::instance()->addPin(img);
}

void App::pinLast() {
    if (m_session) {
        if (m_session->hasSelection()) {
            const QImage img = m_session->currentResult();
            const QRect r = m_session->selection();
            if (!img.isNull()) {
                PinManager::instance()->addPin(img, r.topLeft());
                if (m_session->mode() == CaptureSession::Snip) {
                    m_lastF1 = img;
                    m_lastF1Rect = r;
                }
            }
            closeSession();
            return;
        }
        if (!m_session->fullImage().isNull()) {
            pinFullDesktop(m_session->fullImage());
            closeSession();
            return;
        }
    }
    if (!m_lastF1.isNull()) {
        PinManager::instance()->addPin(m_lastF1, m_lastF1Rect.topLeft());
        return;
    }
    if (m_capturing)
        return;
    m_capturing = true;
    const int gen = ++m_captureGen;
    auto* cap = new Capture(this);
    connect(cap, &Capture::captured, this, [this, cap, gen](const QImage& img) {
        if (gen != m_captureGen) {
            cap->deleteLater();
            return;
        }
        m_capturing = false;
        pinFullDesktop(img);
        cap->deleteLater();
    });
    connect(cap, &Capture::failed, this, [this, cap, gen](const QString& e) {
        if (gen != m_captureGen) {
            cap->deleteLater();
            return;
        }
        m_capturing = false;
        QMessageBox::warning(nullptr, "SnipShot", QStringLiteral("截图失败：") + e);
        cap->deleteLater();
    });
    cap->captureScreen();
}

void App::togglePins() {
    PinManager::instance()->toggleAll();
}

void App::settings() {
    // 非模态：设置开着时仍然可以截图/用剪贴板，避免“整个软件像卡死”
    auto* dlg = new SettingsDialog(nullptr);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    QScreen* s = QGuiApplication::screenAt(QCursor::pos());
    if (!s)
        s = QGuiApplication::primaryScreen();
    if (s) {
        const QRect g = s->availableGeometry();
        const QSize sz = dlg->sizeHint().expandedTo(QSize(620, 560));
        dlg->move(g.center() - QPoint(sz.width() / 2, sz.height() / 2));
    }
    connect(dlg, &QDialog::accepted, this, [this]() {
        if (m_clipboard)
            m_clipboard->reloadSettings();
#ifdef Q_OS_WIN
        QStringList problems;
        Hotkeys::reinstallWindows(Settings::load(), &problems);
        if (!problems.isEmpty() && m_tray) {
            m_tray->showMessage(QStringLiteral("SnipShot 快捷键"),
                                QStringLiteral("以下快捷键未能注册：\n") + problems.join('\n'),
                                QSystemTrayIcon::Warning, 8000);
        }
#endif
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void App::showClipboard() {
    if (m_clipboard)
        m_clipboard->openWindow();
}

void App::toggleRecording() {
    if (!m_recorder)
        return;
    if (!Settings::load().recordingEnabled) {
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot 录屏"),
                                QStringLiteral("录屏功能已在设置中关闭"),
                                QSystemTrayIcon::Information, 2500);
        return;
    }
    if (m_recorder->isRecording())
        m_recorder->stop();
    else
        m_recorder->start();
}

void App::shutdown() {
    if (m_recorder && m_recorder->isRecording())
        m_recorder->stop();
    if (m_clipboard)
        m_clipboard->stop();
}

void App::saveImage(const QImage& img) {
    Settings st = Settings::load();
    QDir().mkpath(st.saveDir);
    const QString fn = st.saveDir + "/Snip_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    if (img.save(fn)) {
        qInfo() << "已保存:" << fn;
        if (m_tray)
            m_tray->showMessage(QStringLiteral("SnipShot"),
                                QStringLiteral("已保存：") + fn,
                                QSystemTrayIcon::Information, 2000);
    } else {
        QMessageBox::warning(nullptr, "SnipShot", QStringLiteral("保存失败：") + fn);
    }
}

void App::debugGrab(const QString& path) {
    if (!m_activeView) {
        qWarning() << "debug-grab: 没有活动视图";
        return;
    }
    const QPixmap pm = m_activeView->grab();
    if (pm.save(path))
        qInfo() << "debug-grab 已保存:" << path << pm.size()
                << "visible:" << m_activeView->isVisible()
                << "selection:" << (m_session && m_session->hasSelection());
    else
        qWarning() << "debug-grab 保存失败:" << path;
}

void App::debugShot(const QString& path) {
    auto* cap = new Capture(this);
    connect(cap, &Capture::captured, this, [cap, path](const QImage& img) {
        if (img.save(path))
            qInfo() << "debug-shot 已保存:" << path << img.size();
        else
            qWarning() << "debug-shot 保存失败:" << path;
        cap->deleteLater();
    });
    connect(cap, &Capture::failed, this, [cap](const QString& e) {
        qWarning() << "debug-shot 失败:" << e;
        cap->deleteLater();
    });
    cap->captureScreen();
}

void App::preview(const QString& path) {
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(nullptr, "SnipShot", QStringLiteral("无法读取预览图片：") + path);
        return;
    }
    m_capturing = false;
    startSession(img, {}, CaptureSession::Snip);
}

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    qInstallMessageHandler(snipLog);
    // 必须在 QApplication 之前：保证抓屏拿到真实物理像素，坐标不被系统虚拟化。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    if (qEnvironmentVariableIsSet("SNIPSHOT_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", qgetenv("SNIPSHOT_PLATFORM"));
    else if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("LECYWZA"));
    app.setApplicationName(QStringLiteral("SnipShot"));
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = QCoreApplication::arguments().mid(1);

    const QString serverName = "snipshot";
    QLocalSocket probe;
    probe.connectToServer(serverName);
    if (probe.waitForConnected(300)) {
        QByteArray payload;
        for (int i = 0; i < args.size(); ++i) {
            if (args[i] == "--preview" && i + 1 < args.size())
                payload += ("--preview=" + args[++i]).toUtf8() + '\n';
            else
                payload += args[i].toUtf8() + '\n';
        }
        probe.write(payload);
        probe.waitForBytesWritten(500);
        probe.disconnectFromServer();
        return 0;
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName))
        qWarning() << "无法监听本地 socket，程序可能已在运行";

    App* a = new App;
    a->init(args);

    QObject::connect(&server, &QLocalServer::newConnection, a, [&server, a]() {
        while (QLocalSocket* s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, a, [a, s]() {
                const QByteArray data = s->readAll();
                for (const QByteArray& line : data.split('\n')) {
                    const QString cmd = QString::fromUtf8(line).trimmed();
                    if (!cmd.isEmpty())
                        a->handleCommand(cmd);
                }
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        }
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, a, &App::shutdown);

    return app.exec();
}

#include "main.moc"
