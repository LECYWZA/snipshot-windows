#include "settings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>

QString Settings::defaultSaveDir() {
    QString pic = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pic.isEmpty())
        pic = QDir::homePath() + "/图片";
    return pic + "/截图";
}

QString Settings::defaultRecordDir() {
    QString vid = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (vid.isEmpty())
        vid = QDir::homePath() + "/视频";
    return vid + "/录屏";
}

Settings Settings::load() {
    QSettings s;
    Settings st;
    st.apiBase  = s.value("api/base", st.apiBase).toString();
    st.apiKey   = s.value("api/key", st.apiKey).toString();
    st.model    = s.value("api/model", st.model).toString();
    st.translateTarget = s.value("translate/target", st.translateTarget).toString();
    st.saveDir  = s.value("save/dir", defaultSaveDir()).toString();
    st.hkSnip       = s.value("hotkey/snip", st.hkSnip).toString();
    st.hkPin        = s.value("hotkey/pin", st.hkPin).toString();
    st.hkTogglePins = s.value("hotkey/togglePins", st.hkTogglePins).toString();
    st.hkTranslate  = s.value("hotkey/translate", st.hkTranslate).toString();
    st.hkOcr        = s.value("hotkey/ocr", st.hkOcr).toString();
    st.pinBorder    = s.value("pin/border", st.pinBorder).toBool();
    st.clipboardEnabled = s.value("clipboard/enabled", st.clipboardEnabled).toBool();
    st.clipboardMax     = s.value("clipboard/max", st.clipboardMax).toInt();
    st.hkClipboard      = s.value("hotkey/clipboard", st.hkClipboard).toString();
    st.recordingEnabled = s.value("record/enabled", st.recordingEnabled).toBool();
    st.recordFps        = s.value("record/fps", st.recordFps).toInt();
    st.recordCursor     = s.value("record/cursor", st.recordCursor).toBool();
    st.recordDir        = s.value("record/dir", defaultRecordDir()).toString();
    st.hkRecord         = s.value("hotkey/record", st.hkRecord).toString();
    st.ffmpegPath       = s.value("record/ffmpeg", st.ffmpegPath).toString();
    st.hkSnipEnabled = s.value("hotkey/snipEnabled", st.hkSnipEnabled).toBool();
    st.hkPinEnabled = s.value("hotkey/pinEnabled", st.hkPinEnabled).toBool();
    st.hkTogglePinsEnabled = s.value("hotkey/togglePinsEnabled", st.hkTogglePinsEnabled).toBool();
    st.hkTranslateEnabled = s.value("hotkey/translateEnabled", st.hkTranslateEnabled).toBool();
    st.hkOcrEnabled = s.value("hotkey/ocrEnabled", st.hkOcrEnabled).toBool();
    st.hkClipboardEnabled = s.value("hotkey/clipboardEnabled", st.hkClipboardEnabled).toBool();
    st.hkRecordEnabled = s.value("hotkey/recordEnabled", st.hkRecordEnabled).toBool();
    st.autoStart = s.value("general/autoStart", st.autoStart).toBool();
    return st;
}

void Settings::save() const {
    QSettings s;
    s.setValue("api/base", apiBase);    s.setValue("api/key", apiKey);
    s.setValue("api/model", model);
    s.setValue("translate/target", translateTarget);
    s.setValue("save/dir", saveDir);
    s.setValue("hotkey/snip", hkSnip);
    s.setValue("hotkey/pin", hkPin);
    s.setValue("hotkey/togglePins", hkTogglePins);
    s.setValue("hotkey/translate", hkTranslate);
    s.setValue("hotkey/ocr", hkOcr);
    s.setValue("pin/border", pinBorder);
    s.setValue("clipboard/enabled", clipboardEnabled);
    s.setValue("clipboard/max", clipboardMax);
    s.setValue("hotkey/clipboard", hkClipboard);
    s.setValue("record/enabled", recordingEnabled);
    s.setValue("record/fps", recordFps);
    s.setValue("record/cursor", recordCursor);
    s.setValue("record/dir", recordDir);
    s.setValue("hotkey/record", hkRecord);
    s.setValue("record/ffmpeg", ffmpegPath);
    s.setValue("hotkey/snipEnabled", hkSnipEnabled);
    s.setValue("hotkey/pinEnabled", hkPinEnabled);
    s.setValue("hotkey/togglePinsEnabled", hkTogglePinsEnabled);
    s.setValue("hotkey/translateEnabled", hkTranslateEnabled);
    s.setValue("hotkey/ocrEnabled", hkOcrEnabled);
    s.setValue("hotkey/clipboardEnabled", hkClipboardEnabled);
    s.setValue("hotkey/recordEnabled", hkRecordEnabled);
    s.setValue("general/autoStart", autoStart);
}

static QString autoStartLink() {
    const QString base = QString::fromLocal8Bit(qgetenv("APPDATA"));
    return base + QStringLiteral("/Microsoft/Windows/Start Menu/Programs/Startup/SnipShot.lnk");
}

bool Settings::autoStartPresent() {
    return QFileInfo::exists(autoStartLink());
}

bool Settings::applyAutoStart(bool on, QString* error) {
#ifdef Q_OS_WIN
    const QString link = QDir::toNativeSeparators(autoStartLink());
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString workDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    auto escape = [](QString s) { return s.replace('\'', QStringLiteral("''")); };

    QString script;
    if (on) {
        QDir().mkpath(QFileInfo(link).absolutePath());
        script = QStringLiteral(
                     "$ws = New-Object -ComObject WScript.Shell; "
                     "$sc = $ws.CreateShortcut('%1'); "
                     "$sc.TargetPath = '%2'; "
                     "$sc.WorkingDirectory = '%3'; "
                     "$sc.IconLocation = '%2,0'; "
                     "$sc.Save()")
                     .arg(escape(link), escape(exe), escape(workDir));
    } else {
        script = QStringLiteral("Remove-Item -LiteralPath '%1' -Force -ErrorAction SilentlyContinue")
                     .arg(escape(link));
    }

    QProcess p;
    p.start(QStringLiteral("powershell"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
             QStringLiteral("-Command"), script});
    if (!p.waitForFinished(15000)) {
        p.kill();
        if (error)
            *error = QStringLiteral("PowerShell 超时");
        return false;
    }
    if (p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return false;
    }
    return QFileInfo::exists(link) == on;
#else
    Q_UNUSED(on);
    Q_UNUSED(error);
    return true;
#endif
}
