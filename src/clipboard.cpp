#include "clipboard.h"
#include "settings.h"

#include <QApplication>
#include <QClipboard>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QBuffer>
#include <QTimer>
#include <QDebug>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTabBar>
#include <QMenu>
#include <QInputDialog>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QWindow>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

// Windows 前台锁经常导致 SetForegroundWindow 失败（窗口显示但不接收键盘）。
// 用 AttachThreadInput + Alt 解锁，确保剪贴板窗口真正拿到焦点。
static void forceForeground(QWidget* w) {
    if (!w)
        return;
    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD myThread = GetCurrentThreadId();
    if (fgThread && fgThread != myThread)
        AttachThreadInput(fgThread, myThread, TRUE);
    keybd_event(VK_MENU, 0, 0, 0);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    if (fgThread && fgThread != myThread)
        AttachThreadInput(fgThread, myThread, FALSE);
    w->activateWindow();
    w->raise();
}
#endif
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QPixmap>
#include <QKeyEvent>
#include <QFileSystemWatcher>
#include <QJsonParseError>
#include <QMimeData>

// ---------------- 历史窗口 ----------------

class ClipItemDelegate : public QStyledItemDelegate {
public:
    explicit ClipItemDelegate(ClipboardManager* mgr, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_mgr(mgr) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(0, 58);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const int i = idx.data(Qt::UserRole).toInt();
        const auto& items = m_mgr->items();
        if (i < 0 || i >= items.size())
            return;
        const ClipItem& c = items[i];
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        QRect r = opt.rect.adjusted(4, 3, -4, -3);
        if (opt.state & QStyle::State_Selected) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(43, 58, 74));
            p->drawRoundedRect(r, 6, 6);
        } else if (opt.state & QStyle::State_MouseOver) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(38, 38, 43));
            p->drawRoundedRect(r, 6, 6);
        }

        QRect thumb(r.left() + 8, r.top() + 6, 76, 44);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(38, 38, 43));
        p->drawRoundedRect(thumb, 4, 4);

        QString title;
        if (c.kind == ClipItem::Image) {
            const QPixmap pm(c.imagePath);
            if (!pm.isNull()) {
                const QPixmap s = pm.scaled(72, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p->drawPixmap(thumb.center() - QPoint(s.width() / 2, s.height() / 2), s);
                title = QStringLiteral("图片  %1×%2").arg(pm.width()).arg(pm.height());
            } else {
                title = QStringLiteral("图片（文件缺失）");
            }
        } else {
            p->setPen(QColor(154, 160, 166));
            QFont tf = opt.font;
            tf.setPixelSize(13);
            p->setFont(tf);
            p->drawText(thumb, Qt::AlignCenter, QStringLiteral("T"));
            title = c.text.simplified();
        }

        QFont f = opt.font;
        f.setPixelSize(12);
        p->setFont(f);
        p->setPen(QColor(234, 234, 234));
        const QRect titleRect(thumb.right() + 10, r.top() + 7, r.width() - thumb.width() - 28, 20);
        p->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(f).elidedText(title, Qt::ElideRight, titleRect.width()));

        f.setPixelSize(10);
        p->setFont(f);
        p->setPen(QColor(139, 144, 150));
        QString detail = c.time.toString(QStringLiteral("MM-dd HH:mm"));
        if (c.pinned)
            detail += QStringLiteral("    ★ 已固定");
        p->drawText(QRect(titleRect.left(), titleRect.bottom() + 2, titleRect.width(), 16),
                    Qt::AlignLeft | Qt::AlignVCenter, detail);
        p->restore();
    }

private:
    ClipboardManager* m_mgr = nullptr;
};

