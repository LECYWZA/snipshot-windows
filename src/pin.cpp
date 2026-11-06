#include <QWindow>
#include "pin.h"
#include "ai.h"
#include "overlay.h"
#include "settings.h"
#include "screenutil.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QTextEdit>
#include <QDialog>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QUrl>
#include <QtMath>
#include <QFontMetricsF>
#include <QTimer>
#include <QComboBox>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

class DragLabel : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            return;
        }
        QLabel::mousePressEvent(e);
    }
};

static QString saveDefault(const QImage& img) {
    Settings st = Settings::load();
    QDir().mkpath(st.saveDir);
    const QString fn = st.saveDir + "/Snip_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";
    return img.save(fn) ? fn : QString();
}

// 贴图/编辑结果：置顶文字窗，定位到原区域
static void showAiResult(const QString& title, const QImage& img, const QString& mode, const QRect& at) {
    auto* ai = new Ai;
    auto* win = new QWidget;
    win->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->setWindowTitle(title);
    win->setStyleSheet("QWidget { background:#1e1e1e; }");

    auto* edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setPlaceholderText(QStringLiteral("识别中…"));
    edit->setStyleSheet("QTextEdit { background:#1e1e1e; color:#f0f0f0; border:1px solid #00a0ff; }");

    auto* titleLabel = new DragLabel(title);
    titleLabel->setStyleSheet("QLabel { color:#9fd8ff; font-weight:bold; padding:2px; }");
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    auto* bar = new QHBoxLayout;
    bar->addWidget(titleLabel);
    bar->addStretch();
    bar->addWidget(copyBtn);
    bar->addWidget(closeBtn);

    auto* lay = new QVBoxLayout(win);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addWidget(edit);
    lay->addLayout(bar);

    QObject::connect(copyBtn, &QPushButton::clicked, win, [edit]() {
        QApplication::clipboard()->setText(edit->toPlainText());
    });
    QObject::connect(closeBtn, &QPushButton::clicked, win, &QWidget::close);

    QRect g = at;
    const QRect desk = virtualDesktopRect();
    if (g.isValid()) {
        if (g.width() < 320) {
            g.setWidth(320);
            g.moveLeft(qMax(desk.left(), at.center().x() - 160));
        }
        if (g.height() < 180) {
            g.setHeight(180);
            g.moveTop(qMax(desk.top(), at.center().y() - 90));
        }
        if (g.right() > desk.right())
            g.moveRight(desk.right());
        if (g.bottom() > desk.bottom())
            g.moveBottom(desk.bottom());
        if (g.left() < desk.left())
            g.moveLeft(desk.left());
        if (g.top() < desk.top())
            g.moveTop(desk.top());
        win->setGeometry(g);
    } else {
        win->resize(520, 360);
    }
    win->show();
    win->raise();

    QObject::connect(ai, &Ai::done, win, [edit, win, mode](const QString& text) {
        edit->setPlainText(text);
        if (mode == "ocr")
            QApplication::clipboard()->setText(text);
        win->raise();
    });
    QObject::connect(ai, &Ai::error, win, [win](const QString& e) {
        QMessageBox::warning(win, "SnipShot", QStringLiteral("AI 调用失败：") + e);
        win->close();
    });
    QObject::connect(ai, &Ai::done, ai, &QObject::deleteLater);
    QObject::connect(ai, &Ai::error, ai, &QObject::deleteLater);
    ai->recognize(img, mode);
}

PinWindow::PinWindow(const QImage& img, const QPoint& pos, QWidget* parent)
    : QWidget(parent), m_img(img) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    const QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    m_baseLogical = logical;
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::ArrowCursor);
    setFixedSize(logical);
    m_border = Settings::load().pinBorder;
    move(pos);
    show();
}

