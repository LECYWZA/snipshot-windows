#pragma once

#include <QString>

// 配置项：OpenAI 兼容 API + 保存目录 + 快捷键
struct Settings {
    // 示例配置（OpenAI 兼容接口）。请在“设置”里改成你自己的地址/密钥/模型。
    QString apiBase  = "https://api.openai.com/v1";
    QString apiKey;                                   // 留空，设置里填写
    QString model    = "gpt-4o-mini";
    QString translateTarget = QStringLiteral("简体中文");   // 默认翻译目标语言
    QString saveDir;  // 默认 ~/图片/截图
    // 快捷键（仅用于展示/Windows 注册；Linux Wayland 走 GNOME 自定义快捷键）
#ifdef Q_OS_WIN
    // Windows 上 F1/F3/Alt+Q/Alt+W 常被 Snipaste、Quicker 等占用，默认改用 Alt+Shift 组合
    QString hkSnip       = "Ctrl+Alt+S";
    QString hkPin        = "Alt+Shift+D";
    QString hkTogglePins = "Alt+Shift+H";
    QString hkTranslate  = "Alt+Shift+Q";
    QString hkOcr        = "Alt+Shift+W";
#else
    QString hkSnip       = "F1";
    QString hkPin        = "F2";
    QString hkTogglePins = "F3";
    QString hkTranslate  = "Alt+q";
    QString hkOcr        = "Alt+w";
#endif
    bool hkSnipEnabled = true;
    bool hkPinEnabled = true;
    bool hkTogglePinsEnabled = true;
    bool hkTranslateEnabled = true;
    bool hkOcrEnabled = true;
    bool hkClipboardEnabled = true;
    bool hkRecordEnabled = true;
    bool autoStart = false;        // 开机自启（当前用户 shell:startup 快捷方式）
    bool pinBorder       = true;   // 新贴图默认是否显示边框

    // 剪贴板管理器
    bool clipboardEnabled = true;
    int  clipboardMax     = 100;
#ifdef Q_OS_WIN
    QString hkClipboard   = "Alt+Shift+V";
#else
    QString hkClipboard   = "Alt+v";
#endif

    // 屏幕录制
    bool recordingEnabled = true;
    int  recordFps        = 30;
    bool recordCursor     = true;
    QString recordDir;             // 默认 ~/视频/录屏
#ifdef Q_OS_WIN
    QString hkRecord      = "Alt+Shift+R";
#else
    QString hkRecord      = "Alt+r";
#endif
    QString ffmpegPath;            // Windows：ffmpeg.exe 路径（留空自动查找）

    static Settings load();
    void save() const;
    static QString defaultSaveDir();
    static QString defaultRecordDir();
    // Windows：按设置创建/删除 shell:startup 快捷方式
    static bool applyAutoStart(bool on, QString* error = nullptr);
    static bool autoStartPresent();
};