class ClipboardWindow : public QDialog {
    Q_OBJECT
public:
    explicit ClipboardWindow(ClipboardManager* mgr, QWidget* parent = nullptr)
        : QDialog(parent), m_mgr(mgr) {
        setWindowTitle(QStringLiteral("SnipShot 剪贴板历史"));
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
        resize(680, 700);
        setStyleSheet(
            "QDialog { background:#1b1b1e; }"
            "QLabel { color:#e6e6e6; }"
            "QLineEdit { background:#26262a; color:#eee; border:1px solid #333; "
            "border-radius:5px; padding:5px 8px; }"
            "QListWidget { background:transparent; border:none; outline:none; }"
            "QListWidget::item { border:0; }"
            "QListWidget::item:selected { background:#2b3a4a; border-radius:6px; }"
            "QPushButton { background:#2a2a2e; color:#eee; border:none; padding:6px 12px; "
            "border-radius:5px; }"
            "QPushButton:hover { background:#3a3a40; }"
            "QPushButton:disabled { color:#777; background:#232326; }"
            "QTabBar::tab { background:transparent; color:#9aa0a6; padding:6px 14px; "
            "border:none; }"
            "QTabBar::tab:selected { color:#4db8ff; border-bottom:2px solid #4db8ff; }");

        m_tabs = new QTabBar;
        m_tabs->addTab(QStringLiteral("当前"));
        m_tabs->addTab(QStringLiteral("固定"));
        m_tabs->setExpanding(false);
        m_tabs->setDrawBase(false);

        auto* top = new QHBoxLayout;
        top->addWidget(m_tabs);
        top->addSpacing(8);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(QStringLiteral("搜索文本或图片…"));
        top->addWidget(m_search, 1);
        m_status = new QLabel;
        m_status->setStyleSheet("QLabel { color:#888; font-size:11px; }");
        top->addWidget(m_status);

        m_list = new QListWidget;
        m_list->setIconSize(QSize(72, 44));
        m_list->setSpacing(2);
        m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_list->setItemDelegate(new ClipItemDelegate(mgr, m_list));
        m_list->setMouseTracking(true);

        auto* btns = new QHBoxLayout;
        m_copy = new QPushButton(QStringLiteral("复制"));
        m_pin = new QPushButton(QStringLiteral("固定"));
        m_del = new QPushButton(QStringLiteral("删除"));
        m_clear = new QPushButton(QStringLiteral("清空未固定"));
        auto* close = new QPushButton(QStringLiteral("关闭"));
        btns->addWidget(m_copy);
        btns->addWidget(m_pin);
        btns->addWidget(m_del);
        btns->addWidget(m_clear);
        btns->addStretch();
        btns->addWidget(close);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(12, 10, 12, 12);
        lay->addLayout(top);
        lay->addWidget(m_list, 1);
        lay->addLayout(btns);

        connect(m_tabs, &QTabBar::currentChanged, this, [this]() { refresh(); });
        connect(m_search, &QLineEdit::textChanged, this, [this]() { refresh(); });
        m_search->installEventFilter(this);
        m_list->installEventFilter(this);
        connect(m_list, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem* it) { activateItem(it); });
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_list, &QListWidget::customContextMenuRequested, this,
                [this](const QPoint& p) { showItemMenu(p); });
        connect(m_copy, &QPushButton::clicked, this, [this]() { doCopy(); });
        connect(m_pin, &QPushButton::clicked, this, [this]() {
            if (auto* it = m_list->currentItem())
                m_mgr->togglePinned(it->data(Qt::UserRole).toInt());
        });
        connect(m_del, &QPushButton::clicked, this, [this]() {
            if (auto* it = m_list->currentItem())
                m_mgr->removeItem(it->data(Qt::UserRole).toInt());
        });
        connect(m_clear, &QPushButton::clicked, this, [this]() { m_mgr->clearUnpinned(); });
        connect(close, &QPushButton::clicked, this, &QWidget::close);
        connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() { updateButtons(); });
        refresh();
        m_search->setFocus();
    }

    void refresh() {
        const QString q = m_search->text().trimmed();
        const bool pinnedTab = m_tabs->currentIndex() == 1;
        m_list->clear();
        const auto& items = m_mgr->items();
        int shown = 0, pinned = 0;
        for (int i = 0; i < items.size(); ++i) {
            const ClipItem& c = items[i];
            if (c.pinned)
                ++pinned;
            if (pinnedTab && !c.pinned)
                continue;
            if (!q.isEmpty()) {
                const QString hay = c.kind == ClipItem::Text ? c.text : QStringLiteral("图片");
                if (!hay.contains(q, Qt::CaseInsensitive))
                    continue;
            }
            makeRow(c, i);
            ++shown;        }
        m_status->setText(QStringLiteral("共 %1 · 固定 %2 · 显示 %3")
                              .arg(items.size())
                              .arg(pinned)
                              .arg(shown));
        m_tabs->setTabText(0, QStringLiteral("当前"));
        m_tabs->setTabText(1, QStringLiteral("固定 %1").arg(pinned));
        updateButtons();
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (o == m_search && e->type() == QEvent::KeyPress) {            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
                if (m_list->count() > 0) {
                    m_list->setFocus();
                    m_list->setCurrentRow(ke->key() == Qt::Key_Down ? 0 : m_list->count() - 1);
                }
                return true;
            }
            if ((ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) &&
                m_search->text().isEmpty()) {
                switchTab(ke->key() == Qt::Key_Left ? -1 : 1);
                return true;
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                doPaste();
                return true;
            }
        }
        if (o == m_list && e->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(e);
            if (ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) {
                switchTab(ke->key() == Qt::Key_Left ? -1 : 1);
                return true;
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                doPaste();
                return true;
            }
        }
        return QDialog::eventFilter(o, e);
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        if (e->key() == Qt::Key_Left) {
            switchTab(-1);
            return;
        }
        if (e->key() == Qt::Key_Right) {
            switchTab(1);
            return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            doPaste();
            return;
        }
        QDialog::keyPressEvent(e);
    }

