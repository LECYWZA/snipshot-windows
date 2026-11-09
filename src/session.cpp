#include "session.h"
#include "screenutil.h"

#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>
#include <QApplication>
#include <QDebug>
#include <QtMath>
#include <QFontMetrics>
#include <QLineF>
#include <cmath>

static const QColor kMask(0, 0, 0, 130);
static const QColor kAccent(0, 160, 255);
static const QColor kFrame(0, 190, 255);

static qreal angleDeg(const QPointF& c, const QPointF& p) {
    return qRadiansToDegrees(std::atan2(p.y() - c.y(), p.x() - c.x()));
}

CaptureSession::CaptureSession(const QImage& fullImage, const QList<ScreenShot>& shots,
                               Mode mode, QObject* parent)
    : QObject(parent), m_full(fullImage), m_shots(shots), m_desk(virtualDesktopRect()), m_mode(mode) {
    if (!m_shots.isEmpty()) {
        QRect desk;
        for (const ScreenShot& s : m_shots)
            desk = desk.united(s.logicalRect);
        if (!desk.isEmpty())
            m_desk = desk;
    }
    qInfo() << "Session 桌面:" << m_desk << "图片:" << m_full.size() << "逐屏快照:" << m_shots.size();
}

QRect CaptureSession::screenRect(QScreen* s) const {
    return logicalScreenGeometry(s);
}

QRect CaptureSession::activeScreenRect() const {
    return m_desk;
}

bool CaptureSession::hasSelection() const {
    const QRect r = selection();
    return m_hasSel && r.width() > 4 && r.height() > 4;
}

void CaptureSession::beginSelection(const QPoint& global) {
    m_editing = -1;
    m_selected = -1;
    m_anns.clear();
    m_hasResult = false;
    m_resultText.clear();
    m_processing = false;
    m_picker = false;
    m_selecting = true;
    m_hasSel = false;
    m_start = global;
    m_sel = QRect(global, global);
    emit changed();
}

void CaptureSession::updateSelection(const QPoint& global) {
    if (!m_selecting)
        return;
    m_sel = QRect(m_start, global).normalized();
    emit changed();
}

void CaptureSession::endSelection() {
    if (!m_selecting)
        return;
    m_selecting = false;
    if (selection().width() > 4 && selection().height() > 4) {
        m_hasSel = true;
        m_tool = Move;
        if (m_mode == Translate || m_mode == Ocr) {
            m_processing = true;
            emit changed();
            emit aiRequested(currentResult(), selection(), int(m_mode == Translate ? 0 : 1));
            return;
        }
        emit selectionChanged(currentResult(), selection());
    } else {
        m_hasSel = false;
    }
    emit changed();
}

void CaptureSession::cancelSelection() {
    m_selecting = false;
    m_hasSel = false;
    m_sel = QRect();
    emit changed();
}

int CaptureSession::selectionHandleAt(const QPoint& global) const {
    if (!hasSelection())
        return NoHandle;
    const QRect s = selection();
    const QPointF pts[8] = {
        QPointF(s.topLeft()), QPointF(s.topRight()), QPointF(s.bottomLeft()), QPointF(s.bottomRight()),
        QPointF(s.center().x(), s.top()), QPointF(s.center().x(), s.bottom()),
        QPointF(s.left(), s.center().y()), QPointF(s.right(), s.center().y())
    };
    const int hs[8] = {TopLeft, TopRight, BottomLeft, BottomRight, Top, Bottom, Left, Right};
    for (int i = 0; i < 8; ++i) {
        if (QLineF(global, pts[i]).length() < 14.0)
            return hs[i];
    }
    return NoHandle;
}

