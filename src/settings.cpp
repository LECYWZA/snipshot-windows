#include "settings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>

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
    return st;
}

void Settings::save() const {
    QSettings s;
    s.setValue("api/base", apiBase);
    s.setValue("api/key", apiKey);
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
}