private:
    void switchTab(int delta) {
        if (!m_tabs || m_tabs->count() == 0)
            return;
        const int n = m_tabs->count();
        m_tabs->setCurrentIndex((m_tabs->currentIndex() + delta + n) % n);
        qInfo() << "clipboard switchTab" << delta << "->" << m_tabs->currentIndex();
        m_list->setFocus();
        if (m_list->count() > 0)
            m_list->setCurrentRow(0);
    }

    void activateItem(QListWidgetItem* it) {
        if (!it)
            return;
        const int idx = it->data(Qt::UserRole).toInt();
        const auto& items = m_mgr->items();
        if (idx < 0 || idx >= items.size())
            return;
        if (items[idx].kind == ClipItem::Image)
            previewImage(items[idx].imagePath);
        else
            doPaste();
    }

    void previewImage(const QString& path) {
        QPixmap pm(path);
        if (pm.isNull())
            return;
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("图片预览"));
        dlg.setStyleSheet("QDialog { background:#111; }");
        auto* label = new QLabel;
        label->setAlignment(Qt::AlignCenter);
        QScreen* s = screen();
        const QSize max = s ? s->availableGeometry().size() * 0.88 : QSize(1280, 800);
        label->setPixmap(pm.scaled(max, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        auto* lay = new QVBoxLayout(&dlg);
        lay->setContentsMargins(6, 6, 6, 6);
        lay->addWidget(label);
        dlg.resize(label->pixmap().size() + QSize(16, 16));
        dlg.exec();
    }

    void showItemMenu(const QPoint& pos) {
        auto* it = m_list->itemAt(pos);
        if (!it)
            return;
        m_list->setCurrentItem(it);
        const int idx = it->data(Qt::UserRole).toInt();
        const auto& items = m_mgr->items();
        if (idx < 0 || idx >= items.size())
            return;
        const ClipItem& c = items[idx];
        QMenu menu(this);
        menu.addAction(QStringLiteral("复制"), this, [this]() { doCopy(); });
        if (c.kind == ClipItem::Text) {
            menu.addAction(QStringLiteral("编辑文字…"), this, [this, idx]() {
                const auto& all = m_mgr->items();
                if (idx < 0 || idx >= all.size())
                    return;
                bool ok = false;
                const QString text = QInputDialog::getMultiLineText(
                    this, QStringLiteral("编辑文字"), QStringLiteral("内容："),
                    all[idx].text, &ok);
                if (ok && !text.trimmed().isEmpty())
                    m_mgr->updateText(idx, text);
            });
        } else {
            menu.addAction(QStringLiteral("查看大图…"), this,
                           [this, path = c.imagePath]() { previewImage(path); });
        }
        menu.addAction(c.pinned ? QStringLiteral("取消固定") : QStringLiteral("固定"), this,
                       [this, idx]() { m_mgr->togglePinned(idx); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除"), this, [this, idx]() { m_mgr->removeItem(idx); });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    }

    QListWidgetItem* makeRow(const ClipItem& c, int index) {
        Q_UNUSED(c);
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 58));
        m_list->addItem(item);
        return item;
    }

    void doCopy() {
        auto* it = m_list->currentItem();
        if (!it)
            return;
        m_mgr->copyItem(it->data(Qt::UserRole).toInt());
        close();
    }

    void doPaste() {
        auto* it = m_list->currentItem();
        if (!it && m_list->count() > 0)
            it = m_list->item(0);
        if (!it)
            return;
        m_mgr->pasteItem(it->data(Qt::UserRole).toInt());
    }
    void updateButtons() {
        auto* it = m_list->currentItem();
        const bool has = it != nullptr;
        m_copy->setEnabled(has);
        m_pin->setEnabled(has);
        m_del->setEnabled(has);
        if (has) {
            const int idx = it->data(Qt::UserRole).toInt();
            const auto& items = m_mgr->items();
            const bool pinned = idx >= 0 && idx < items.size() && items[idx].pinned;
            m_pin->setText(pinned ? QStringLiteral("取消固定") : QStringLiteral("固定"));
        } else {
            m_pin->setText(QStringLiteral("固定"));
        }
    }

    ClipboardManager* m_mgr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_status = nullptr;
    QTabBar* m_tabs = nullptr;
    QPushButton* m_copy = nullptr;
    QPushButton* m_pin = nullptr;
    QPushButton* m_del = nullptr;
    QPushButton* m_clear = nullptr;
};

// ---------------- 管理器 ----------------

ClipboardManager::ClipboardManager(QObject* parent) : QObject(parent) {}

ClipboardManager::~ClipboardManager() {
    stop();
}

QString ClipboardManager::storeDir() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/clipboard";
}