void PinWindow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.drawImage(rect(), m_img);
    if (m_border) {
        painter.setPen(QPen(QColor(0, 160, 255), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PinWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (windowHandle())
            windowHandle()->startSystemMove();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* e) {
    Q_UNUSED(e);
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent*) {
    close();
}

void PinWindow::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y();
    if (d == 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_zoom = qBound(0.2, m_zoom * (d > 0 ? 1.1 : 0.9), 8.0);
    setFixedSize(qMax(20, qRound(m_baseLogical.width() * m_zoom)),
                 qMax(20, qRound(m_baseLogical.height() * m_zoom)));
    update();
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

PinManager* PinManager::instance() {
    static PinManager* s = new PinManager;
    return s;
}

void PinManager::addPin(const QImage& img) {
    if (img.isNull())
        return;
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    const QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    const QRect desk = virtualDesktopRect();
    const QPoint pos(desk.center().x() - logical.width() / 2,
                     desk.center().y() - logical.height() / 2);
    addPin(img, pos);
}

void PinManager::addPin(const QImage& img, const QPoint& pos) {
    if (img.isNull())
        return;
    auto* w = new PinWindow(img, pos);
    m_pins.append(w);
    connect(w, &QObject::destroyed, this, [this, w]() { m_pins.removeAll(w); });
}

void PinManager::toggleAll() {
    if (m_pins.isEmpty())
        return;
    bool anyVisible = false;
    for (QWidget* w : m_pins)
        if (w->isVisible())
            anyVisible = true;
    for (QWidget* w : m_pins) {
        if (anyVisible)
            w->hide();
        else
            w->show();
    }
}

void PinManager::closeAll() {
    for (QWidget* w : m_pins)
        w->close();
}

// ---------- 贴图窗口右键菜单 ----------

void PinWindow::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu;
    QAction* borderAct = menu.addAction(QStringLiteral("显示边框"));
    borderAct->setCheckable(true);
    borderAct->setChecked(m_border);
    connect(borderAct, &QAction::toggled, this, [this](bool on) {
        m_border = on;
        update();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("复制"), this, [this]() {
        QApplication::clipboard()->setImage(m_img);
    });
    menu.addAction(QStringLiteral("另存为…"), this, [this]() {
        Settings st = Settings::load();
        QDir().mkpath(st.saveDir);
        const QString fn = QFileDialog::getSaveFileName(
            this, QStringLiteral("另存为"),
            st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
            QStringLiteral("PNG (*.png)"));
        if (!fn.isEmpty() && m_img.save(fn))
            m_savedPath = fn;
    });
    menu.addAction(QStringLiteral("打开图片所在路径"), this, [this]() {
        if (m_savedPath.isEmpty())
            m_savedPath = saveDefault(m_img);
        if (!m_savedPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_savedPath).absolutePath()));
    });
    menu.addAction(QStringLiteral("绘制…"), this, [this]() {
        auto* ov = new Overlay(m_img, QRect(pos(), size()), true);
        QObject::connect(ov, &Overlay::saveRequested, this, [](const QImage& im, const QRect&) {
            saveDefault(im);
        });
        QObject::connect(ov, &Overlay::copyRequested, this, [](const QImage& im, const QRect&) {
            QApplication::clipboard()->setImage(im);
        });
        QObject::connect(ov, &Overlay::pinRequested, this, [](const QImage& im, const QRect& r) {
            PinManager::instance()->addPin(im, r.topLeft());
        });
        QObject::connect(ov, &Overlay::translateRequested, this, [ov](const QImage& im, const QRect& r) {
            showAiResult(QStringLiteral("翻译"), im, "translate", r);
            ov->close();
        });
        QObject::connect(ov, &Overlay::ocrRequested, this, [ov](const QImage& im, const QRect& r) {
            showAiResult(QStringLiteral("文字提取"), im, "ocr", r);
            ov->close();
        });
    });
    menu.addAction(QStringLiteral("文字提取"), this, [this]() {
        showAiResult(QStringLiteral("文字提取"), m_img, "ocr", QRect(pos(), size()));
    });
    menu.addAction(QStringLiteral("翻译"), this, [this]() {
        showAiResult(QStringLiteral("翻译"), m_img, "translate", QRect(pos(), size()));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("关闭"), this, &QWidget::close);
    menu.exec(e->globalPos());
}

// ---------------- AI 结果贴图：像贴图一样显示选区原图 + 原位文字 ----------------

TextPinWindow::TextPinWindow(const QImage& img, const QRect& logicalRect, const QString& mode,
                             QWidget* parent)
    : QWidget(parent), m_img(img), m_mode(mode) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    QSize logical(qRound(img.width() / d), qRound(img.height() / d));
    // 以选区实际大小为准，不再撑成 380×220 的大黑框
    logical.setWidth(qMax(140, logical.width()));
    logical.setHeight(qMax(56, logical.height()));
    m_baseLogical = logical;
    m_baseFont = qBound(11, logical.height() / 8, 22);
    m_border = Settings::load().pinBorder;

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                      : QStringLiteral("文字提取"));
    setFixedSize(logical);
    if (logicalRect.isValid())
        move(logicalRect.topLeft());

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 细窄工具条：只做拖动和复制/关闭，背景半透明，下面是原图
    auto* header = new QWidget;
    header->setFixedHeight(18);
    header->setStyleSheet("QWidget { background: rgba(0,0,0,70); }");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 0, 2, 0);
    hl->setSpacing(2);
    m_title = new DragLabel(mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                               : QStringLiteral("文字提取"));
    m_title->setStyleSheet("QLabel { color:#dff4ff; font-size:10px; padding-left:2px; }");
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    copyBtn->setFlat(true);
    copyBtn->setFixedSize(34, 16);
    copyBtn->setStyleSheet("QPushButton { background: rgba(255,255,255,26); border:none; "
                           "color:#fff; font-size:10px; } "
                           "QPushButton:hover { background: rgba(255,255,255,70); }");
    auto* closeBtn = new QPushButton(QStringLiteral("×"));
    closeBtn->setFlat(true);
    closeBtn->setFixedSize(16, 16);
    closeBtn->setStyleSheet("QPushButton { background: rgba(255,255,255,26); border:none; "
                            "color:#fff; font-size:12px; } "
                            "QPushButton:hover { background: rgba(255,80,80,160); }");
    hl->addWidget(m_title);
    hl->addStretch();
    hl->addWidget(copyBtn);
    hl->addWidget(closeBtn);
    header->installEventFilter(this);
    lay->addWidget(header);

    m_edit = new QTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setPlaceholderText(mode == QStringLiteral("translate")
                                   ? QStringLiteral("翻译中…")
                                   : QStringLiteral("识别中…"));
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_edit->setStyleSheet(
        "QTextEdit { background: rgba(0,0,0,95); color:#f4f4f4; border:none; padding:4px; "
        "selection-background-color:#0078d7; }");
    m_edit->setFont(QFont(QStringLiteral("Sans"), m_baseFont));
    m_edit->setCursor(Qt::IBeamCursor);
    m_edit->installEventFilter(this);
    m_edit->viewport()->installEventFilter(this);
    lay->addWidget(m_edit, 1);

    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_edit->toPlainText());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    m_ai = new Ai(this);
    connect(m_ai, &Ai::done, this, [this](const QString& text) {
        setResultText(text);
        if (m_mode == QStringLiteral("ocr"))
            QApplication::clipboard()->setText(text);
    });
    connect(m_ai, &Ai::error, this, [this](const QString& e) {
        setResultText(QStringLiteral("识别失败：") + e);
    });
    show();
    activateWindow();
    m_ai->recognize(m_img, m_mode);
}

