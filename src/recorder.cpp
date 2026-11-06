#include "recorder.h"
#include "settings.h"

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  include <QCoreApplication>
#  include <QProcess>
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <QDBusConnection>
#  include <QDBusMessage>
#  include <QDBusPendingCallWatcher>
#  include <QDBusPendingReply>
#  include <QDBusArgument>
#  include <QVariantMap>
#endif

ScreenRecorder::ScreenRecorder(QObject* parent) : QObject(parent) {}

#ifdef Q_OS_WIN

static QString findFfmpeg(const Settings& st) {
    QStringList candidates;
    if (!st.ffmpegPath.trimmed().isEmpty())
        candidates << st.ffmpegPath.trimmed();
    candidates << QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    const QString onPath = QStandardPaths::findExecutable("ffmpeg");
    if (!onPath.isEmpty())
        candidates << onPath;
    candidates << "E:/Tools/ffmpeg/ffmpeg.exe";
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QString();
}

QString ScreenRecorder::newFilePath() {
    Settings st = Settings::load();
    QString dir = st.recordDir;
    if (dir.isEmpty())
        dir = Settings::defaultRecordDir();
    QDir().mkpath(dir);
    return dir + "/SnipShot_" +
           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4";
}

bool ScreenRecorder::start() {
    if (m_recording)
        return true;
    const Settings st = Settings::load();
    const QString ffmpeg = findFfmpeg(st);
    if (ffmpeg.isEmpty()) {
        emit error(QStringLiteral("找不到 ffmpeg.exe：请放到程序目录，或在设置里指定路径"));
        return false;
    }

    const QString file = newFilePath();
    QStringList args{
        "-hide_banner", "-loglevel", "warning", "-y",
        "-f", "gdigrab",
        "-framerate", QString::number(qBound(5, st.recordFps, 60)),
        "-draw_mouse", st.recordCursor ? "1" : "0",
        "-i", "desktop",
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-crf", "20",
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
        file
    };

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromLocal8Bit(m_proc->readAll()).trimmed();
        if (!out.isEmpty())
            qInfo() << "[ffmpeg]" << out;
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool wasStopping = m_stopping;
        m_recording = false;
        m_stopping = false;
        if (m_proc)
            m_proc->deleteLater();
        m_proc = nullptr;
        if (wasStopping) {
            m_lastFile = m_file;
            qInfo() << "录屏结束:" << m_lastFile;
            emit finished(m_lastFile);
        } else if (status == QProcess::CrashExit || code != 0) {
            emit error(QStringLiteral("录屏进程意外退出（代码 %1）").arg(code));
        } else {
            m_lastFile = m_file;
            emit finished(m_lastFile);
        }
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_recording = false;
            emit error(QStringLiteral("ffmpeg 启动失败"));
        }
    });

    m_proc->start(ffmpeg, args);
    if (!m_proc->waitForStarted(4000)) {
        emit error(QStringLiteral("ffmpeg 启动超时"));
        m_proc->deleteLater();
        m_proc = nullptr;
        return false;
    }

    // 录屏时不让系统息屏/睡眠
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);

    m_recording = true;
    m_file = file;
    qInfo() << "录屏开始:" << file << "ffmpeg:" << ffmpeg;
    emit started(file);
    return true;
}

void ScreenRecorder::stop() {
    if (!m_recording || !m_proc)
        return;
    m_stopping = true;
    // ffmpeg 交互命令：q 正常收尾并写 moov，避免 mp4 损坏
    m_proc->write("q\n");
    m_proc->closeWriteChannel();
    if (!m_proc->waitForFinished(8000)) {
        qWarning() << "ffmpeg 未在 8 秒内退出，强制结束";
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    SetThreadExecutionState(ES_CONTINUOUS);
}

#else  // ---------------- Linux (GNOME Shell) ----------------

QString ScreenRecorder::newFilePath() {
    Settings st = Settings::load();
    QString dir = st.recordDir;
    if (dir.isEmpty())
        dir = Settings::defaultRecordDir();
    QDir().mkpath(dir);
    return dir + "/SnipShot_" +
           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".webm";
}

bool ScreenRecorder::start() {
    if (m_recording)
        return true;
    const Settings st = Settings::load();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit error(QStringLiteral("D-Bus 会话不可用，无法录屏"));
        return false;
    }
    const QString file = newFilePath();
    QVariantMap opts;
    opts.insert("framerate", qBound(5, st.recordFps, 60));
    opts.insert("draw-cursor", st.recordCursor);

    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.gnome.Shell.Screencast",
        "/org/gnome/Shell/Screencast",
        "org.gnome.Shell.Screencast",
        "Screencast");
    msg << file << opts;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, file]() {
        QDBusPendingReply<bool, QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            emit error(QStringLiteral("录屏启动失败: ") + reply.error().message());
            return;
        }
        const bool ok = reply.argumentAt<0>();
        const QString used = reply.argumentAt<1>();
        if (!ok) {
            emit error(QStringLiteral("GNOME Shell 拒绝了录屏请求"));
            return;
        }
        m_recording = true;
        m_file = used.isEmpty() ? file : used;
        qInfo() << "录屏开始:" << m_file;
        emit started(m_file);
    });
    return true;
}

void ScreenRecorder::stop() {
    if (!m_recording)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.gnome.Shell.Screencast",
        "/org/gnome/Shell/Screencast",
        "org.gnome.Shell.Screencast",
        "StopScreencast");
    m_recording = false;
    const QString file = m_file;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, file]() {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            qWarning() << "StopScreencast:" << reply.error().message();
        m_lastFile = file;
        qInfo() << "录屏结束:" << m_lastFile;
        emit finished(m_lastFile);
    });
}

#endif
