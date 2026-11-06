#pragma once

#include <QDialog>
#include <QLineEdit>
#include "settings.h"

class QCheckBox;
class QComboBox;
class QSpinBox;

// 点击后直接按键盘组合录入，不接受手动打字
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget* parent = nullptr);
    void setBinding(const QString& text);
    QString binding() const { return m_binding; }
    void startCapture();

signals:
    void captured(const QString& text);

protected:
    void focusInEvent(QFocusEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    void stopCapture(bool restore);
    static QString keyName(int key, const QString& text);

    QString m_binding;
    QString m_old;
    bool m_capturing = false;
};

// 设置界面：OpenAI API、保存目录、快捷键、剪贴板、录屏
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void browseDir();
    void browseRecordDir();
    void applyAndClose();
    void registerHotkeys();

private:
    void checkHotkey(HotkeyEdit* edit, const QString& text);
    void persist();

    QLineEdit* m_base;
    QLineEdit* m_key;
    QLineEdit* m_model;
    QComboBox* m_translateTarget = nullptr;
    QLineEdit* m_dir;
    QLineEdit* m_recordDir;
    HotkeyEdit* m_hkSnip;
    HotkeyEdit* m_hkPin;
    HotkeyEdit* m_hkToggle;
    HotkeyEdit* m_hkTranslate;
    HotkeyEdit* m_hkOcr;
    HotkeyEdit* m_hkClipboard;
    HotkeyEdit* m_hkRecord;
    QCheckBox* m_hkSnipEnabled;
    QCheckBox* m_hkPinEnabled;
    QCheckBox* m_hkToggleEnabled;
    QCheckBox* m_hkTranslateEnabled;
    QCheckBox* m_hkOcrEnabled;
    QCheckBox* m_hkClipboardEnabled;
    QCheckBox* m_hkRecordEnabled;
    QCheckBox* m_pinBorder;
    QCheckBox* m_clipEnabled;
    QSpinBox* m_clipMax;
    QCheckBox* m_recEnabled;
    QSpinBox* m_recFps;
    QCheckBox* m_recCursor;
    QCheckBox* m_autoStart = nullptr;
    QLineEdit* m_ffmpeg = nullptr;
};
