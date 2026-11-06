#include "hotkeys.h"

#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <QAbstractNativeEventFilter>
#  include <QHash>
#  include <QVector>
#else
#  include <algorithm>
#  include <QProcess>
#  include <QRegularExpression>
#endif

QString Hotkeys::executablePath() {
    return QCoreApplication::applicationFilePath();
}

#ifdef Q_OS_WIN

namespace {

struct Binding {
    QString text;
    QString command;
    UINT mods = 0;
    UINT vk = 0;
};

class WinHotkeyFilter : public QAbstractNativeEventFilter {
public:
    std::function<void(const QString&)> onCommand;
    QHash<int, QString> idToCommand;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        Q_UNUSED(eventType);
        MSG* msg = static_cast<MSG*>(message);
        if (msg && msg->message == WM_HOTKEY) {
            const auto it = idToCommand.constFind(int(msg->wParam));
            if (it != idToCommand.constEnd()) {
                if (onCommand)
                    onCommand(it.value());
                if (result)
                    *result = 0;
                return true;
            }
        }
        return false;
    }
};

WinHotkeyFilter* g_filter = nullptr;
std::function<void(const QString&)> g_callback;
QVector<int> g_registeredIds;
int g_nextId = 0x5340;

QString vkName(UINT vk) {
    switch (vk) {
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
    case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
    case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
    case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    case VK_F13: return "F13"; case VK_F14: return "F14"; case VK_F15: return "F15";
    case VK_F16: return "F16"; case VK_F17: return "F17"; case VK_F18: return "F18";
    case VK_F19: return "F19"; case VK_F20: return "F20"; case VK_F21: return "F21";
    case VK_F22: return "F22"; case VK_F23: return "F23"; case VK_F24: return "F24";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_INSERT: return "Insert";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_SPACE: return "Space";
    case VK_TAB: return "Tab";
    case VK_ESCAPE: return "Esc";
    case VK_RETURN: return "Enter";
    case VK_BACK: return "Backspace";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_OEM_3: return "`";
    case VK_OEM_MINUS: return "-";
    case VK_OEM_PLUS: return "=";
    case VK_OEM_4: return "[";
    case VK_OEM_6: return "]";
    case VK_OEM_5: return "\\";
    case VK_OEM_1: return ";";
    case VK_OEM_7: return "'";
    case VK_OEM_COMMA: return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_2: return "/";
    default: break;
    }
    if (vk >= 'A' && vk <= 'Z')
        return QString(QChar(vk));
    if (vk >= '0' && vk <= '9')
        return QString(QChar(vk));
    return QString();
}

bool parseKey(const QString& raw, UINT* vk) {
    const QString k = raw.trimmed();
    if (k.isEmpty())
        return false;
    const QString up = k.toUpper();
    if (up == "PRINTSCREEN" || up == "PRTSC" || up == "PRTSCN") { *vk = VK_SNAPSHOT; return true; }
    if (up == "INSERT" || up == "INS") { *vk = VK_INSERT; return true; }
    if (up == "DELETE" || up == "DEL") { *vk = VK_DELETE; return true; }
    if (up == "HOME") { *vk = VK_HOME; return true; }
    if (up == "END") { *vk = VK_END; return true; }
    if (up == "PAGEUP" || up == "PGUP") { *vk = VK_PRIOR; return true; }
    if (up == "PAGEDOWN" || up == "PGDN" || up == "PAGEDN") { *vk = VK_NEXT; return true; }
    if (up == "SPACE") { *vk = VK_SPACE; return true; }
    if (up == "TAB") { *vk = VK_TAB; return true; }
    if (up == "ESC" || up == "ESCAPE") { *vk = VK_ESCAPE; return true; }
    if (up == "ENTER" || up == "RETURN") { *vk = VK_RETURN; return true; }
    if (up == "BACKSPACE" || up == "BACK") { *vk = VK_BACK; return true; }
    if (up == "LEFT" || up == "RIGHT" || up == "UP" || up == "DOWN") {
        if (up == "LEFT") *vk = VK_LEFT;
        else if (up == "RIGHT") *vk = VK_RIGHT;
        else if (up == "UP") *vk = VK_UP;
        else *vk = VK_DOWN;
        return true;
    }
    if (up.startsWith('F') && up.mid(1).toInt() >= 1 && up.mid(1).toInt() <= 24) {
        *vk = UINT(VK_F1 + up.mid(1).toInt() - 1);
        return true;
    }
    if (k.size() == 1) {
        const QChar c = k.at(0).toUpper();
        const SHORT scan = VkKeyScanW(WCHAR(c.unicode()));
        if (scan != -1) {
            *vk = UINT(scan & 0xFF);
            return true;
        }
        if (c.isLetterOrNumber()) {
            *vk = UINT(c.unicode());
            return true;
        }
    }
    return false;
}

