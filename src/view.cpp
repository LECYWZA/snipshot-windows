#include "view.h"
#include "session.h"
#include "screenutil.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextEdit>
#include <QColorDialog>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <cmath>

static const QColor kAccent(0, 160, 255);
static const QColor kBtnBg(40, 40, 40, 235);
static const QColor kBtnSel(0, 120, 215);
static const QStringList kToolNames{
    QStringLiteral("移动"), QStringLiteral("矩形"), QStringLiteral("椭圆"), QStringLiteral("箭头"),
    QStringLiteral("画笔"), QStringLiteral("马赛克"), QStringLiteral("文字"), QStringLiteral("取色"),
    QStringLiteral("颜色"), QStringLiteral("－"), QStringLiteral("＋")
};
static const QStringList kActionNames{
    QStringLiteral("保存"), QStringLiteral("复制"), QStringLiteral("贴图"),
    QStringLiteral("翻译"), QStringLiteral("OCR"), QStringLiteral("撤销")
};

OverlayView::OverlayView(CaptureSession* session, QScreen* screen, QWidget* parent)
    : QWidget(parent), m_session(session), m_screen(screen) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    winId();
    if (windowHandle()) {
        connect(windowHandle(), &QWindow::screenChanged, this, [this](QScreen* s) {
            if (s && s != m_screen && geometry() == s->geometry()) {
                m_screen = s;
                update();
                syncEditor();
            }
        });
    }
    // Windows 下 showFullScreen() 会把窗口吸到“当前屏幕”，负坐标副屏容易跑到主屏；
    // 这里改成无边框、铺满目标屏几何区域的普通窗口，定位稳定且照样盖住任务栏。
    setGeometry(logicalScreenGeometry(screen));
    qInfo() << "OverlayView 请求屏幕:" << (screen ? screen->name() : QStringLiteral("?"))
            << logicalScreenGeometry(screen);
    show();
    raise();
    if (windowHandle())
        windowHandle()->setScreen(screen);
    setGeometry(logicalScreenGeometry(screen));
    QTimer::singleShot(200, this, [this]() {
        if (windowHandle() && windowHandle()->screen()) {
            qInfo() << "OverlayView 实际屏幕:" << windowHandle()->screen()->name()
                    << windowHandle()->screen()->geometry() << "窗口:" << geometry();
        }
    });
    connect(m_session, &CaptureSession::changed, this, &OverlayView::onSessionChanged);
    connect(m_session, &CaptureSession::textEditingStarted, this, [this]() { syncEditor(); });
    connect(m_session, &CaptureSession::textEditingFinished, this, [this]() {
        destroyEditor();
        update();
    });
}

OverlayView::~OverlayView() {
    destroyEditor();
}

void OverlayView::setActive(bool on) {
    if (m_active == on)
        return;
    if (!on)
        destroyEditor();
    m_active = on;
    if (on) {
        show();
        raise();
        if (windowHandle())
            windowHandle()->requestActivate();
        setFocus();
        syncEditor();
    }
    update();
}

QPoint OverlayView::toGlobal(const QPoint& local) const {
    return local + m_session->screenRect(m_screen).topLeft();
}

QRect OverlayView::toolbarRect(bool tools) const {
    return toolbarRectFor(m_session->selection(), tools);
}

static Qt::CursorShape handleCursor(int h) {
    switch (h) {
    case CaptureSession::TopLeft:
    case CaptureSession::BottomRight:
        return Qt::SizeFDiagCursor;
    case CaptureSession::TopRight:
    case CaptureSession::BottomLeft:
        return Qt::SizeBDiagCursor;
    case CaptureSession::Left:
    case CaptureSession::Right:
        return Qt::SizeHorCursor;
    case CaptureSession::Top:
    case CaptureSession::Bottom:
        return Qt::SizeVerCursor;
    case CaptureSession::Rotate:
        return Qt::PointingHandCursor;
    case CaptureSession::Check:
        return Qt::PointingHandCursor;
    default:
        return Qt::ArrowCursor;
    }
}