void ClipboardManager::start() {
    const Settings st = Settings::load();
    if (!st.clipboardEnabled) {
        stop();
        return;
    }
    if (!m_running) {
        QDir().mkpath(storeDir());
        load();
        m_running = true;
        if (extensionInstalled()) {
            startRegistrySync();
        } else {
            startWatchers();
        }
    }
    emit changed();
}

void ClipboardManager::stop() {
    stopWatchers();
    stopRegistrySync();
    m_running = false;
    if (m_window)
        m_window->close();
}

void ClipboardManager::reloadSettings() {
    const Settings st = Settings::load();
    if (!st.clipboardEnabled) {
        stop();
        return;
    }
    if (!m_running) {
        start();
    } else if (m_poll) {
        syncRegistry();
    } else {
        startWatchers();
    }
    enforceLimit();
    emit changed();
}

void ClipboardManager::startWatchers() {
    stopWatchers();
#ifdef Q_OS_WIN
    // Windows：Qt 内部监听 WM_CLIPBOARDUPDATE，别的程序复制也能收到事件，无需轮询。
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
        const QMimeData* mime = QApplication::clipboard()->mimeData();
        if (!mime)
            return;
        if (mime->hasImage()) {
            const QImage img = qvariant_cast<QImage>(mime->imageData());
            if (!img.isNull())
                addImage(img);
        } else if (mime->hasText()) {
            addText(mime->text());
        }
    });
    return;
#endif
    const QString wl = QStandardPaths::findExecutable("wl-paste");
    if (wl.isEmpty()) {
        // 没有 wl-clipboard 时退化为前台 QClipboard 监听（仅本应用有焦点时可靠）
        connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
            const QMimeData* mime = QApplication::clipboard()->mimeData();
            if (!mime)
                return;
            if (mime->hasImage()) {
                const QImage img = qvariant_cast<QImage>(mime->imageData());
                if (!img.isNull())
                    addImage(img);
            } else if (mime->hasText()) {
                addText(mime->text());
            }
        }, Qt::UniqueConnection);
        emit error(QStringLiteral("未找到 wl-paste，剪贴板只在 SnipShot 有焦点时记录"));
        return;
    }

    const QString app = QCoreApplication::applicationFilePath();
    auto makeScript = [app](const QString& type) {
        return QString(
            "f=$(mktemp) || exit 0\n"
            "cat > \"$f\"\n"
            "\"%1\" --clip-file=%2:\"$f\" >/dev/null 2>&1\n")
            .arg(app, type);
    };

    m_textWatch = new QProcess(this);
    m_textWatch->setProgram(wl);
    m_textWatch->setArguments({"--watch", "sh", "-c", makeScript("text")});
    m_textWatch->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_textWatch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_textWatch)
            emit error(QStringLiteral("wl-paste 文本监听失败: ") + m_textWatch->errorString());
    });
    m_textWatch->start();

    m_imageWatch = new QProcess(this);
    m_imageWatch->setProgram(wl);
    m_imageWatch->setArguments({"--type", "image/png", "--watch", "sh", "-c", makeScript("image")});
    m_imageWatch->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(m_imageWatch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_imageWatch)
            emit error(QStringLiteral("wl-paste 图片监听失败: ") + m_imageWatch->errorString());
    });
    m_imageWatch->start();
}