TextPinWindow::~TextPinWindow() = default;

void TextPinWindow::setResultText(const QString& text) {
    m_done = true;
    if (m_title)
        m_title->setText(m_mode == QStringLiteral("translate") ? QStringLiteral("翻译")
                                                               : QStringLiteral("文字提取"));
    m_edit->setReadOnly(false);
    m_edit->setPlainText(text);
    m_edit->moveCursor(QTextCursor::End);
    m_edit->setFocus();
    raise();
    activateWindow();
    update();
}

void TextPinWindow::applyZoom() {
    const QSize sz(qMax(60, qRound(m_baseLogical.width() * m_zoom)),
                   qMax(30, qRound(m_baseLogical.height() * m_zoom)));
    setFixedSize(sz);
    QFont f = m_edit->font();
    f.setPixelSize(qMax(8, qRound(m_baseFont * m_zoom)));
    m_edit->setFont(f);
    update();
}

void TextPinWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    // 原图铺满窗口，文字区域半透明叠在上面；不再额外压一层大黑底
    p.drawImage(rect(), m_img);
    if (m_border) {
        p.setPen(QPen(QColor(0, 160, 255), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void TextPinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TextPinWindow::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y();
    if (d == 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_zoom = qBound(0.3, m_zoom * (d > 0 ? 1.1 : 0.9), 6.0);
    applyZoom();
    e->accept();
}

bool TextPinWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == m_edit || (m_edit && o == m_edit->viewport())) {
        if (e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Escape) {
                close();
                return true;
            }
        } else if (e->type() == QEvent::Wheel) {
            auto* we = static_cast<QWheelEvent*>(e);
            if (we->modifiers() & Qt::ControlModifier) {
                const int d = we->angleDelta().y();
                if (d != 0) {
                    m_zoom = qBound(0.3, m_zoom * (d > 0 ? 1.1 : 0.9), 6.0);
                    applyZoom();
                    return true;
                }
            }
        }
    }
    if (e->type() == QEvent::MouseButtonPress && m_title) {
        auto* me = static_cast<QMouseEvent*>(e);
        QWidget* w = qobject_cast<QWidget*>(o);
        if (me->button() == Qt::LeftButton &&
            (w == m_title || w == m_title->parentWidget())) {
            if (windowHandle())
                windowHandle()->startSystemMove();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void TextPinWindow::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    menu.addAction(QStringLiteral("复制全部文字"), this, [this]() {
        QApplication::clipboard()->setText(m_edit->toPlainText());
    });
    menu.addAction(QStringLiteral("全选"), this, [this]() { m_edit->selectAll(); });
    menu.addAction(QStringLiteral("保存文字…"), this, [this]() {
        Settings st = Settings::load();
        QDir().mkpath(st.saveDir);
        const QString fn = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存文字"),
            st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".txt",
            QStringLiteral("文本 (*.txt)"));
        if (!fn.isEmpty()) {
            QFile f(fn);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(m_edit->toPlainText().toUtf8());
        }
    });
    menu.addSeparator();
    QAction* border = menu.addAction(QStringLiteral("显示边框"));
    border->setCheckable(true);
    border->setChecked(m_border);
    connect(border, &QAction::toggled, this, [this](bool on) { m_border = on; update(); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("关闭"), this, &QWidget::close);
    menu.exec(e->globalPos());
}

static QColor backgroundColor(const QImage& img, const QRect& box);

TranslatePinWindow::TranslatePinWindow(const QImage& img, const QRect& logicalRect, bool translateMode,
                                       QWidget* parent)
    : QWidget(parent), m_img(img), m_translateMode(translateMode) {
    double d = img.devicePixelRatio();
    if (d <= 0)
        d = 1.0;
    m_baseLogical = QSize(qMax(120, qRound(img.width() / d)),
                          qMax(48, qRound(img.height() / d)));
    m_border = Settings::load().pinBorder;
    m_message = QStringLiteral("识别中…");
    if (qEnvironmentVariableIsSet("SNIPSHOT_DEBUG_PIN")) {
        const QString base = QString::fromLocal8Bit(qgetenv("SNIPSHOT_DEBUG_PIN"));
        m_img.save(base);
        QTimer::singleShot(3000, this, [this, base]() { grab().save(base + ".grab.png"); });
    }

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(translateMode ? QStringLiteral("翻译") : QStringLiteral("文字提取"));

    // 图片画布：用子控件承载合成图，避免顶层窗口自绘内容在部分环境不上屏
    m_canvas = new QLabel(this);
    m_canvas->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvas->setStyleSheet("background:transparent;");
    m_canvas->hide();

    // 顶部工具条：在图片外围（属于同一个窗口）
    m_bar = new QWidget(this);
    m_bar->setAttribute(Qt::WA_StyledBackground, true);
    m_bar->setStyleSheet(
        "QWidget { background:#141416; border-bottom:1px solid #2a2a2e; }"
        "QPushButton { background:#2a2a2e; color:#fff; border:none; "
        "padding:3px 9px; border-radius:3px; font-size:11px; }"
        "QPushButton:hover { background:#0078d7; }"
        "QComboBox { background:#2a2a2e; color:#fff; border:none; "
        "padding:2px 4px; font-size:11px; min-width:84px; }"
        "QComboBox QAbstractItemView { background:#252528; color:#fff; "
        "selection-background-color:#0078d7; border:1px solid #444; }");
    auto* bl = new QHBoxLayout(m_bar);
    bl->setContentsMargins(6, 2, 6, 2);
    bl->setSpacing(5);
    m_toggle = new QPushButton(QStringLiteral("原文"));
    m_toggle->setToolTip(QStringLiteral("切换 原文 / 译文（快捷键 T）"));
    m_lang = new QComboBox;
    const QList<QPair<QString, QString>> langs{
        {QStringLiteral("简体中文"), QStringLiteral("简体中文")},
        {QStringLiteral("繁體中文"), QStringLiteral("繁体中文")},
        {QStringLiteral("English"), QStringLiteral("英文")},
        {QStringLiteral("日本語"), QStringLiteral("日文")},
        {QStringLiteral("한국어"), QStringLiteral("韩文")},
        {QStringLiteral("Deutsch"), QStringLiteral("德文")},
        {QStringLiteral("Français"), QStringLiteral("法文")},
        {QStringLiteral("Español"), QStringLiteral("西班牙文")},
        {QStringLiteral("Русский"), QStringLiteral("俄文")},
    };
    for (const auto& l : langs)
        m_lang->addItem(l.first, l.second);
    const int defaultLang = m_lang->findText(Settings::load().translateTarget);
    if (defaultLang >= 0)
        m_lang->setCurrentIndex(defaultLang);
    m_target = m_lang->currentData().toString();
    auto* copyBtn = new QPushButton(QStringLiteral("复制"));
    auto* panelBtn = new QPushButton(QStringLiteral("文本"));
    panelBtn->setCheckable(true);
    auto* closeBtn = new QPushButton(QStringLiteral("×"));
    closeBtn->setFixedWidth(24);
    bl->addWidget(m_toggle);
    bl->addWidget(m_lang);
    bl->addWidget(copyBtn);
    bl->addWidget(panelBtn);
    bl->addStretch();
    bl->addWidget(closeBtn);
    m_bar->show();

    // 右侧文本栏：和图片在同一个窗口内
    m_panel = new QWidget(this);
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panel->setStyleSheet("QWidget { background:#1e1e20; border-left:1px solid #2f2f34; }");
    auto* pl = new QVBoxLayout(m_panel);
    pl->setContentsMargins(7, 5, 7, 7);
    pl->setSpacing(4);
    auto* panelTitle = new QLabel(m_translateMode ? QStringLiteral("译文文本（可编辑 / 复制）")
                                                  : QStringLiteral("提取文字（可编辑 / 复制）"));
    panelTitle->setStyleSheet("QLabel { color:#9fd8ff; font-size:11px; }");
    m_textEdit = new QTextEdit;
    m_textEdit->setReadOnly(false);
    m_textEdit->setStyleSheet(
        "QTextEdit { background:#141416; color:#f0f0f0; border:none; font-size:13px; "
        "padding:4px; selection-background-color:#0078d7; }");
    pl->addWidget(panelTitle);
    pl->addWidget(m_textEdit, 1);
    m_panel->hide();

    connect(m_toggle, &QPushButton::clicked, this, [this]() { setShowOriginal(!m_showOriginal); });
    connect(m_lang, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (idx < 0)
            return;
        m_target = m_lang->itemData(idx).toString();
        m_showOriginal = false;
        if (m_toggle)
            m_toggle->setText(QStringLiteral("原文"));
        startTranslate(m_target);
    });
    connect(copyBtn, &QPushButton::clicked, this, [this]() { copyPrimaryText(); });
    connect(panelBtn, &QPushButton::toggled, this, [this](bool on) { setTextPanelVisible(on); });
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);

    if (!m_translateMode) {
        m_toggle->hide();
        m_lang->hide();
    }

    updateWindowLayout();

    // 让图片区域尽量与原选区重合：工具条在图片上方
    QRect g = logicalRect;
    if (g.isValid()) {
        const QRect desk = virtualDesktopRect();
        QPoint tl = g.topLeft() - QPoint(0, toolbarHeight());
        if (tl.y() < desk.top())
            tl.setY(g.top());
        if (tl.x() + width() > desk.right())
            tl.setX(qMax(desk.left(), desk.right() - width()));
        if (tl.y() + height() > desk.bottom())
            tl.setY(qMax(desk.top(), desk.bottom() - height()));
        if (tl.x() < desk.left())
            tl.setX(desk.left());
        move(tl);
    }

    show();
    raise();
    activateWindow();
    setFocus();
#ifdef Q_OS_WIN
    // 有些系统窗口会抢到上面，显式设置一次原生 TOPMOST
    SetWindowPos(reinterpret_cast<HWND>(winId()), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
    qInfo() << "TranslatePin 位置:" << pos() << "窗口:" << size()
            << "图片:" << imageSize() << "选区:" << logicalRect;
}

void TranslatePinWindow::setSourceLines(const QList<OcrLine>& lines) {
    m_lines = lines;
    m_message.clear();
    if (!m_translateMode) {
        m_texts.clear();
        refreshTextPanel();
        setTextPanelVisible(true);
        update();
        return;
    }
    startTranslate(m_target.isEmpty() ? QStringLiteral("简体中文") : m_target);
}

void TranslatePinWindow::startTranslate(const QString& target) {
    if (m_lines.isEmpty())
        return;
    m_message = QStringLiteral("翻译中…");
    update();
    QStringList texts;
    for (const OcrLine& l : m_lines)
        texts << l.text;
    auto* ai = new Ai(this);
    connect(ai, &Ai::linesDone, this, [this, ai](const QStringList& tr) {
        ai->deleteLater();
        applyTranslations(m_lines, tr);
    });
    connect(ai, &Ai::error, this, [this, ai](const QString& e) {
        ai->deleteLater();
        setMessage(QStringLiteral("翻译失败：") + e.left(60));
    });
    ai->translateLines(texts, target);
}

void TranslatePinWindow::setShowOriginal(bool on) {
    m_showOriginal = on;
    if (m_toggle)
        m_toggle->setText(on ? QStringLiteral("译文") : QStringLiteral("原文"));
    updateCanvas();
    update();
}

void TranslatePinWindow::setTargetLanguage(const QString& displayName) {
    if (!m_lang)
        return;
    for (int i = 0; i < m_lang->count(); ++i) {
        if (m_lang->itemText(i) == displayName) {
            m_lang->setCurrentIndex(i);   // 触发 currentIndexChanged → 重新翻译
            return;
        }
    }
}

void TranslatePinWindow::setToolbarVisible(bool on) {
    m_showToolbar = on;
    updateWindowLayout();
}

void TranslatePinWindow::setTextPanelVisible(bool on) {
    m_panelVisible = on;
    updateWindowLayout();
    refreshTextPanel();
}

void TranslatePinWindow::refreshTextPanel() {
    if (!m_textEdit)
        return;
    const QString text = m_translateMode ? allTranslations() : allSources();
    if (m_textEdit->toPlainText() != text)
        m_textEdit->setPlainText(text);
}

void TranslatePinWindow::copyPrimaryText() {
    const QString text = m_translateMode ? allTranslations() : allSources();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

QSize TranslatePinWindow::imageSize() const {
    return QSize(qMax(80, qRound(m_baseLogical.width() * m_zoom)),
                 qMax(30, qRound(m_baseLogical.height() * m_zoom)));
}

int TranslatePinWindow::toolbarHeight() const {
    return m_showToolbar ? kBarHeight : 0;
}

int TranslatePinWindow::panelWidth() const {
    return m_panelVisible ? kPanelWidth : 0;
}

void TranslatePinWindow::updateWindowLayout() {
    const QSize img = imageSize();
    const int bar = toolbarHeight();
    const int panel = panelWidth();
    const int pad = m_border ? 1 : 0;   // 边框画在图片外侧，不压住像素
    const int imgW = img.width() + pad * 2;
    const int imgH = img.height() + pad * 2;
    setFixedSize(imgW + panel, bar + imgH);
    if (m_bar) {
        m_bar->setGeometry(0, 0, imgW, bar);
        m_bar->setVisible(m_showToolbar);
    }
    if (m_panel) {
        m_panel->setGeometry(imgW, 0, panel, height());
        m_panel->setVisible(m_panelVisible);
    }
    if (m_canvas) {
        m_canvas->setGeometry(pad, bar + pad, img.width(), img.height());
        m_canvas->raise();
        m_bar->raise();
        m_panel->raise();
    }
    updateCanvas();
    update();
}

QImage TranslatePinWindow::composeImage() const {
    const QSize sz = imageSize();
    QImage out(sz, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::black);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRect(QPoint(0, 0), sz), m_img);

    const double d = m_img.devicePixelRatio() > 0 ? m_img.devicePixelRatio() : 1.0;
    const double sx = double(sz.width()) / qMax(1, m_baseLogical.width());

    if (!m_showOriginal) {
        for (int i = 0; i < m_lines.size() && i < m_texts.size(); ++i) {
            const QString text = m_texts.at(i).trimmed();
            if (text.isEmpty() || text == m_lines.at(i).text.trimmed())
                continue;
            const QRectF r(m_lines.at(i).box.x() / d * sx,
                           m_lines.at(i).box.y() / d * sx,
                           m_lines.at(i).box.width() / d * sx,
                           m_lines.at(i).box.height() / d * sx);
            if (r.width() < 2 || r.height() < 2)
                continue;

            const QColor cover = backgroundColor(m_img, m_lines.at(i).box);
            const QColor fg = cover.lightness() < 140 ? QColor(245, 245, 245)
                                                      : QColor(20, 20, 20);
            p.setPen(Qt::NoPen);
            p.setBrush(cover);
            p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 2, 2);

            QFont f = font();
            int px = qMax(8, int(r.height() * 0.68));
            f.setPixelSize(px);
            QFontMetricsF fm(f);
            while (px > 7 && fm.horizontalAdvance(text) > r.width() - 2) {
                --px;
                f.setPixelSize(px);
                fm = QFontMetricsF(f);
            }
            p.setFont(f);
            p.setPen(fg);
            QString shown = text;
            if (fm.horizontalAdvance(shown) > r.width() - 2)
                shown = fm.elidedText(shown, Qt::ElideRight, int(r.width() - 2));
            p.drawText(r.adjusted(1, 0, -1, 0), Qt::AlignLeft | Qt::AlignVCenter, shown);
        }
    }

    if (!m_message.isEmpty()) {
        const QRect badge(6, 6, 150, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 190));
        p.drawRoundedRect(badge, 10, 10);
        p.setPen(Qt::white);
        p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        p.drawText(badge, Qt::AlignCenter, m_message);
    }
    p.end();
    return out;
}