bool parseBinding(const QString& text, Binding* out) {
    out->text = text.trimmed();
    out->mods = MOD_NOREPEAT;
    out->vk = 0;
    const QStringList parts = out->text.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString m = parts[i].trimmed().toLower();
        if (m == "alt") out->mods |= MOD_ALT;
        else if (m == "ctrl" || m == "control") out->mods |= MOD_CONTROL;
        else if (m == "shift") out->mods |= MOD_SHIFT;
        else if (m == "win" || m == "super" || m == "meta") out->mods |= MOD_WIN;
        else return false;
    }
    return parseKey(parts.last(), &out->vk);
}

void unregisterAll() {
    for (int id : g_registeredIds)
        UnregisterHotKey(nullptr, id);
    g_registeredIds.clear();
    if (g_filter)
        g_filter->idToCommand.clear();
}

QString winErrorText(DWORD err) {
    if (err == ERROR_HOTKEY_ALREADY_REGISTERED)
        return QStringLiteral("已被其他程序占用");
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString s = n ? QString::fromWCharArray(buf, int(n)).trimmed() : QStringLiteral("错误 %1").arg(err);
    if (buf)
        LocalFree(buf);
    return s;
}

}  // namespace

bool Hotkeys::installWindows(const Settings& st, const std::function<void(const QString&)>& onCommand,
                             QStringList* problems) {
    if (problems)
        problems->clear();
    g_callback = onCommand;

    if (!g_filter) {
        g_filter = new WinHotkeyFilter;
        QCoreApplication::instance()->installNativeEventFilter(g_filter);
    }
    g_filter->onCommand = g_callback;
    unregisterAll();

    QVector<Binding> list{
        {st.hkSnip, "--snip", 0, 0},
        {st.hkPin, "--pin", 0, 0},
        {st.hkTogglePins, "--toggle-pins", 0, 0},
        {st.hkTranslate, "--translate", 0, 0},
        {st.hkOcr, "--ocr", 0, 0},
        {st.hkClipboard, "--clipboard", 0, 0},
        {st.hkRecord, "--record-toggle", 0, 0},
    };

    bool ok = true;
    for (Binding& b : list) {
        if (b.text.trimmed().isEmpty())
            continue;
        if (!parseBinding(b.text, &b)) {
            ok = false;
            if (problems)
                problems->append(QStringLiteral("%1：格式无法识别").arg(b.text));
            continue;
        }
        const int id = g_nextId++;
        if (RegisterHotKey(nullptr, id, b.mods, b.vk)) {
            g_registeredIds.append(id);
            g_filter->idToCommand.insert(id, b.command);
            qInfo() << "热键已注册:" << b.text << "->" << b.command;
        } else {
            ok = false;
            if (problems)
                problems->append(QStringLiteral("%1：%2").arg(b.text, winErrorText(GetLastError())));
            qWarning() << "热键注册失败:" << b.text;
        }
    }
    return ok;
}

bool Hotkeys::reinstallWindows(const Settings& st, QStringList* problems) {
    return installWindows(st, g_callback, problems);
}

void Hotkeys::uninstallWindows() {
    unregisterAll();
    if (g_filter) {
        QCoreApplication::instance()->removeNativeEventFilter(g_filter);
        delete g_filter;
        g_filter = nullptr;
    }
}