void ClipboardManager::stopWatchers() {
    if (m_textWatch) {
        m_textWatch->disconnect(this);
        m_textWatch->kill();
        m_textWatch->deleteLater();
        m_textWatch = nullptr;
    }
    if (m_imageWatch) {
        m_imageWatch->disconnect(this);
        m_imageWatch->kill();
        m_imageWatch->deleteLater();
        m_imageWatch = nullptr;
    }
    disconnect(QApplication::clipboard(), nullptr, this, nullptr);
}

QString ClipboardManager::extensionCacheDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           "/clipboard-indicator@tudmotu.com";
}

QString ClipboardManager::extensionRegistryPath() const {
    return extensionCacheDir() + "/registry.txt";
}

bool ClipboardManager::extensionInstalled() const {
    const QString uuid = QStringLiteral("clipboard-indicator@tudmotu.com");
    return QFileInfo::exists(QDir::homePath() + "/.local/share/gnome-shell/extensions/" + uuid) ||
           QFileInfo::exists("/usr/share/gnome-shell/extensions/" + uuid);
}

void ClipboardManager::startRegistrySync() {
    stopWatchers();
    stopRegistrySync();
    QDir().mkpath(extensionCacheDir());
    m_registryPath = extensionRegistryPath();
    m_watch = new QFileSystemWatcher(this);
    // 目录和文件都监听：registry.txt 可能尚未创建
    if (QFileInfo::exists(m_registryPath))
        m_watch->addPath(m_registryPath);
    if (QFileInfo::exists(extensionCacheDir()))
        m_watch->addPath(extensionCacheDir());
    connect(m_watch, &QFileSystemWatcher::fileChanged, this, [this]() {
        syncRegistry();
        if (QFileInfo::exists(m_registryPath) && !m_watch->files().contains(m_registryPath))
            m_watch->addPath(m_registryPath);
    });
    connect(m_watch, &QFileSystemWatcher::directoryChanged, this, [this]() {
        if (QFileInfo::exists(m_registryPath) && !m_watch->files().contains(m_registryPath))
            m_watch->addPath(m_registryPath);
        syncRegistry();
    });
    m_poll = new QTimer(this);
    m_poll->setInterval(1500);
    connect(m_poll, &QTimer::timeout, this, &ClipboardManager::syncRegistry);
    m_poll->start();
    syncRegistry();
    emit error(QStringLiteral("剪贴板：使用 clipboard-indicator 扩展历史作为后台捕获源"));
}

void ClipboardManager::stopRegistrySync() {
    if (m_poll) {
        m_poll->stop();
        m_poll->deleteLater();
        m_poll = nullptr;
    }
    if (m_watch) {
        m_watch->deleteLater();
        m_watch = nullptr;
    }
}