void CaptureSession::beginSelectionMove(const QPoint& global) {
    if (!hasSelection() || m_hasResult)
        return;
    m_selMoving = true;
    m_selLast = global;
    m_selected = -1;
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::updateSelectionMove(const QPoint& global) {
    if (!m_selMoving)
        return;
    const QPoint wanted = global - m_selLast;
    QRect s = selection();
    QRect ns = s.translated(wanted);
    int dx = 0, dy = 0;
    if (ns.left() < m_desk.left())
        dx = m_desk.left() - ns.left();
    if (ns.right() > m_desk.right())
        dx = m_desk.right() - ns.right();
    if (ns.top() < m_desk.top())
        dy = m_desk.top() - ns.top();
    if (ns.bottom() > m_desk.bottom())
        dy = m_desk.bottom() - ns.bottom();
    QPoint actual = wanted + QPoint(dx, dy);
    if (actual.isNull()) {
        m_selLast = global;
        return;
    }
    m_sel = s.translated(actual);
    for (Ann& a : m_anns) {
        a.rect.translate(actual);
        a.a += actual;
        a.b += actual;
        for (QPointF& pt : a.path)
            pt += actual;
    }
    m_selLast = global;
    emit changed();
}

void CaptureSession::beginSelectionResize(int handle, const QPoint& global) {
    if (!hasSelection() || m_hasResult)
        return;
    m_selResizing = true;
    m_selHandle = handle;
    m_selLast = global;
    m_selected = -1;
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::updateSelectionResize(const QPoint& global) {
    if (!m_selResizing)
        return;
    QRect r = selection();
    switch (m_selHandle) {
    case TopLeft: r.setTopLeft(global); break;
    case TopRight: r.setTopRight(global); break;
    case BottomLeft: r.setBottomLeft(global); break;
    case BottomRight: r.setBottomRight(global); break;
    case Top: r.setTop(global.y()); break;
    case Bottom: r.setBottom(global.y()); break;
    case Left: r.setLeft(global.x()); break;
    case Right: r.setRight(global.x()); break;
    default: break;
    }
    r = r.normalized();
    if (r.width() < 8) r.setWidth(8);
    if (r.height() < 8) r.setHeight(8);
    if (r.left() < m_desk.left()) r.setLeft(m_desk.left());
    if (r.top() < m_desk.top()) r.setTop(m_desk.top());
    if (r.right() > m_desk.right()) r.setRight(m_desk.right());
    if (r.bottom() > m_desk.bottom()) r.setBottom(m_desk.bottom());
    m_sel = r;
    emit changed();
}

void CaptureSession::nudgeSelection(const QPoint& delta) {
    if (!hasSelection() || m_hasResult)
        return;
    QRect s = selection().translated(delta);
    if (s.left() < m_desk.left()) s.moveLeft(m_desk.left());
    if (s.top() < m_desk.top()) s.moveTop(m_desk.top());
    if (s.right() > m_desk.right()) s.moveRight(m_desk.right());
    if (s.bottom() > m_desk.bottom()) s.moveBottom(m_desk.bottom());
    const QPoint actual = s.topLeft() - selection().topLeft();
    m_sel = s;
    for (Ann& a : m_anns) {
        a.rect.translate(actual);
        a.a += actual;
        a.b += actual;
        for (QPointF& pt : a.path)
            pt += actual;
    }
    emit changed();
}

void CaptureSession::setTool(Tool t) {
    if (m_tool == t)
        return;
    m_tool = t;
    if (t == Text)
        m_picker = false;
    emit changed();
}

void CaptureSession::setColor(const QColor& c) {
    m_color = c;
    if (m_selected >= 0 && m_selected < m_anns.size()) {
        m_anns[m_selected].color = c;
    }
    emit changed();
}

void CaptureSession::setWidth(qreal w) {
    m_width = qBound(1.0, w, 48.0);
    if (m_selected >= 0 && m_selected < m_anns.size()) {
        Ann& a = m_anns[m_selected];
        if (a.tool == Text)
            a.width = qMax(8.0, m_width * 6.0);
        else
            a.width = m_width;
    }
    emit changed();
}

void CaptureSession::setProcessing(bool on) {
    if (m_processing == on)
        return;
    m_processing = on;
    emit changed();
}

void CaptureSession::setResult(const QString& text, bool translation) {
    m_resultText = text;
    m_resultIsTranslation = translation;
    m_hasResult = true;
    m_processing = false;
    m_editing = -1;
    m_selected = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::clearResult() {
    if (!m_hasResult)
        return;
    m_hasResult = false;
    m_resultText.clear();
    m_processing = false;
    emit changed();
}

QRect CaptureSession::editingRect() const {
    if (!editingText())
        return QRect();
    return m_anns[m_editing].rect.toRect();
}

const CaptureSession::Ann* CaptureSession::selectedAnn() const {
    if (m_selected < 0 || m_selected >= m_anns.size())
        return nullptr;
    if (m_anns[m_selected].locked)
        return nullptr;
    return &m_anns[m_selected];
}

QRect CaptureSession::selectedRect() const {
    const Ann* a = selectedAnn();
    return a ? a->rect.toRect() : QRect();
}

qreal CaptureSession::selectedRotation() const {
    const Ann* a = selectedAnn();
    return a ? a->rotation : 0;
}

QRect CaptureSession::sourceRect(const QRect& global) const {
    return sourceRectFor(m_full, global);
}

QRect CaptureSession::sourceRectFor(const QImage& img, const QRect& global) const {
    const double sx = double(img.width()) / qMax(1, m_desk.width());
    const double sy = double(img.height()) / qMax(1, m_desk.height());
    return QRect(qRound((global.x() - m_desk.x()) * sx),
                 qRound((global.y() - m_desk.y()) * sy),
                 qRound(global.width() * sx), qRound(global.height() * sy));
}

QImage CaptureSession::displayImage() const {
    if (m_display.isNull()) {
        // 只缩放一次：选区和遮罩的屏幕绘制都基于这张逻辑分辨率图，
        // 避免每次鼠标移动都重采样 7680x2160 原图。
        m_display = m_full.scaled(m_desk.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (m_display.isNull())
            m_display = m_full;
    }
    return m_display;
}

QImage CaptureSession::dimmedImage() const {
    if (m_dimmed.isNull()) {
        m_dimmed = displayImage().copy();
        QPainter p(&m_dimmed);
        p.fillRect(m_dimmed.rect(), kMask);
        p.end();
    }
    return m_dimmed;
}

QImage CaptureSession::screenImage(QScreen* s, bool dimmed) const {
    const QRect sr = screenRect(s);
    auto& cache = dimmed ? m_screenDim : m_screenNormal;
    auto it = cache.constFind(s);
    if (it != cache.constEnd())
        return it.value();

    // Windows：直接用该屏原始物理图，叠加层 1:1 清晰显示。
    for (const ScreenShot& sh : m_shots) {
        if (sh.screen != s)
            continue;
        const double dpr = s->devicePixelRatio();
        const QSize sz(qRound(sr.width() * dpr), qRound(sr.height() * dpr));
        QImage out = sh.physical;
        if (out.size() != sz)
            out = out.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out = out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        out.setDevicePixelRatio(1.0);
        if (dimmed) {
            QPainter dp(&out);
            dp.fillRect(out.rect(), kMask);
            dp.end();
        }
        cache.insert(s, out);
        return out;
    }

    // 从原始高分辨率图切出本屏，缩放到 Qt 后备缓冲的物理像素尺寸；
    // 之后每帧都是 1:1 blit，不再做缩放。
    const QRect src = sourceRectFor(m_full, sr);
    QImage base = m_full.copy(src);
    const double dpr = s->devicePixelRatio();
    const QSize sz(qRound(sr.width() * dpr), qRound(sr.height() * dpr));
    QImage out = base.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (out.isNull())
        out = base;
    out.setDevicePixelRatio(1.0);
    if (dimmed) {
        QPainter dp(&out);
        dp.fillRect(out.rect(), kMask);
        dp.end();
    }
    cache.insert(s, out);
    return out;
}

QPointF CaptureSession::rotCenter(const Ann& a) const {
    return a.rect.center();
}

QPointF CaptureSession::rotatePoint(const QPointF& p, const QPointF& c, qreal deg) const {
    const qreal rad = qDegreesToRadians(deg);
    const qreal cs = std::cos(rad), sn = std::sin(rad);
    const qreal dx = p.x() - c.x(), dy = p.y() - c.y();
    return QPointF(c.x() + dx * cs - dy * sn, c.y() + dx * sn + dy * cs);
}

QRectF CaptureSession::rotatedBounds(const Ann& a) const {
    const QPointF c = rotCenter(a);
    const QPointF pts[4] = {
        rotatePoint(a.rect.topLeft(), c, a.rotation),
        rotatePoint(a.rect.topRight(), c, a.rotation),
        rotatePoint(a.rect.bottomLeft(), c, a.rotation),
        rotatePoint(a.rect.bottomRight(), c, a.rotation)
    };
    QRectF r(pts[0], pts[0]);
    for (int i = 1; i < 4; ++i)
        r = r.united(QRectF(pts[i], pts[i]));
    return r;
}

void CaptureSession::beginAnnotation(const QPoint& global) {
    if (!hasSelection())
        return;
    if (m_tool == Text) {
        beginText(global);
        return;
    }
    Ann a;
    a.tool = m_tool;
    a.a = global;
    a.b = global;
    a.rect = QRectF(global, QSizeF(0, 0));
    a.color = m_color;
    a.width = m_width;
    if (m_tool == Pen)
        a.path.append(global);
    m_anns.append(a);
    m_selected = m_anns.size() - 1;
    m_drawing = true;
    emit changed();
}

void CaptureSession::updateAnnotation(const QPoint& global) {
    if (!m_drawing || m_anns.isEmpty())
        return;
    Ann& a = m_anns.last();
    if (a.tool == Pen) {
        a.path.append(global);
        a.rect = a.path.boundingRect();
    } else {
        a.b = global;
        a.rect = QRectF(a.a, global).normalized();
    }
    emit changed();
}

void CaptureSession::finishAnnotation() {
    if (!m_drawing)
        return;
    m_drawing = false;
    if (!m_anns.isEmpty()) {
        Ann& a = m_anns.last();
        if (a.tool != Text && a.rect.width() < 2 && a.rect.height() < 2) {
            m_anns.removeLast();
            m_selected = -1;
        }
    }
    emit changed();
}

void CaptureSession::beginText(const QPoint& global) {
    if (!hasSelection())
        return;
    int idx = annotationAt(global);
    if (idx >= 0 && m_anns[idx].tool == Text) {
        m_selected = idx;
        m_editing = idx;
        emit changed();
        emit textEditingStarted();
        return;
    }
    Ann a;
    a.tool = Text;
    a.rect = QRectF(global, QSizeF(220, 48));
    a.a = a.rect.topLeft();
    a.b = a.rect.bottomRight();
    a.color = m_color;
    a.width = qMax(12.0, m_width * 6.0);
    m_anns.append(a);
    m_selected = m_anns.size() - 1;
    m_editing = m_selected;
    emit changed();
    emit textEditingStarted();
}

void CaptureSession::setEditingText(const QString& text) {
    if (!editingText())
        return;
    m_anns[m_editing].text = text;
    emit changed();
}

void CaptureSession::commitEditingText() {
    if (!editingText())
        return;
    if (m_anns[m_editing].text.trimmed().isEmpty()) {
        m_anns.removeAt(m_editing);
        m_selected = -1;
    }
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::cancelEditingText() {
    if (!editingText())
        return;
    if (m_anns[m_editing].text.trimmed().isEmpty()) {
        m_anns.removeAt(m_editing);
        m_selected = -1;
    }
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

int CaptureSession::annotationAt(const QPoint& global) const {
    for (int i = m_anns.size() - 1; i >= 0; --i) {
        const Ann& a = m_anns[i];
        if (a.locked)
            continue;
        const QPointF local = rotatePoint(global, rotCenter(a), -a.rotation);
        if (a.tool == Pen) {
            if (a.path.size() > 1) {
                const QRectF br = a.path.boundingRect().adjusted(-8, -8, 8, 8);
                if (br.contains(local))
                    return i;
            }
        } else if (a.rect.adjusted(-4, -4, 4, 4).contains(local)) {
            return i;
        }
    }
    return -1;
}

int CaptureSession::handleAt(const QPoint& global) const {
    int idx = m_selected;
    if (editingText())
        idx = m_editing;
    if (idx < 0 || idx >= m_anns.size())
        return NoHandle;
    const Ann& a = m_anns[idx];
    if (a.locked)
        return NoHandle;
    const QPointF c = rotCenter(a);
    const QPointF tl = rotatePoint(a.rect.topLeft(), c, a.rotation);
    const QPointF tr = rotatePoint(a.rect.topRight(), c, a.rotation);
    const QPointF bl = rotatePoint(a.rect.bottomLeft(), c, a.rotation);
    const QPointF br = rotatePoint(a.rect.bottomRight(), c, a.rotation);
    const QPointF t = rotatePoint(QPointF(c.x(), a.rect.top()), c, a.rotation);
    const QPointF b = rotatePoint(QPointF(c.x(), a.rect.bottom()), c, a.rotation);
    const QPointF l = rotatePoint(QPointF(a.rect.left(), c.y()), c, a.rotation);
    const QPointF r = rotatePoint(QPointF(a.rect.right(), c.y()), c, a.rotation);

    const qreal rad = qDegreesToRadians(a.rotation);
    const QPointF up(-std::sin(rad), -std::cos(rad));
    const QPointF right(std::cos(rad), std::sin(rad));
    const QPointF checkPos = tr + up * 16.0 + right * 16.0;
    const QPointF rotPos = br + right * 22.0 + up * -22.0;
    const QPointF delPos = bl - right * 18.0 + up * 18.0;

    auto near = [&](const QPointF& p) {
        return QLineF(global, p).length() < 16.0;
    };
    if (near(checkPos))
        return Check;
    if (near(rotPos))
        return Rotate;
    if (near(delPos))
        return Delete;
    if (near(tl)) return TopLeft;
    if (near(tr)) return TopRight;
    if (near(bl)) return BottomLeft;
    if (near(br)) return BottomRight;
    if (near(t)) return Top;
    if (near(b)) return Bottom;
    if (near(l)) return Left;
    if (near(r)) return Right;
    return NoHandle;
}

void CaptureSession::moveSelected(const QPoint& global, const QPoint& delta) {
    Q_UNUSED(global);
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    Ann& a = m_anns[m_selected];
    a.rect.translate(delta);
    a.a += delta;
    a.b += delta;
    for (QPointF& p : a.path)
        p += delta;
    emit changed();
}

void CaptureSession::resizeSelected(int handle, const QPoint& global) {
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    Ann& a = m_anns[m_selected];
    const QPointF c = rotCenter(a);
    const QPointF local = rotatePoint(global, c, -a.rotation);
    QRectF r = a.rect;
    switch (handle) {
    case TopLeft: r.setTopLeft(local); break;
    case TopRight: r.setTopRight(local); break;
    case BottomLeft: r.setBottomLeft(local); break;
    case BottomRight: r.setBottomRight(local); break;
    case Top: r.setTop(local.y()); break;
    case Bottom: r.setBottom(local.y()); break;
    case Left: r.setLeft(local.x()); break;
    case Right: r.setRight(local.x()); break;
    default: break;
    }
    a.rect = r.normalized();
    a.a = a.rect.topLeft();
    a.b = a.rect.bottomRight();
    emit changed();
}

void CaptureSession::beginRotate(const QPoint& global) {
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    const Ann& a = m_anns[m_selected];
    m_rotateStartAngle = angleDeg(rotCenter(a), global);
    m_rotateStartValue = a.rotation;
}

void CaptureSession::updateRotate(const QPoint& global) {
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    Ann& a = m_anns[m_selected];
    const qreal cur = angleDeg(rotCenter(a), global);
    a.rotation = m_rotateStartValue + (cur - m_rotateStartAngle);
    emit changed();
}

void CaptureSession::selectAnn(int index) {
    if (index < 0 || index >= m_anns.size())
        return;
    m_selected = index;
    m_editing = -1;
    emit changed();
}

void CaptureSession::deselect() {
    m_selected = -1;
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::undo() {
    if (editingText()) {
        cancelEditingText();
        return;
    }
    for (int i = m_anns.size() - 1; i >= 0; --i) {
        if (m_anns[i].locked)
            continue;
        m_anns.removeAt(i);
        m_selected = -1;
        emit changed();
        break;
    }
}

void CaptureSession::commitSelected() {
    if (editingText()) {
        const int idx = m_editing;
        const bool empty = m_anns[idx].text.trimmed().isEmpty();
        commitEditingText();
        if (empty || idx < 0 || idx >= m_anns.size())
            return;
        m_anns[idx].locked = true;
        m_selected = -1;
        emit changed();
        return;
    }
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    m_anns[m_selected].locked = true;
    m_selected = -1;
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::deleteSelected() {
    if (m_selected < 0 || m_selected >= m_anns.size())
        return;
    m_anns.removeAt(m_selected);
    m_selected = -1;
    m_editing = -1;
    emit textEditingFinished();
    emit changed();
}

void CaptureSession::setPicker(bool on) {
    m_picker = on;
    if (on) {
        m_tool = Move;
        m_editing = -1;
    }
    emit changed();
}

void CaptureSession::setPickerPos(const QPoint& global) {
    m_pickerPos = global;
    if (m_picker)
        emit changed();
}

QColor CaptureSession::colorAt(const QPoint& global) const {
    const QRect src = sourceRect(QRect(global, QSize(1, 1)));
    if (!m_full.rect().contains(src.topLeft()))
        return QColor();
    return m_full.pixelColor(src.topLeft());
}

void CaptureSession::copyColorAt(const QPoint& global) {
    const QColor c = colorAt(global);
    if (!c.isValid())
        return;
    QApplication::clipboard()->setText(c.name(QColor::HexRgb));
    qInfo() << "取色:" << c.name(QColor::HexRgb) << c.name(QColor::HexArgb) << c;
}

QImage CaptureSession::currentResult() const {
    if (!hasSelection())
        return QImage();
    const QRect sel = selection();

    // Windows：按各屏原始物理图合成，保留 4K/高 DPI 原始分辨率。
    if (!m_shots.isEmpty()) {
        qreal k = 1.0;
        for (const ScreenShot& sh : m_shots) {
            if (sh.logicalRect.intersects(sel))
                k = qMax(k, sh.scale);
        }
        QImage out(qRound(sel.width() * k), qRound(sel.height() * k),
                   QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::black);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (const ScreenShot& sh : m_shots) {
            const QRect inter = sel.intersected(sh.logicalRect);
            if (inter.isEmpty() || sh.physical.isNull())
                continue;
            const QRectF srcF((inter.x() - sh.logicalRect.x()) * sh.scale,
                              (inter.y() - sh.logicalRect.y()) * sh.scale,
                              inter.width() * sh.scale, inter.height() * sh.scale);
            const QRectF dstF((inter.x() - sel.x()) * k, (inter.y() - sel.y()) * k,
                              inter.width() * k, inter.height() * k);
            p.drawImage(dstF, sh.physical, srcF);
        }
        const double sx = double(out.width()) / qMax(1, sel.width());
        const double sy = double(out.height()) / qMax(1, sel.height());
        p.scale(sx, sy);
        p.translate(-sel.topLeft());
        for (const Ann& a : m_anns)
            drawAnn(p, a, false, m_full);
        p.end();
        out.setDevicePixelRatio(sx);
        return out;
    }

    QImage out = m_full.copy(sourceRect(sel));
    if (out.isNull())
        return out;
    const double sx = double(out.width()) / qMax(1, sel.width());
    const double sy = double(out.height()) / qMax(1, sel.height());
    QPainter p(&out);
    p.scale(sx, sy);
    p.translate(-sel.topLeft());
    for (const Ann& a : m_anns)
        drawAnn(p, a, false, m_full);
    p.end();
    out.setDevicePixelRatio(sx);
    return out;
}

void CaptureSession::drawAnn(QPainter& p, const Ann& a, bool editing, const QImage& source) const {
    const QPointF c = rotCenter(a);
    p.save();
    p.translate(c);
    p.rotate(a.rotation);
    p.translate(-c);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(a.color, qMax(1.0, a.width)));
    switch (a.tool) {
    case Move:
        break;
    case Rect:
        p.setBrush(Qt::NoBrush);
        p.drawRect(a.rect);
        break;
    case Ellipse:
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(a.rect);
        break;
    case Arrow: {
        const QPointF aa = a.a;
        const QPointF bb = a.b;
        p.drawLine(aa, bb);
        const QLineF line(aa, bb);
        const qreal ang = std::atan2(-line.dy(), line.dx());
        const qreal len = qMax(12.0, a.width * 5);
        const QPointF p1 = bb - QPointF(std::cos(ang - M_PI / 7) * len, -std::sin(ang - M_PI / 7) * len);
        const QPointF p2 = bb - QPointF(std::cos(ang + M_PI / 7) * len, -std::sin(ang + M_PI / 7) * len);
        p.drawLine(bb, p1);
        p.drawLine(bb, p2);
        break;
    }
    case Pen:
        if (a.path.size() > 1)
            p.drawPolyline(a.path.constData(), a.path.size());
        break;
    case Mosaic: {
        QImage sub = source.copy(sourceRectFor(source, a.rect.toRect()));
        if (!sub.isNull()) {
            const QImage small = sub.scaled(qMax(1, sub.width() / 10), qMax(1, sub.height() / 10),
                                           Qt::IgnoreAspectRatio, Qt::FastTransformation);
            p.drawImage(a.rect, small.scaled(sub.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
        }
        break;
    }
    case Text:
        if (!editing) {
            QFont f;
            f.setPixelSize(qMax(10, qRound(a.width)));
            p.setFont(f);
            p.setPen(a.color);
            p.drawText(a.rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, a.text);
        }
        break;
    }
    p.restore();
}

void CaptureSession::paint(QPainter& p, QScreen* screen, const QRect& dirtyLocal) const {
    if (m_full.isNull())
        return;
    const QRect sr = screenRect(screen);
    const QRect logicalScreen(QPoint(0, 0), sr.size());

    const QImage bg = screenImage(screen, true);
    if (!bg.isNull()) {
        QRect bgTarget = logicalScreen;
        if (!dirtyLocal.isEmpty())
            bgTarget = dirtyLocal.intersected(logicalScreen);
        if (!bgTarget.isEmpty()) {
            const double bx = double(bg.width()) / qMax(1, logicalScreen.width());
            const double by = double(bg.height()) / qMax(1, logicalScreen.height());
            const QRect bgSrc(qRound(bgTarget.x() * bx), qRound(bgTarget.y() * by),
                              qRound(bgTarget.width() * bx), qRound(bgTarget.height() * by));
            p.drawImage(bgTarget, bg, bgSrc);
        }
    }

    p.save();
    p.translate(-sr.topLeft());
    p.setClipRect(sr);

    const QImage normal = screenImage(screen, false);
    const double sx = double(normal.width()) / qMax(1, sr.width());
    const double sy = double(normal.height()) / qMax(1, sr.height());
    auto drawNormalPart = [&](const QRect& part) {
        if (part.isEmpty())
            return;
        const QRect src(qRound((part.x() - sr.x()) * sx),
                        qRound((part.y() - sr.y()) * sy),
                        qRound(part.width() * sx), qRound(part.height() * sy));
        p.drawImage(part, normal, src);
    };

    const QRect sel = selection();
    const QImage disp = displayImage();

    if (m_hasResult) {
        if (!sel.isEmpty()) {
            drawNormalPart(sel);
            p.fillRect(sel, QColor(0, 0, 0, 170));
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(kAccent, 2));
            p.drawRect(sel.adjusted(0, 0, -1, -1));
            const QRect panel = sel.adjusted(4, 4, -4, -4);
            QFont f;
            f.setPixelSize(qMax(12, qMin(22, int(sel.height() / 8))));
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(panel, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_resultText);
        }
        p.restore();
        return;
    }

    if (m_selecting || m_hasSel) {
        if (!sel.isEmpty())
            drawNormalPart(sel);
        for (const Ann& a : m_anns)
            drawAnn(p, a, a.tool == Text && m_editing >= 0 && &a == &m_anns[m_editing], disp);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(kAccent, 1));
        if (!sel.isEmpty()) {
            p.drawRect(sel.adjusted(0, 0, -1, -1));
            QFont f;
            f.setPixelSize(12);
            p.setFont(f);
            QRect r(sel.left(), sel.top() - 22, 160, 20);
            if (r.top() < m_desk.top())
                r.moveTop(sel.top() + 4);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                       QString("%1 × %2").arg(sel.width()).arg(sel.height()));
        }
    }
    p.restore();
}

void CaptureSession::paintSelectionHandles(QPainter& p, QScreen* screen) const {
    if (!hasSelection() || m_hasResult)
        return;
    const QRect sr = screenRect(screen);
    const QRect s = selection();
    p.save();
    p.translate(-sr.topLeft());
    p.setClipRect(sr);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(0, 120, 200), 1));
    p.setBrush(Qt::white);
    const QPoint pts[8] = {
        s.topLeft(), s.topRight(), s.bottomLeft(), s.bottomRight(),
        QPoint(s.center().x(), s.top()), QPoint(s.center().x(), s.bottom()),
        QPoint(s.left(), s.center().y()), QPoint(s.right(), s.center().y())
    };
    for (const QPoint& pt : pts)
        p.drawRect(QRect(pt - QPoint(4, 4), QSize(8, 8)));
    p.restore();
}

void CaptureSession::paintHandles(QPainter& p, QScreen* screen) const {
    int idx = editingText() ? m_editing : m_selected;
    if (idx < 0 || idx >= m_anns.size())
        return;
    const QRect sr = screenRect(screen);
    const Ann& a = m_anns[idx];
    if (a.locked)
        return;
    const QPointF c = rotCenter(a);
    p.save();
    p.translate(-sr.topLeft());
    p.setClipRect(sr);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kFrame, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.translate(c);
    p.rotate(a.rotation);
    p.translate(-c);
    p.drawRect(a.rect);

    const QPointF tl = a.rect.topLeft();
    const QPointF tr = a.rect.topRight();
    const QPointF bl = a.rect.bottomLeft();
    const QPointF br = a.rect.bottomRight();
    const QPointF t(c.x(), a.rect.top());
    const QPointF b(c.x(), a.rect.bottom());
    const QPointF l(a.rect.left(), c.y());
    const QPointF r(a.rect.right(), c.y());
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(0, 120, 200), 1));
    auto handle = [&](const QPointF& pt) { p.drawRect(QRectF(pt.x() - 4, pt.y() - 4, 8, 8)); };
    handle(tl); handle(tr); handle(bl); handle(br);
    handle(t); handle(b); handle(l); handle(r);

    // 旋转图标（右下角外侧）与确认勾（右上角外侧）
    const qreal rad = qDegreesToRadians(a.rotation);
    const QPointF right(std::cos(rad), std::sin(rad));
    const QPointF up(-std::sin(rad), -std::cos(rad));
    const QPointF rot = br + right * 22.0 - up * 22.0;
    const QPointF check = tr + right * 18.0 + up * 18.0;
    const QPointF del = bl - right * 18.0 + up * 18.0;
    p.setPen(QPen(QColor(0, 200, 120), 2));
    p.drawEllipse(check, 9, 9);
    p.drawText(QRectF(check.x() - 8, check.y() - 8, 16, 16), Qt::AlignCenter, QStringLiteral("✓"));
    p.setPen(QPen(kFrame, 2));
    p.drawEllipse(rot, 9, 9);
    p.drawText(QRectF(rot.x() - 8, rot.y() - 8, 16, 16), Qt::AlignCenter, QStringLiteral("↻"));
    p.setPen(QPen(QColor(255, 90, 90), 2));
    p.setBrush(QColor(0, 0, 0, 130));
    p.drawEllipse(del, 9, 9);
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(del + QPointF(-4, -4), del + QPointF(4, 4));
    p.drawLine(del + QPointF(-4, 4), del + QPointF(4, -4));
    p.restore();
}

void CaptureSession::requestAction(int action) {
    if (!hasSelection())
        return;
    emit actionRequested(action, currentResult(), selection());
}
