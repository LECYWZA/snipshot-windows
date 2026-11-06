#pragma once

#include <QObject>
#include <QImage>
#include <QPoint>
#include <QList>
#include <QWidget>
#include <QString>
#include <QSize>
#include <QStringList>

#include "ocr_win.h"

class QTextEdit;
class QLabel;
class Ai;

// 无边框置顶贴图窗（Snipaste 的"贴图"）
class PinWindow : public QWidget {
    Q_OBJECT
public:
    PinWindow(const QImage& img, const QPoint& pos, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QImage m_img;
    QSize m_baseLogical;
    qreal m_zoom = 1.0;
    QPoint m_dragOffset;
    bool m_dragging = false;
    QString m_savedPath;
    bool m_border = true;
};

// AI 结果贴图：先固定贴出原图并显示“翻译中/识别中”，完成后变成可编辑文本域
class TextPinWindow : public QWidget {
    Q_OBJECT
public:
    TextPinWindow(const QImage& img, const QRect& logicalRect, const QString& mode,
                  QWidget* parent = nullptr);
    ~TextPinWindow() override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void setResultText(const QString& text);
    void applyZoom();

    QImage m_img;
    QSize m_baseLogical;
    qreal m_zoom = 1.0;
    int m_baseFont = 14;
    QLabel* m_title = nullptr;
    QString m_mode;
    bool m_border = true;
    bool m_done = false;
    QTextEdit* m_edit = nullptr;
    Ai* m_ai = nullptr;
};

// 原位翻译贴图：把译文按 OCR 行坐标覆盖在原文位置上（不是堆在一边）
class TranslatePinWindow : public QWidget {
    Q_OBJECT
public:
    TranslatePinWindow(const QImage& img, const QRect& logicalRect, bool translateMode = true,
                       QWidget* parent = nullptr);

    void setSourceLines(const QList<OcrLine>& lines);   // OCR 完成：开始翻译/填充文本
    void applyTranslations(const QList<OcrLine>& lines, const QStringList& translations);
    void setMessage(const QString& text);
    void setBorderVisible(bool on) { m_border = on; updateWindowLayout(); }
    void setTargetLanguage(const QString& displayName);
    void showTextPanel(bool on) { setTextPanelVisible(on); }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool event(QEvent* e) override;

private:
    void startTranslate(const QString& target);
    void applyZoom();
    void layoutBar();
    void updateWindowLayout();
    void setShowOriginal(bool on);
    void setToolbarVisible(bool on);
    void setTextPanelVisible(bool on);
    void refreshTextPanel();
    void copyPrimaryText();
    void updateCanvas();
    QImage composeImage() const;
    QSize imageSize() const;
    int toolbarHeight() const;
    int panelWidth() const;
    QString allTranslations() const;
    QString allSources() const;

    QImage m_img;
    QList<OcrLine> m_lines;
    QStringList m_texts;
    QString m_message;
    QSize m_baseLogical;
    qreal m_zoom = 1.0;
    QPoint m_dragOffset;
    bool m_dragging = false;
    bool m_border = true;
    bool m_showOriginal = false;
    bool m_translateMode = true;
    bool m_showToolbar = true;    QString m_target;
    QWidget* m_bar = nullptr;
    class QComboBox* m_lang = nullptr;
    class QPushButton* m_toggle = nullptr;
    class QWidget* m_panel = nullptr;
    class QTextEdit* m_textEdit = nullptr;
    class QLabel* m_canvas = nullptr;
    bool m_panelVisible = false;
    static constexpr int kBarHeight = 30;
    static constexpr int kPanelWidth = 360;
};

// 管理所有贴图窗：F3 一键隐藏/显示
class PinManager : public QObject {
    Q_OBJECT
public:
    static PinManager* instance();
    void addPin(const QImage& img);                   // 居中贴出
    void addPin(const QImage& img, const QPoint& pos); // 在逻辑坐标 pos 贴出
    void registerPin(QWidget* w);                     // 托管已有贴图窗（如文本结果贴图）
    void toggleAll();
    void closeAll();
    int count() const { return m_pins.size(); }

private:
    QList<QWidget*> m_pins;
};