void TranslatePinWindow::updateCanvas() {
    if (!m_canvas)
        return;
    m_canvas->setPixmap(QPixmap::fromImage(composeImage()));
    m_canvas->show();
}

void TranslatePinWindow::layoutBar() {
    if (!m_bar)
        return;
    m_bar->raise();
}

void TranslatePinWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    layoutBar();
}

bool TranslatePinWindow::event(QEvent* e) {
    if (e->type() == QEvent::WindowActivate || e->type() == QEvent::Show)
        update();
    return QWidget::event(e);
}

void TranslatePinWindow::setMessage(const QString& text) {
    m_message = text;
    updateCanvas();
    update();
}

void TranslatePinWindow::applyTranslations(const QList<OcrLine>& lines,
                                            const QStringList& translations) {
    m_lines = lines;
    m_texts = translations;
    m_message.clear();
    qInfo() << "TranslatePin 应用译文: 行" << lines.size() << "译文" << translations.size()
            << (translations.isEmpty() ? QString() : translations.first().left(40));
    for (int i = 0; i < qMin(5, lines.size()); ++i)
        qInfo() << "  line" << i << lines.at(i).box << lines.at(i).text.left(30)
                << "=>" << (i < translations.size() ? translations.at(i).left(30) : QString());
    if (qEnvironmentVariableIsSet("SNIPSHOT_DEBUG_PIN")) {
        const QString base = QString::fromLocal8Bit(qgetenv("SNIPSHOT_DEBUG_PIN"));
        QTimer::singleShot(900, this, [this, base]() {
            grab().save(base + ".after.png");
            m_img.save(base + ".after_mimg.png");
            qInfo() << "TranslatePin grab after" << size()
                    << "panel" << (m_panel ? m_panel->geometry() : QRect())
                    << (m_panel && m_panel->isVisible())
                    << "edit" << (m_textEdit ? m_textEdit->geometry() : QRect())
                    << (m_textEdit && m_textEdit->isVisible())
                    << "editParent" << (m_textEdit ? m_textEdit->parentWidget() : nullptr);
        });
    }
    refreshTextPanel();
    updateCanvas();
    update();
    raise();
}

