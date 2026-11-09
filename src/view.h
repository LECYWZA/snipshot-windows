#pragma once

#include <QWidget>
#include <QPointer>
#include <QPoint>

class QScreen;
class QTextEdit;
class CaptureSession;

// 单屏全屏视图：只负责显示 CaptureSession 的一部分 + 处理本屏鼠标/键盘。
class OverlayView : public QWidget {
    Q_OBJECT
public:
    OverlayView(CaptureSession* session, QScreen* screen, QWidget* parent = nullptr);
    ~OverlayView() override;

    QScreen* screen() const { return m_screen; }
    void setActive(bool on);
    bool isActive() const { return m_active; }

signals:
    void activated(OverlayView* self);
    void cancelRequested();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void onSessionChanged();
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QPoint toGlobal(const QPoint& local) const;
    QRect toolbarRect(bool tools) const;
    QRect toolbarRectFor(const QRect& selection, bool tools) const;
    int toolbarHit(const QPoint& local, bool tools) const;
    void toolbarClick(int idx, bool tools);
    void drawToolbar(QPainter& p, bool tools);
    void drawPicker(QPainter& p);
    void drawResultHint(QPainter& p);
    void showColorMenu();
    void syncEditor();
    void commitEditor();
    void destroyEditor();
    void updateCursorAt(const QPoint& global);
    void updateHover(const QPoint& local);

    CaptureSession* m_session = nullptr;
    QScreen* m_screen = nullptr;
    bool m_active = false;
    QPointer<QTextEdit> m_editor;
    bool m_moving = false;
    bool m_resizing = false;
    bool m_rotating = false;
    bool m_selMoving = false;
    bool m_selResizing = false;
    int m_selHandle = 0;
    int m_handle = 0;
    int m_hoverToolIdx = -1;
    int m_hoverActionIdx = -1;
    int m_pressedToolIdx = -1;
    int m_pressedActionIdx = -1;
    QPoint m_lastGlobal;
    QRect m_prevSelection;
    QString m_prevState;
};
