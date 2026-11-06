#pragma once

#include <QObject>
#include <QString>

// 使用 GNOME Shell 原生 Screencast D-Bus 录屏（VP8，全屏/全桌面，性能好）
class ScreenRecorder : public QObject {
    Q_OBJECT
public:
    explicit ScreenRecorder(QObject* parent = nullptr);

    bool isRecording() const { return m_recording; }
    QString currentFile() const { return m_file; }
    QString lastFile() const { return m_lastFile; }

    bool start();
    void stop();

signals:
    void started(const QString& file);
    void finished(const QString& file);
    void error(const QString& message);

private:
    static QString newFilePath();
    bool m_recording = false;
    QString m_file;
    QString m_lastFile;
    class QProcess* m_proc = nullptr;
    bool m_stopping = false;
};