QString TranslatePinWindow::allTranslations() const {
    QStringList out;
    for (int i = 0; i < m_texts.size(); ++i) {
        if (!m_texts.at(i).trimmed().isEmpty())
            out << m_texts.at(i);
    }
    return out.join('\n');
}

QString TranslatePinWindow::allSources() const {
    QStringList out;
    for (const OcrLine& l : m_lines)
        out << l.text;
    return out.join('\n');
}

static QColor averageColor(const QImage& img, const QRect& rect) {
    const QRect r = rect.intersected(img.rect());
    if (r.isEmpty())
        return QColor(30, 30, 30);
    const int step = qMax(1, qMin(r.width(), r.height()) / 20);
    qint64 rr = 0, gg = 0, bb = 0, n = 0;
    for (int y = r.top(); y <= r.bottom(); y += step) {
        for (int x = r.left(); x <= r.right(); x += step) {
            const QColor c = img.pixelColor(x, y);
            rr += c.red();
            gg += c.green();
            bb += c.blue();
            ++n;
        }
    }
    if (!n)
        return QColor(30, 30, 30);
    return QColor(int(rr / n), int(gg / n), int(bb / n));
}

// 取文字行“背景色”：采样框外一圈，取出现次数最多的量化颜色。
// 直接用平均值会把黑底白字混成灰色，整张图看起来灰蒙蒙。
static QColor backgroundColor(const QImage& img, const QRect& box) {
    QRect outer = box.adjusted(-4, -2, 4, 2).intersected(img.rect());
    QRect inner = box.intersected(img.rect());
    if (outer.isEmpty() || inner.isEmpty())
        return QColor(30, 30, 30);

    QHash<QRgb, int> hist;
    auto addPixel = [&](int x, int y) {
        const QRgb c = img.pixel(x, y);
        const QRgb q = qRgb(qRed(c) & 0xF8, qGreen(c) & 0xF8, qBlue(c) & 0xF8);
        hist[q] += 1;
    };
    for (int y = outer.top(); y <= outer.bottom(); ++y) {
        for (int x = outer.left(); x <= outer.right(); ++x) {
            if (!inner.contains(x, y))
                addPixel(x, y);
        }
    }
    if (hist.isEmpty()) {
        for (int y = outer.top(); y <= outer.bottom(); y += 2)
            for (int x = outer.left(); x <= outer.right(); x += 2)
                addPixel(x, y);
    }
    QRgb best = 0;
    int bestCount = -1;
    for (auto it = hist.constBegin(); it != hist.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return QColor(qRed(best), qGreen(best), qBlue(best));
}

void TranslatePinWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QSize img = imageSize();
    const int pad = m_border ? 1 : 0;
    const QRect imgRect(pad, toolbarHeight() + pad, img.width(), img.height());
    p.fillRect(rect(), QColor(20, 20, 22));
    p.drawImage(imgRect, m_img);

    const double d = m_img.devicePixelRatio() > 0 ? m_img.devicePixelRatio() : 1.0;
    const double sx = double(imgRect.width()) / qMax(1, m_baseLogical.width());

    for (int i = 0; i < m_lines.size() && i < m_texts.size(); ++i) {
        if (m_showOriginal)
            break;
        const QString text = m_texts.at(i).trimmed();
        if (text.isEmpty())
            continue;
        if (text == m_lines.at(i).text.trimmed())
            continue;  // 译文与原文相同，保留原始像素
        const QRectF r(imgRect.x() + m_lines.at(i).box.x() / d * sx,
                       imgRect.y() + m_lines.at(i).box.y() / d * sx,
                       m_lines.at(i).box.width() / d * sx,
                       m_lines.at(i).box.height() / d * sx);
        if (r.width() < 2 || r.height() < 2)
            continue;

        // 用文字行外圈的主色做底（不是平均值），避免黑底白字被刷成灰色
        const QColor cover = backgroundColor(m_img, m_lines.at(i).box);
        const bool dark = cover.lightness() < 140;
        const QColor fg = dark ? QColor(245, 245, 245) : QColor(20, 20, 20);

        p.setPen(Qt::NoPen);
        p.setBrush(cover);
        p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 2, 2);

        QFont f = font();
        int px = qMax(8, int(r.height() * 0.68));
        f.setPixelSize(px);
        QFontMetricsF fm(f);
        while (px > 7 && fm.horizontalAdvance(text) > r.width() - 2) {
            --px;
            f.setPixelSize(px);
            fm = QFontMetricsF(f);
        }
        p.setFont(f);
        p.setPen(fg);
        QString shown = text;
        if (fm.horizontalAdvance(shown) > r.width() - 2)
            shown = fm.elidedText(shown, Qt::ElideRight, int(r.width() - 2));
        p.drawText(r.adjusted(1, 0, -1, 0), Qt::AlignLeft | Qt::AlignVCenter, shown);
    }

    if (!m_message.isEmpty()) {
        const QRect badge(imgRect.x() + 6, imgRect.y() + 6, 150, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 190));
        p.drawRoundedRect(badge, 10, 10);
        p.setPen(Qt::white);
        p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
        p.drawText(badge, Qt::AlignCenter, m_message);
    }
    if (m_border) {
        p.setPen(QPen(QColor(0, 160, 255), 1));
        p.drawRect(QRect(0, toolbarHeight(), img.width() + 2, img.height() + 2)
                       .adjusted(0, 0, -1, -1));
    }
}

