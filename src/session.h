#pragma once

#include <QObject>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QColor>
#include <QString>
#include <QList>
#include <QFont>
#include <QHash>

#include "capture.h"

class QScreen;

// 一次截图会话：整张桌面图 + 全局选区 + 标注 + 结果。
// 两块屏各有一个 OverlayView，只是这个会话的两个视图；
// 选区、标注、工具栏都按“整张桌面”坐标工作，因此可以从一屏框到另一屏。
class CaptureSession : public QObject {
    Q_OBJECT
public:
    enum Mode { Snip, Translate, Ocr };
    enum Tool { Move = 0, Rect, Ellipse, Arrow, Pen, Mosaic, Text };
    enum Handle { NoHandle, TopLeft, TopRight, BottomLeft, BottomRight,
                  Top, Bottom, Left, Right, Rotate, Check, Delete };

    struct Ann {
        Tool tool = Rect;
        QRectF rect;          // 未旋转的全局矩形
        QPointF a, b;         // 箭头起点/终点（全局）
        QPolygonF path;       // 画笔路径（全局）
        QColor color = QColor(255, 60, 60);
        qreal width = 3;
        QString text;
        qreal rotation = 0;   // 度
        bool locked = false;  // 打勾确认后固化，不再参与选中/编辑
    };

    explicit CaptureSession(const QImage& fullImage, const QList<ScreenShot>& shots,
                            Mode mode, QObject* parent = nullptr);

    const QImage& fullImage() const { return m_full; }
    QRect desk() const { return m_desk; }
    Mode mode() const { return m_mode; }
    QRect screenRect(QScreen* s) const;
    QRect activeScreenRect() const;

    bool selecting() const { return m_selecting; }
    bool drawing() const { return m_drawing; }
    bool selectionMoving() const { return m_selMoving; }
    bool selectionResizing() const { return m_selResizing; }
    bool hasSelection() const;
    QRect selection() const { return m_sel.normalized(); }
    void beginSelection(const QPoint& global);
    void updateSelection(const QPoint& global);
    void endSelection();
    void cancelSelection();
    int selectionHandleAt(const QPoint& global) const;
    void beginSelectionMove(const QPoint& global);
    void updateSelectionMove(const QPoint& global);
    void beginSelectionResize(int handle, const QPoint& global);
    void updateSelectionResize(const QPoint& global);
    void nudgeSelection(const QPoint& delta);

    Tool tool() const { return m_tool; }
    void setTool(Tool t);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);
    qreal width() const { return m_width; }
    void setWidth(qreal w);

    bool processing() const { return m_processing; }
    void setProcessing(bool on);
    QString resultText() const { return m_resultText; }
    bool resultIsTranslation() const { return m_resultIsTranslation; }
    bool hasResult() const { return m_hasResult; }
    void setResult(const QString& text, bool translation);
    void clearResult();

    bool editingText() const { return m_editing >= 0 && m_editing < m_anns.size(); }
    int editingIndex() const { return m_editing; }
    QRect editingRect() const;
    const Ann* selectedAnn() const;
    QRect selectedRect() const;
    qreal selectedRotation() const;
    int handleAt(const QPoint& global) const;
    int annotationAt(const QPoint& global) const;

    bool pickerActive() const { return m_picker; }
    void setPicker(bool on);
    QPoint pickerPos() const { return m_pickerPos; }
    void setPickerPos(const QPoint& global);
    QColor colorAt(const QPoint& global) const;
    void copyColorAt(const QPoint& global);

    void beginAnnotation(const QPoint& global);
    void updateAnnotation(const QPoint& global);
    void finishAnnotation();
    void beginText(const QPoint& global);
    void setEditingText(const QString& text);
    void commitEditingText();
    void cancelEditingText();
    void moveSelected(const QPoint& global, const QPoint& delta);
    void resizeSelected(int handle, const QPoint& global);
    void beginRotate(const QPoint& global);
    void updateRotate(const QPoint& global);
    void selectAnn(int index);
    void deselect();
    void undo();
    void commitSelected();   // 打勾：固化当前标注
    void deleteSelected();   // 删除当前选中的标注

    QImage currentResult() const;   // 选区 + 标注
    QRect selectionRect() const { return selection(); }
    void requestAction(int action);

    void paint(QPainter& p, QScreen* screen, const QRect& dirtyLocal = QRect()) const;
    void paintHandles(QPainter& p, QScreen* screen) const;
    void paintSelectionHandles(QPainter& p, QScreen* screen) const;
    void paintResultHint(QPainter& p, QScreen* screen) const;

signals:
    void changed();
    void selectionChanged(const QImage& result, const QRect& logicalRect);
    void actionRequested(int action, const QImage& result, const QRect& logicalRect);
    void aiRequested(const QImage& result, const QRect& rect, int mode);
    void cancelRequested();
    void textEditingStarted();
    void textEditingFinished();

private:
    QRect sourceRect(const QRect& global) const;
    QRect sourceRectFor(const QImage& img, const QRect& global) const;
    QImage displayImage() const;
    QImage dimmedImage() const;
    QImage screenImage(QScreen* s, bool dimmed) const;
    QPointF rotCenter(const Ann& a) const;
    QPointF rotatePoint(const QPointF& p, const QPointF& c, qreal deg) const;
    QRectF rotatedBounds(const Ann& a) const;
    void applyColorWidth();
    void drawAnn(QPainter& p, const Ann& a, bool editing, const QImage& source) const;

    QImage m_full;
    QList<ScreenShot> m_shots;   // 逐屏原始分辨率图（高分辨率输出用）
    QRect m_desk;
    Mode m_mode = Snip;
    QRect m_sel;
    bool m_selecting = false;
    bool m_hasSel = false;
    QPoint m_start;

    Tool m_tool = Rect;
    QColor m_color = QColor(255, 60, 60);
    qreal m_width = 3;
    QList<Ann> m_anns;
    int m_selected = -1;
    int m_editing = -1;
    bool m_drawing = false;
    int m_resizeHandle = NoHandle;
    QPoint m_lastDrag;
    qreal m_rotateStartAngle = 0;
    qreal m_rotateStartValue = 0;

    bool m_processing = false;
    bool m_hasResult = false;
    QString m_resultText;
    bool m_resultIsTranslation = false;

    bool m_picker = false;
    QPoint m_pickerPos;

    bool m_selMoving = false;
    bool m_selResizing = false;
    int m_selHandle = NoHandle;
    QPoint m_selLast;

    mutable QImage m_display;
    mutable QImage m_dimmed;
    mutable QHash<QScreen*, QImage> m_screenNormal;
    mutable QHash<QScreen*, QImage> m_screenDim;
};