void OverlayView::updateCursorAt(const QPoint& g) {
    if (!m_session) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_session->pickerActive()) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_session->hasSelection()) {
        if (toolbarRect(true).contains(g) || toolbarRect(false).contains(g)) {
            setCursor(Qt::ArrowCursor);
            return;
        }
        const int sh = m_session->selectionHandleAt(g);
        if (sh != CaptureSession::NoHandle) {
            setCursor(handleCursor(sh));
            return;
        }
        const int h = m_session->handleAt(g);
        if (h != CaptureSession::NoHandle) {
            setCursor(handleCursor(h));
            return;
        }
        if (m_session->selection().contains(g)) {
            if (m_session->hasResult()) {
                setCursor(Qt::ArrowCursor);
                return;
            }
            switch (m_session->tool()) {
            case CaptureSession::Move:
                setCursor(Qt::SizeAllCursor);
                break;
            case CaptureSession::Text:
                setCursor(Qt::IBeamCursor);
                break;
            default:
                setCursor(Qt::CrossCursor);
                break;
            }
            return;
        }
    }
    setCursor(Qt::CrossCursor);
}

void OverlayView::updateHover(const QPoint& local) {
    if (!m_session || !m_session->hasSelection())
        return;
    const int t = toolbarHit(local, true);
    const int a = toolbarHit(local, false);
    if (t == m_hoverToolIdx && a == m_hoverActionIdx)
        return;
    m_hoverToolIdx = t;
    m_hoverActionIdx = a;
    const QRect scr = m_session->screenRect(m_screen);
    update(toolbarRect(true).translated(-scr.topLeft()));
    update(toolbarRect(false).translated(-scr.topLeft()));
}

QRect OverlayView::toolbarRectFor(const QRect& sel, bool tools) const {
    const QRect scr = m_session->screenRect(m_screen);
    const int bw = 52, bh = 28, gap = 4;
    const int n = tools ? kToolNames.size() : kActionNames.size();
    const int w = n * (bw + gap) + gap;
    const int h = bh + gap * 2;
    int x = sel.right() - w + 1;
    int y;
    if (tools) {
        y = sel.top() - h - gap;
        if (y < scr.top())
            y = sel.top() + gap;
    } else {
        y = sel.bottom() + gap;
        if (y + h > scr.bottom())
            y = sel.bottom() - h - gap;
    }
    if (x < scr.left())
        x = scr.left();
    if (x + w > scr.right())
        x = scr.right() - w;
    if (y < scr.top())
        y = scr.top();
    if (y + h > scr.bottom())
        y = scr.bottom() - h;
    return QRect(x, y, w, h);
}

int OverlayView::toolbarHit(const QPoint& local, bool tools) const {
    const QPoint g = toGlobal(local);
    const QRect tb = toolbarRect(tools);
    if (!tb.contains(g))
        return -1;
    const int bw = 52, bh = 28, gap = 4;
    int x = tb.left() + gap;
    for (int i = 0; i < (tools ? kToolNames.size() : kActionNames.size()); ++i) {
        if (QRect(x, tb.top() + gap, bw, bh).contains(g))
            return i;
        x += bw + gap;
    }
    return -1;
}

void OverlayView::toolbarClick(int idx, bool tools) {
    if (!tools) {
        switch (idx) {
        case 0: m_session->requestAction(0); break;
        case 1: m_session->requestAction(1); break;
        case 2: m_session->requestAction(2); break;
        case 3: m_session->requestAction(3); break;
        case 4: m_session->requestAction(4); break;
        case 5: m_session->undo(); break;
        default: break;
        }
        return;
    }
    switch (idx) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6:
        m_session->setPicker(false);
        m_session->setTool(CaptureSession::Tool(idx));
        break;
    case 7:
        m_session->setPicker(!m_session->pickerActive());
        break;
    case 8:
        showColorMenu();
        break;
    case 9:
        m_session->setWidth(m_session->width() - 1.0);
        break;
    case 10:
        m_session->setWidth(m_session->width() + 1.0);
        break;
    default:
        break;
    }
    if (tools)
        updateCursorAt(QCursor::pos());
}

