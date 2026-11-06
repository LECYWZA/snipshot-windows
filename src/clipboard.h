#pragma once

#include <QObject>
#include <QList>
#include <QDateTime>
#include <QString>
#include <QPointer>
#include <QImage>
#include <QSet>

class QProcess;
class QFileSystemWatcher;
class QTimer;
class ClipboardWindow;

struct ClipItem {
    enum Kind { Text, Image };
    Kind kind = Text;
    QString text;
    QString imagePath;
    QDateTime time;
    bool pinned = false;
    QString hash;
};

// 剪贴板历史：后台捕获（wl-paste / data-control），可搜索、固定、复制、删除、持久化
class ClipboardManager : public QObject {
    Q_OBJECT
public:
    explicit ClipboardManager(QObject* parent = nullptr);
    ~ClipboardManager() override;

    void start();
    void stop();
    bool isRunning() const { return m_running; }
    void reloadSettings();

    void openWindow();
    const QList<ClipItem>& items() const { return m_items; }
    void copyItem(int index);
    void togglePinned(int index);
    void removeItem(int index);
    void updateText(int index, const QString& text);
    void clearUnpinned();
    void refreshWindow();

    // watcher 子进程调用：--clip-file=text:/tmp/xxx
    void addFile(const QString& type, const QString& path);

signals:
    void changed();
    void error(const QString& message);

private:
    void startWatchers();
    void stopWatchers();
    void startRegistrySync();
    void stopRegistrySync();
    void syncRegistry();
    QString extensionCacheDir() const;
    QString extensionRegistryPath() const;
    bool extensionInstalled() const;
    void addText(const QString& text, bool pinned = false);
    void addImage(const QImage& img, bool pinned = false);
    void load();
    void save();
    QString storeDir() const;
    void scheduleSave();
    void enforceLimit();
    void deleteImageFile(const ClipItem& it);

    QList<ClipItem> m_items;
    QProcess* m_textWatch = nullptr;
    QProcess* m_imageWatch = nullptr;
    QFileSystemWatcher* m_watch = nullptr;
    QTimer* m_poll = nullptr;
    QString m_registryPath;
    QSet<QString> m_hidden;
    QSet<QString> m_extLoaded;   // 已处理过的扩展图片，避免每 1.5s 重复解码
    QPointer<ClipboardWindow> m_window;
    bool m_running = false;
    bool m_savePending = false;
    bool m_syncing = false;
};