void ClipboardManager::syncRegistry() {
    if (!m_running)
        return;
    QFile f(m_registryPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return;
    const QJsonArray arr = doc.array();
    const Settings st = Settings::load();
    const int limit = qMin(arr.size(), qMax(20, st.clipboardMax + 20));
    const int before = m_items.size();
    m_syncing = true;
    // 扩展 registry 为新到旧；只取最新的 limit 条，倒序加入，让最新排最前
    for (int i = limit - 1; i >= 0; --i) {
        const QJsonObject o = arr[i].toObject();
        const QString mime = o.value("mimetype").toString();
        const QString contents = o.value("contents").toString();
        const bool fav = o.value("favorite").toBool();
        if (mime.startsWith("text/")) {
            if (!contents.trimmed().isEmpty())
                addText(contents, fav);
        } else if (mime.startsWith("image/")) {
            if (m_extLoaded.contains(contents))
                continue;
            const QString path = extensionCacheDir() + "/" + contents;
            QImage img(path);
            if (!img.isNull())
                addImage(img, fav);
            m_extLoaded.insert(contents);
        }
    }
    m_syncing = false;
    if (m_items.size() != before) {
        enforceLimit();
        save();
        emit changed();
        refreshWindow();
    }
}

void ClipboardManager::addFile(const QString& type, const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        f.remove();
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    f.remove();
    if (type == QStringLiteral("image")) {
        const QImage img = QImage::fromData(data, "PNG");
        if (!img.isNull())
            addImage(img);
    } else {
        const QString text = QString::fromUtf8(data);
        if (!text.trimmed().isEmpty())
            addText(text);
    }
}

void ClipboardManager::addText(const QString& text, bool pinned) {
    const QString t = text;
    if (t.trimmed().isEmpty())
        return;
    const QByteArray hash = QCryptographicHash::hash(t.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString h = QString::fromLatin1(hash);
    if (m_hidden.contains(h))
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].hash == h) {
            if (m_syncing)
                return;
            ClipItem it = m_items.takeAt(i);
            it.time = QDateTime::currentDateTime();
            m_items.prepend(it);
            enforceLimit();
            save();
            emit changed();
            refreshWindow();
            return;
        }
    }
    ClipItem it;
    it.kind = ClipItem::Text;
    it.text = t;
    it.time = QDateTime::currentDateTime();
    it.hash = h;
    it.pinned = pinned;
    m_items.prepend(it);
    if (m_syncing)
        return;
    enforceLimit();
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::addImage(const QImage& img, bool pinFlag) {
    if (img.isNull())
        return;
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (m_hidden.contains(h))
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].hash == h) {
            if (m_syncing)
                return;
            ClipItem it = m_items.takeAt(i);
            it.time = QDateTime::currentDateTime();
            m_items.prepend(it);
            enforceLimit();
            save();
            emit changed();
            refreshWindow();
            return;
        }
    }
    QDir().mkpath(storeDir());
    const QString path = storeDir() + "/" + h + ".png";
    if (!QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return;
        f.write(bytes);
        f.close();
    }
    ClipItem it;
    it.kind = ClipItem::Image;
    it.imagePath = path;
    it.time = QDateTime::currentDateTime();
    it.hash = h;
    it.pinned = pinFlag;
    m_items.prepend(it);
    if (m_syncing)
        return;
    enforceLimit();
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::enforceLimit() {
    const Settings st = Settings::load();
    int max = qMax(10, st.clipboardMax);
    int count = 0;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items[i].pinned)
            continue;
        ++count;
        if (count > max) {
            deleteImageFile(m_items[i]);
            m_items.removeAt(i);
        }
    }
}

void ClipboardManager::deleteImageFile(const ClipItem& it) {
    if (it.kind == ClipItem::Image && !it.imagePath.isEmpty()) {
        // 还有别的条目引用同一文件就不删
        for (const ClipItem& o : m_items) {
            if (&o != &it && o.imagePath == it.imagePath)
                return;
        }
        QFile::remove(it.imagePath);
    }
}

void ClipboardManager::load() {
    m_items.clear();
    m_hidden.clear();
    QFile f(storeDir() + "/clipboard.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    for (const QJsonValue& v : doc.object().value("items").toArray()) {
        const QJsonObject o = v.toObject();
        ClipItem it;
        it.kind = o.value("kind").toString() == "image" ? ClipItem::Image : ClipItem::Text;
        it.text = o.value("text").toString();
        it.imagePath = o.value("image").toString();
        it.hash = o.value("hash").toString();
        it.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        it.pinned = o.value("pinned").toBool();
        if (it.kind == ClipItem::Image && !QFile::exists(it.imagePath))
            continue;
        m_items.append(it);
    }
    for (const QJsonValue& v : doc.object().value("hidden").toArray())
        m_hidden.insert(v.toString());

    // 旧版本没有设置 organizationName，历史存放在 %APPDATA%\SnipShot\clipboard；
    // 首次运行新版时把旧记录合并过来，避免历史“消失”。
    const QString legacy = QString::fromLocal8Bit(qgetenv("APPDATA")) + QStringLiteral("/SnipShot/clipboard");
    if (QDir(legacy) != QDir(storeDir()) && QFileInfo::exists(legacy + "/clipboard.json")) {
        QFile lf(legacy + "/clipboard.json");
        if (lf.open(QIODevice::ReadOnly)) {
            const QJsonDocument ldoc = QJsonDocument::fromJson(lf.readAll());
            lf.close();
            QSet<QString> have;
            for (const ClipItem& it : m_items)
                have.insert(it.hash);
            bool changed = false;
            for (const QJsonValue& v : ldoc.object().value("items").toArray()) {
                const QJsonObject o = v.toObject();
                ClipItem it;
                it.kind = o.value("kind").toString() == "image" ? ClipItem::Image : ClipItem::Text;
                it.text = o.value("text").toString();
                it.imagePath = o.value("image").toString();
                it.hash = o.value("hash").toString();
                it.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
                it.pinned = o.value("pinned").toBool();
                if (!it.hash.isEmpty() && have.contains(it.hash))
                    continue;
                if (it.kind == ClipItem::Image) {
                    if (!QFile::exists(it.imagePath))
                        continue;
                    const QString dst = storeDir() + "/" + QFileInfo(it.imagePath).fileName();
                    if (!QFile::exists(dst))
                        QFile::copy(it.imagePath, dst);
                    it.imagePath = dst;
                }
                m_items.append(it);
                if (!it.hash.isEmpty())
                    have.insert(it.hash);
                changed = true;
            }
            if (changed) {
                qInfo() << "剪贴板：已合并旧版历史" << legacy;
                save();
            }
        }
    }
    enforceLimit();
}