void OverlayView::showColorMenu() {
    QMenu menu(this);
    const QColor presets[] = {
        QColor(255, 60, 60), QColor(255, 180, 0), QColor(255, 255, 0),
        QColor(60, 220, 100), QColor(0, 180, 255), QColor(180, 100, 255),
        QColor(255, 255, 255), QColor(0, 0, 0)
    };
    for (const QColor& c : presets) {
        QPixmap pm(18, 18);
        pm.fill(c);
        QAction* a = menu.addAction(QIcon(pm), c.name(QColor::HexRgb));
        connect(a, &QAction::triggered, this, [this, c]() { m_session->setColor(c); });
    }
    menu.addSeparator();
    QAction* custom = menu.addAction(QStringLiteral("自定义…"));
    connect(custom, &QAction::triggered, this, [this]() {
        const QColor c = QColorDialog::getColor(m_session->color(), this,
                                                QStringLiteral("选择颜色"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            m_session->setColor(c);
    });
    menu.exec(QCursor::pos());
}

void OverlayView::drawToolbar(QPainter& p, bool tools) {
    const QRect tb = toolbarRect(tools);
    p.fillRect(tb, kBtnBg);
    p.setPen(QPen(QColor(90, 90, 90), 1));
    p.drawRect(tb);
    const int bw = 52, bh = 28, gap = 4;
    int x = tb.left() + gap;
    const QStringList& names = tools ? kToolNames : kActionNames;
    for (int i = 0; i < names.size(); ++i) {
        const QRect r(x, tb.top() + gap, bw, bh);
        bool active = false;
        if (tools) {
            if (i <= 6)
                active = m_session->tool() == CaptureSession::Tool(i) && !m_session->pickerActive();
            else if (i == 7)
                active = m_session->pickerActive();
        }
        const bool hovered = tools ? (i == m_hoverToolIdx) : (i == m_hoverActionIdx);
        const bool pressed = tools ? (i == m_pressedToolIdx) : (i == m_pressedActionIdx);
        const QColor bg = active   ? kBtnSel
                          : pressed ? QColor(0, 110, 190)
                          : hovered ? QColor(0, 160, 255, 190)
                                    : QColor(60, 60, 60);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r.adjusted(0, 0, -1, -1), 4, 4);
        if (tools && i == 8) {
            p.fillRect(r.adjusted(6, 6, -6, -6), m_session->color());
            p.setPen(QPen(Qt::white, 1));
            p.drawRect(r.adjusted(6, 6, -6, -6));
        } else {
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setPixelSize(12);
            p.setFont(f);
            p.drawText(r, Qt::AlignCenter, names[i]);
        }
        x += bw + gap;
    }
}

void OverlayView::drawPicker(QPainter& p) {
    const QPoint g = m_session->pickerPos();
    const QColor c = m_session->colorAt(g);
    if (!c.isValid())
        return;
    QRect panel(g.x() + 18, g.y() + 18, 210, 64);
    const QRect scr = m_session->screenRect(m_screen);
    if (panel.right() > scr.right())
        panel.moveRight(g.x() - 18);
    if (panel.bottom() > scr.bottom())
        panel.moveBottom(g.y() - 18);
    p.fillRect(panel, QColor(0, 0, 0, 210));
    p.setPen(QPen(kAccent, 1));
    p.drawRect(panel);
    QRect sw(panel.left() + 8, panel.top() + 8, 48, 48);
    p.fillRect(sw, c);
    p.setPen(QPen(Qt::white, 1));
    p.drawRect(sw);
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(panel.left() + 64, panel.top() + 8, 140, 20), Qt::AlignLeft | Qt::AlignVCenter,
               c.name(QColor::HexRgb));
    p.drawText(QRect(panel.left() + 64, panel.top() + 30, 140, 20), Qt::AlignLeft | Qt::AlignVCenter,
               QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
    p.setPen(QColor(180, 180, 180));
    p.drawText(QRect(panel.left() + 8, panel.top() + 58, 194, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("点击复制 • Esc 退出"));
}

void OverlayView::drawResultHint(QPainter& p) {
    const QRect sel = m_session->selection();
    if (sel.isEmpty())
        return;
    const QString hint = QStringLiteral("Ctrl+C 复制 • Esc 关闭");
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    QRect r(sel.left(), sel.top() - 24, 260, 20);
    if (r.top() < m_session->screenRect(m_screen).top())
        r.moveTop(sel.top() + 4);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, hint);
}

void OverlayView::onSessionChanged() {
    if (!m_session)
        return;
    const QRect sel = m_session->selection();
    const bool result = m_session->hasResult();
    const bool processing = m_session->processing();
    const bool picker = m_session->pickerActive();
    const QString state = QString("%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(int(m_session->mode()))
        .arg(int(m_session->tool()))
        .arg(m_session->hasSelection())
        .arg(m_session->selecting())
        .arg(result)
        .arg(processing)
        .arg(m_session->editingText())
        .arg(m_session->selectedAnn() != nullptr)
        .arg(m_session->color().rgba() ^ int(m_session->width()));

    // 状态变化（工具/选区创建完成/结果等）才整屏重绘；
    // 拖拽过程中只刷旧选区+新选区的脏矩形。
    if (state != m_prevState || result || processing || picker) {
        m_prevState = state;
        m_prevSelection = sel;
        update();
        syncEditor();
        return;
    }

    // 残影多来自旧工具栏/旧控制手柄的外框：把旧、新工具栏和手柄
    // 都显式并进脏区，并留足边距抗高分屏取整。
    QRect dirty = m_prevSelection.united(sel).adjusted(-100, -100, 100, 100);
    if (!m_prevSelection.isEmpty())
        dirty |= toolbarRectFor(m_prevSelection, true) | toolbarRectFor(m_prevSelection, false);
    if (m_session->hasSelection() && !result) {
        dirty |= toolbarRect(true);
        dirty |= toolbarRect(false);
    }
    m_prevSelection = sel;

    const QRect local = dirty.translated(-m_session->screenRect(m_screen).topLeft())
                            .intersected(rect());
    if (!local.isEmpty())
        update(local);
    syncEditor();
    if (QGuiApplication::mouseButtons() == Qt::NoButton)
        updateCursorAt(QCursor::pos());
}

void OverlayView::paintEvent(QPaintEvent* ev) {
    QElapsedTimer timer;
    timer.start();
    QPainter p(this);
    m_session->paint(p, m_screen, ev->rect());
    // 选区手柄两块屏都画，避免跨屏时另一半手柄消失
    m_session->paintSelectionHandles(p, m_screen);
    if (!m_active)
        return;
    QPainter q(this);
    q.translate(-m_session->screenRect(m_screen).topLeft());
    if (!m_session->hasResult() && m_session->mode() == CaptureSession::Snip
        && m_session->hasSelection() && !m_session->processing()) {
        drawToolbar(q, true);
        drawToolbar(q, false);
    }
    m_session->paintHandles(p, m_screen);
    if (m_session->pickerActive())
        drawPicker(q);
    if (m_session->hasResult())
        drawResultHint(q);
    if (m_session->processing()) {
        const QRect sel = m_session->selection();
        if (!sel.isEmpty()) {
            q.fillRect(QRect(sel.center() - QPoint(70, 20), QSize(140, 40)), QColor(0, 0, 0, 180));
            q.setPen(Qt::white);
            q.drawText(QRect(sel.center() - QPoint(70, 20), QSize(140, 40)), Qt::AlignCenter,
                       m_session->mode() == CaptureSession::Translate ? QStringLiteral("翻译中…")
                                                                    : QStringLiteral("识别中…"));
        }
    }
    const qint64 ms = timer.elapsed();
    static int count = 0;
    if (ms >= 8 || (++count % 120) == 0)
        qInfo() << "paint" << ms << "ms dirty" << ev->rect().size()
                << "sel" << m_session->selection().size();
}

void OverlayView::mousePressEvent(QMouseEvent* e) {
    qInfo() << "View mouse" << e->button() << toGlobal(e->position().toPoint());
    if (!m_active) {
        emit activated(this);
        setActive(true);
    }
    const QPoint g = toGlobal(e->position().toPoint());

    if (e->button() == Qt::RightButton) {
        if (m_session->pickerActive()) {
            m_session->setPicker(false);
            return;
        }
        QMenu menu(this);
        if (m_session->hasResult()) {
            menu.addAction(QStringLiteral("复制结果"), this, [this]() {
                QApplication::clipboard()->setText(m_session->resultText());
            });
            menu.addSeparator();
            menu.addAction(QStringLiteral("关闭"), this, [this]() { emit cancelRequested(); });
        } else if (m_session->hasSelection()) {
            menu.addAction(QStringLiteral("复制"), this, [this]() {
                QApplication::clipboard()->setImage(m_session->currentResult());
            });
            menu.addAction(QStringLiteral("保存"), this, [this]() { m_session->requestAction(0); });
            menu.addAction(QStringLiteral("贴图"), this, [this]() { m_session->requestAction(2); });
            menu.addAction(QStringLiteral("翻译"), this, [this]() { m_session->requestAction(3); });
            menu.addAction(QStringLiteral("文字提取"), this, [this]() { m_session->requestAction(4); });
            menu.addSeparator();
            menu.addAction(QStringLiteral("撤销"), this, [this]() { m_session->undo(); });
            menu.addAction(QStringLiteral("关闭"), this, [this]() { emit cancelRequested(); });
        } else {
            menu.addAction(QStringLiteral("关闭"), this, [this]() { emit cancelRequested(); });
        }
        menu.exec(e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    const int tIdx = toolbarHit(e->position().toPoint(), true);
    if (tIdx >= 0) {
        m_pressedToolIdx = tIdx;
        update(toolbarRect(true).translated(-m_session->screenRect(m_screen).topLeft()));
        return;
    }
    const int aIdx = toolbarHit(e->position().toPoint(), false);
    if (aIdx >= 0) {
        m_pressedActionIdx = aIdx;
        update(toolbarRect(false).translated(-m_session->screenRect(m_screen).topLeft()));
        return;
    }
    if (m_session->pickerActive()) {
        m_session->copyColorAt(g);
        m_session->setPicker(false);
        return;
    }
    if (m_session->hasResult())
        return;

    const int h = m_session->handleAt(g);
    if (h == CaptureSession::Check) {
        if (m_session->editingText())
            m_session->commitEditingText();
        else
            m_session->deselect();
        return;
    }
    if (h == CaptureSession::Rotate) {
        m_session->beginRotate(g);
        m_rotating = true;
        return;
    }
    if (h != CaptureSession::NoHandle) {
        m_handle = h;
        m_resizing = true;
        m_session->resizeSelected(h, g);
        return;
    }
    // 选区边框手柄：整体缩放选区
    if (m_session->hasSelection()) {
        const int sh = m_session->selectionHandleAt(g);
        if (sh != CaptureSession::NoHandle) {
            m_session->beginSelectionResize(sh, g);
            m_selResizing = true;
            m_selHandle = sh;
            return;
        }
    }
    if (m_session->editingText()) {
        m_session->commitEditingText();
        return;
    }
    if (!m_session->hasSelection()) {
        m_session->beginSelection(g);
        return;
    }

    // 移动工具：拖选区内整体平移，不画标注
    if (m_session->tool() == CaptureSession::Move) {
        if (m_session->selection().contains(g)) {
            m_session->beginSelectionMove(g);
            m_selMoving = true;
        } else {
            m_session->beginSelection(g);
        }
        return;
    }

    const int ann = m_session->annotationAt(g);
    if (ann >= 0) {
        m_session->selectAnn(ann);
        m_moving = true;
        m_lastGlobal = g;
        return;
    }
    if (m_session->selection().contains(g)) {
        if (m_session->tool() == CaptureSession::Text)
            m_session->beginText(g);
        else
            m_session->beginAnnotation(g);
        return;
    }
    m_session->beginSelection(g);
}

void OverlayView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint g = toGlobal(e->position().toPoint());
    const bool dragging = m_session->selecting() || m_session->drawing() || m_moving ||
                          m_resizing || m_rotating || m_selMoving || m_selResizing;
    if (!dragging) {
        updateHover(e->position().toPoint());
        updateCursorAt(g);
    }
    if (m_session->pickerActive()) {
        m_session->setPickerPos(g);
        return;
    }
    if (m_session->selecting()) {
        m_session->updateSelection(g);
        return;
    }
    if (m_moving) {
        const QPoint delta = g - m_lastGlobal;
        m_session->moveSelected(g, delta);
        m_lastGlobal = g;
        return;
    }
    if (m_resizing) {
        m_session->resizeSelected(m_handle, g);
        return;
    }
    if (m_rotating) {
        m_session->updateRotate(g);
        return;
    }
    if (m_selMoving) {
        m_session->updateSelectionMove(g);
        return;
    }
    if (m_selResizing) {
        m_session->updateSelectionResize(g);
        return;
    }
    if (m_session->drawing())
        m_session->updateAnnotation(g);
}

void OverlayView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const QPoint local = e->position().toPoint();
    const QRect scr = m_session->screenRect(m_screen);
    if (m_pressedToolIdx >= 0) {
        const int pressed = m_pressedToolIdx;
        m_pressedToolIdx = -1;
        if (m_session->hasSelection())
            update(toolbarRect(true).translated(-scr.topLeft()));
        if (toolbarHit(local, true) == pressed) {
            toolbarClick(pressed, true);
            return;
        }
    }
    if (m_pressedActionIdx >= 0) {
        const int pressed = m_pressedActionIdx;
        m_pressedActionIdx = -1;
        if (m_session->hasSelection())
            update(toolbarRect(false).translated(-scr.topLeft()));
        if (toolbarHit(local, false) == pressed) {
            toolbarClick(pressed, false);
            return;
        }
    }
    if (m_session->selecting()) {
        m_session->endSelection();
    } else if (m_session->drawing()) {
        m_session->finishAnnotation();
    }
    m_moving = false;
    m_resizing = false;
    m_rotating = false;
    m_selMoving = false;
    m_selResizing = false;
    updateCursorAt(toGlobal(local));
}

void OverlayView::keyPressEvent(QKeyEvent* e) {
    qInfo() << "View key" << e->key();
    if (e->key() == Qt::Key_Escape) {
        if (m_session->pickerActive()) {
            m_session->setPicker(false);
            return;
        }
        if (m_session->editingText()) {
            m_session->cancelEditingText();
            return;
        }
        emit cancelRequested();
        return;
    }
    if (!m_session->editingText() && !m_session->hasResult() && m_session->hasSelection()) {
        int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        QPoint d;
        if (e->key() == Qt::Key_Left) d = QPoint(-step, 0);
        else if (e->key() == Qt::Key_Right) d = QPoint(step, 0);
        else if (e->key() == Qt::Key_Up) d = QPoint(0, -step);
        else if (e->key() == Qt::Key_Down) d = QPoint(0, step);
        if (!d.isNull()) {
            m_session->nudgeSelection(d);
            return;
        }
    }
    if (e->key() == Qt::Key_Z && (e->modifiers() & Qt::ControlModifier)) {
        m_session->undo();
        return;
    }
    if (e->key() == Qt::Key_C && (e->modifiers() & Qt::ControlModifier)) {
        if (m_session->hasResult()) {
            QApplication::clipboard()->setText(m_session->resultText());
        } else if (m_session->hasSelection()) {
            QApplication::clipboard()->setImage(m_session->currentResult());
        }
        return;
    }
    if (e->key() == Qt::Key_C && !(e->modifiers() & Qt::ControlModifier)) {
        if (m_session->mode() == CaptureSession::Snip)
            m_session->setPicker(!m_session->pickerActive());
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
        && !m_session->editingText()) {
        if (m_session->hasSelection())
            m_session->requestAction(0);
        return;
    }
    if (e->key() == Qt::Key_F2) {
        if (m_session->hasSelection())
            m_session->requestAction(2);
        return;
    }
    QWidget::keyPressEvent(e);
}

bool OverlayView::eventFilter(QObject* o, QEvent* e) {
    if (o == m_editor && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) {
            m_session->cancelEditingText();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && (ke->modifiers() & Qt::ControlModifier)) {
            m_session->commitEditingText();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

void OverlayView::syncEditor() {
    if (!m_active || !m_session->editingText() || m_session->editingIndex() < 0) {
        destroyEditor();
        return;
    }
    const QRect g = m_session->editingRect();
    const QRect scr = m_session->screenRect(m_screen);
    if (!g.intersects(scr)) {
        destroyEditor();
        return;
    }
    if (!m_editor) {
        m_editor = new QTextEdit(this);
        m_editor->setLineWrapMode(QTextEdit::WidgetWidth);
        m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_editor->setFrameShape(QFrame::NoFrame);
        m_editor->installEventFilter(this);
        connect(m_editor, &QTextEdit::textChanged, this, [this]() {
            if (m_editor && m_session->editingText())
                m_session->setEditingText(m_editor->toPlainText());
        });
    }
    const CaptureSession::Ann* ann = m_session->selectedAnn();
    QFont f;
    f.setPixelSize(ann ? qMax(10, qRound(ann->width)) : 18);
    m_editor->setFont(f);
    const QColor c = ann ? ann->color : QColor(255, 60, 60);
    m_editor->setStyleSheet(QString("QTextEdit { background: rgba(0,0,0,0); border: 1px dashed rgba(0,190,255,180); color: %1; }")
                                .arg(c.name(QColor::HexRgb)));
    QRect local = g.translated(-scr.topLeft()).adjusted(6, 6, -6, -6);
    if (local.width() < 60) local.setWidth(60);
    if (local.height() < 32) local.setHeight(32);
    m_editor->setGeometry(local);
    const QString annText = ann ? ann->text : QString();
    if (m_editor->toPlainText() != annText) {
        const bool blocked = m_editor->blockSignals(true);
        m_editor->setPlainText(annText);
        m_editor->blockSignals(blocked);
        QTextCursor cur = m_editor->textCursor();
        cur.movePosition(QTextCursor::End);
        m_editor->setTextCursor(cur);
    }
    m_editor->show();
    m_editor->setFocus();
}

void OverlayView::commitEditor() {
    if (m_session->editingText())
        m_session->commitEditingText();
    destroyEditor();
}

void OverlayView::destroyEditor() {
    if (!m_editor)
        return;
    m_editor->removeEventFilter(this);
    m_editor->hide();
    m_editor->deleteLater();
    m_editor = nullptr;
}

void OverlayView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    setFocus();
}