#else  // ---------------- Linux ----------------

static const char* kMediaKeysSchema = "org.gnome.settings-daemon.plugins.media-keys";
static const char* kCustomPrefix = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/";

static QString normalizeBinding(const QString& input) {
    QString b = input.trimmed();
    if (b.isEmpty())
        return b;
    if (b.contains('<') || b.contains('>')) {
        if (b.back().isLetter() && b.back().isUpper())
            b[b.size() - 1] = b.back().toLower();
        return b;
    }
    const QStringList parts = b.split('+', Qt::SkipEmptyParts);
    if (parts.size() <= 1) {
        if (b.size() == 1 && b[0].isLetter())
            b = b.toLower();
        return b;
    }
    QString out;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString m = parts[i].trimmed().toLower();
        if (m == QStringLiteral("alt"))
            out += QStringLiteral("<Alt>");
        else if (m == QStringLiteral("ctrl") || m == QStringLiteral("control"))
            out += QStringLiteral("<Ctrl>");
        else if (m == QStringLiteral("shift"))
            out += QStringLiteral("<Shift>");
        else if (m == QStringLiteral("super") || m == QStringLiteral("win") || m == QStringLiteral("meta"))
            out += QStringLiteral("<Super>");
        else
            out += QStringLiteral("<") + parts[i].trimmed() + QStringLiteral(">");
    }
    QString key = parts.last().trimmed();
    if (key.size() == 1 && key[0].isLetter())
        key = key.toLower();
    return out + key;
}

static bool gsettingsSet(const QString& schema, const QString& key, const QString& value) {
    return QProcess::startDetached("gsettings", {"set", schema, key, value});
}

static QString gsettingsGet(const QString& schema, const QString& key) {
    QProcess p;
    p.start("gsettings", {"get", schema, key});
    p.waitForFinished(5000);
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool Hotkeys::installGnome(const Settings& st) {
    const QString cur = gsettingsGet(kMediaKeysSchema, "custom-keybindings");
    QRegularExpression re(QString("(%1[^']+)").arg(kCustomPrefix));
    QStringList paths;
    auto it = re.globalMatch(cur);
    while (it.hasNext())
        paths << it.next().captured(1);

    struct Entry { QString name; QString cmd; QString binding; };
    const QList<Entry> entries{
        {"snipshot-snip",      executablePath() + " --snip",                    st.hkSnip},
        {"snipshot-pin",       executablePath() + " --pin",                     st.hkPin},
        {"snipshot-toggle",    executablePath() + " --toggle-pins",             st.hkTogglePins},
        {"snipshot-translate", executablePath() + " --translate",               st.hkTranslate},
        {"snipshot-ocr",       executablePath() + " --ocr",                     st.hkOcr},
        {"snipshot-clipboard", executablePath() + " --clipboard",               st.hkClipboard},
        {"snipshot-record",    executablePath() + " --record-toggle",           st.hkRecord},
    };

    paths.erase(std::remove_if(paths.begin(), paths.end(),
                               [](const QString& p) { return p.contains(QStringLiteral("-upper/")); }),
                paths.end());

    bool ok = true;
    for (const Entry& e : entries) {
        const QString path = QString(kCustomPrefix) + e.name + "/";
        const QString schema = QString("org.gnome.settings-daemon.plugins.media-keys.custom-keybinding:") + path;
        ok = gsettingsSet(schema, "name", e.name) && ok;
        ok = gsettingsSet(schema, "command", e.cmd) && ok;
        ok = gsettingsSet(schema, "binding", normalizeBinding(e.binding)) && ok;
        if (!paths.contains(path))
            paths << path;
    }
    QString list = "[";
    for (int i = 0; i < paths.size(); ++i) {
        if (i)
            list += ", ";
        list += "'" + paths[i] + "'";
    }
    list += "]";
    ok = gsettingsSet(kMediaKeysSchema, "custom-keybindings", list) && ok;
    return ok;
}

#endif