void ClipboardManager::save() {
    QDir().mkpath(storeDir());
    QJsonArray arr;
    for (const ClipItem& it : m_items) {
        QJsonObject o;
        o["kind"] = it.kind == ClipItem::Image ? "image" : "text";
        o["text"] = it.text;
        o["image"] = it.imagePath;
        o["hash"] = it.hash;
        o["time"] = it.time.toString(Qt::ISODate);
        o["pinned"] = it.pinned;
        arr.append(o);
    }
    QJsonArray hiddenArr;
    for (const QString& h : m_hidden)
        hiddenArr.append(h);
    QJsonObject root;
    root["items"] = arr;
    root["hidden"] = hiddenArr;
    QSaveFile f(storeDir() + "/clipboard.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void ClipboardManager::scheduleSave() {
    if (m_savePending)
        return;
    m_savePending = true;
    QTimer::singleShot(150, this, [this]() {
        m_savePending = false;
        save();
    });
}

void ClipboardManager::openWindow() {
    if (!m_running)
        start();
    if (m_window) {
        m_window->refresh();
#ifdef Q_OS_WIN
        if (!m_window->isVisible())
            m_prevHwnd = quintptr(GetForegroundWindow());
#endif
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
#ifdef Q_OS_WIN
        forceForeground(m_window);
#endif
        return;
    }
#ifdef Q_OS_WIN
    m_prevHwnd = quintptr(GetForegroundWindow());
#endif
    m_window = new ClipboardWindow(this);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
#ifdef Q_OS_WIN
    forceForeground(m_window);
#endif
}

void ClipboardManager::refreshWindow() {
    if (m_window)
        m_window->refresh();
}

void ClipboardManager::copyItem(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    const ClipItem& it = m_items[index];
    if (it.kind == ClipItem::Text) {
        QApplication::clipboard()->setText(it.text);
    } else {
        const QImage img(it.imagePath);
        if (!img.isNull())
            QApplication::clipboard()->setImage(img);
    }
    emit changed();
}

void ClipboardManager::pasteItem(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    copyItem(index);
    if (m_window)
        m_window->hide();
#ifdef Q_OS_WIN
    if (m_prevHwnd) {
        SetForegroundWindow(HWND(m_prevHwnd));
        QTimer::singleShot(120, []() {
            keybd_event(VK_CONTROL, 0, 0, 0);
            keybd_event('V', 0, 0, 0);
            keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        });
    }
#endif
    if (m_window)
        m_window->close();
}

void ClipboardManager::togglePinned(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    m_items[index].pinned = !m_items[index].pinned;
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::removeItem(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    m_hidden.insert(m_items[index].hash);
    deleteImageFile(m_items[index]);
    m_items.removeAt(index);
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::updateText(int index, const QString& text) {
    if (index < 0 || index >= m_items.size())
        return;
    if (m_items[index].kind != ClipItem::Text)
        return;
    if (text.trimmed().isEmpty())
        return;
    m_items[index].text = text;
    m_items[index].hash = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_items[index].time = QDateTime::currentDateTime();
    save();
    emit changed();
    refreshWindow();
}

void ClipboardManager::clearUnpinned() {
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (!m_items[i].pinned) {
            m_hidden.insert(m_items[i].hash);
            deleteImageFile(m_items[i]);
            m_items.removeAt(i);
        }
    }
    save();
    emit changed();
    refreshWindow();
}

#include "clipboard.moc"
