#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "settings.h"

// 全局快捷键：
// - Windows：RegisterHotKey + WM_HOTKEY 原生事件过滤（常驻进程内注册）。
// - Linux/Wayland：写 GNOME 自定义快捷键，绑定命令行参数唤醒常驻进程。
namespace Hotkeys {
#ifdef Q_OS_WIN
// 注册全部快捷键；problems 返回失败项（如被其它程序占用）
bool installWindows(const Settings& st, const std::function<void(const QString&)>& onCommand,
                    QStringList* problems = nullptr);
bool reinstallWindows(const Settings& st, QStringList* problems = nullptr);
void uninstallWindows();
// 测试某个组合是否可用（不会影响已注册的热键）。
// 已被本程序注册的组合也视为可用。
bool testBinding(const QString& text, QString* error = nullptr);
#endif
bool installGnome(const Settings& st);   // Linux：注册到 GNOME
QString executablePath();                 // 当前程序路径
}