void TranslatePinWindow::applyZoom() {
    updateWindowLayout();
}

void TranslatePinWindow::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
    }
}

void TranslatePinWindow::mouseMoveEvent(QMouseEvent* e) {
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPosition().toPoint() - m_dragOffset);
        e->accept();
    }
}

void TranslatePinWindow::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        copyPrimaryText();
        setMessage(QStringLiteral("已复制"));
        QTimer::singleShot(900, this, [this]() { m_message.clear(); update(); });
    }
}

void TranslatePinWindow::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (e->key() == Qt::Key_T) {
        setShowOriginal(!m_showOriginal);
        return;
    }
    QWidget::keyPressEvent(e);
}

void TranslatePinWindow::wheelEvent(QWheelEvent* e) {
    const int d = e->angleDelta().y();
    if (d == 0) {
        QWidget::wheelEvent(e);
        return;
    }
    m_zoom = qBound(0.3, m_zoom * (d > 0 ? 1.1 : 0.9), 6.0);
    applyZoom();
    e->accept();
}

void TranslatePinWindow::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    if (m_translateMode)
        menu.addAction(m_showOriginal ? QStringLiteral("显示译文") : QStringLiteral("显示原文"),
                       this, [this]() { setShowOriginal(!m_showOriginal); });

    QAction* toolbar = menu.addAction(QStringLiteral("显示工具条"));
    toolbar->setCheckable(true);
    toolbar->setChecked(m_showToolbar);
    connect(toolbar, &QAction::toggled, this, [this](bool on) { setToolbarVisible(on); });

    QAction* panel = menu.addAction(QStringLiteral("显示文本栏"));
    panel->setCheckable(true);
    panel->setChecked(m_panelVisible);
    connect(panel, &QAction::toggled, this, [this](bool on) { setTextPanelVisible(on); });

    if (m_translateMode && m_lang) {
        QMenu* langMenu = menu.addMenu(QStringLiteral("翻译语言"));
        const QString current = m_lang->currentText();
        for (int i = 0; i < m_lang->count(); ++i) {
            QAction* a = langMenu->addAction(m_lang->itemText(i));
            a->setCheckable(true);
            a->setChecked(m_lang->itemText(i) == current);
            const QString name = m_lang->itemText(i);
            connect(a, &QAction::triggered, this, [this, name]() { setTargetLanguage(name); });
        }
    }

    menu.addSeparator();
    menu.addAction(m_translateMode ? QStringLiteral("复制译文") : QStringLiteral("复制文字"), this, [this]() {
        copyPrimaryText();
    });
    if (m_translateMode) {
        menu.addAction(QStringLiteral("复制原文"), this, [this]() {
            QApplication::clipboard()->setText(allSources());
        });
    }
    menu.addAction(QStringLiteral("保存为图片…"), this, [this]() {
        Settings st = Settings::load();
        QDir().mkpath(st.saveDir);
        const QString fn = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存图片"),
            st.saveDir + "/Snip_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png",
            QStringLiteral("图片 (*.png)"));
        if (!fn.isEmpty())
            grab().save(fn);
    });
    menu.addSeparator();
    QAction* border = menu.addAction(QStringLiteral("显示边框"));
    border->setCheckable(true);
    border->setChecked(m_border);
    connect(border, &QAction::toggled, this, [this](bool on) {
        m_border = on;
        updateWindowLayout();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("关闭"), this, &QWidget::close);
    menu.exec(e->globalPos());
    update();
}

void PinManager::registerPin(QWidget* w) {
    if (!w)
        return;
    m_pins.append(w);
    connect(w, &QObject::destroyed, this, [this, w]() { m_pins.removeAll(w); });
}
