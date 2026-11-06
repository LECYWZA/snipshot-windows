#pragma once

#include <QDialog>
#include "settings.h"

class QLineEdit;
class QCheckBox;
class QSpinBox;
class QComboBox;

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
    QLineEdit* m_base;
    QLineEdit* m_key;
    QLineEdit* m_model;
    QComboBox* m_translateTarget = nullptr;
    QLineEdit* m_dir;
    QLineEdit* m_recordDir;
    QLineEdit* m_hkSnip;
    QLineEdit* m_hkPin;
    QLineEdit* m_hkToggle;
    QLineEdit* m_hkTranslate;
    QLineEdit* m_hkOcr;
    QLineEdit* m_hkClipboard;
    QLineEdit* m_hkRecord;
    QCheckBox* m_pinBorder;
    QCheckBox* m_clipEnabled;
    QSpinBox* m_clipMax;
    QCheckBox* m_recEnabled;
    QSpinBox* m_recFps;
    QCheckBox* m_recCursor;
    QLineEdit* m_ffmpeg = nullptr;
};
