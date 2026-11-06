#include "settingsdialog.h"
#include "hotkeys.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <QWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QStringList>

HotkeyEdit::HotkeyEdit(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText(QStringLiteral("点击后按下快捷键"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("点击后直接按键盘组合（如 Ctrl+Alt+S）；Backspace 清除，Esc 取消"));
}

void HotkeyEdit::setBinding(const QString& text) {
    m_binding = text;
    setText(text.isEmpty() ? QStringLiteral("未设置") : text);
}

void HotkeyEdit::startCapture() {
    m_old = m_binding;
    m_capturing = true;
    setText(QStringLiteral("请按下快捷键…"));
    setFocus();
    selectAll();
}

void HotkeyEdit::focusInEvent(QFocusEvent* e) {
    QLineEdit::focusInEvent(e);
    if (!m_capturing)
        startCapture();
}

void HotkeyEdit::mousePressEvent(QMouseEvent* e) {
    if (!m_capturing) {
        startCapture();
        e->accept();
        return;
    }
    QLineEdit::mousePressEvent(e);
}

QString HotkeyEdit::keyName(int key, const QString& text) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(key));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(key));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
    switch (key) {
    case Qt::Key_Print: return QStringLiteral("PrintScreen");
    case Qt::Key_Insert: return QStringLiteral("Insert");
    case Qt::Key_Delete: return QStringLiteral("Delete");
    case Qt::Key_Home: return QStringLiteral("Home");
    case Qt::Key_End: return QStringLiteral("End");
    case Qt::Key_PageUp: return QStringLiteral("PageUp");
    case Qt::Key_PageDown: return QStringLiteral("PageDown");
    case Qt::Key_Space: return QStringLiteral("Space");
    case Qt::Key_Tab: return QStringLiteral("Tab");
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("Enter");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Left: return QStringLiteral("Left");
    case Qt::Key_Right: return QStringLiteral("Right");
    case Qt::Key_Up: return QStringLiteral("Up");
    case Qt::Key_Down: return QStringLiteral("Down");
    case Qt::Key_Minus: return QStringLiteral("-");
    case Qt::Key_Equal: return QStringLiteral("=");
    case Qt::Key_BracketLeft: return QStringLiteral("[");
    case Qt::Key_BracketRight: return QStringLiteral("]");
    case Qt::Key_Backslash: return QStringLiteral("\\");
    case Qt::Key_Semicolon: return QStringLiteral(";");
    case Qt::Key_Apostrophe: return QStringLiteral("'");
    case Qt::Key_Comma: return QStringLiteral(",");
    case Qt::Key_Period: return QStringLiteral(".");
    case Qt::Key_Slash: return QStringLiteral("/");
    case Qt::Key_QuoteLeft: return QStringLiteral("`");
    default: break;
    }
    if (!text.isEmpty() && text.at(0).isPrint())
        return text.left(1).toUpper();
    return QString();
}

void HotkeyEdit::keyPressEvent(QKeyEvent* e) {
    if (!m_capturing) {
        QLineEdit::keyPressEvent(e);
        return;
    }
    const int key = e->key();
    if (key == Qt::Key_Escape) {
        stopCapture(true);
        return;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        m_binding.clear();
        m_capturing = false;
        setText(QStringLiteral("未设置"));
        clearFocus();
        emit captured(QString());
        return;
    }
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta)
        return;   // 等真正的键

    const QString base = keyName(key, e->text());
    if (base.isEmpty())
        return;

    QStringList mods;
    const Qt::KeyboardModifiers m = e->modifiers();
    if (m & Qt::ControlModifier)
        mods << QStringLiteral("Ctrl");
    if (m & Qt::AltModifier)
        mods << QStringLiteral("Alt");
    if (m & Qt::ShiftModifier)
        mods << QStringLiteral("Shift");
    if (m & Qt::MetaModifier)
        mods << QStringLiteral("Win");

    const bool printable = base.length() == 1 && base.at(0).isLetterOrNumber();
    if (mods.isEmpty() && printable) {
        QToolTip::showText(mapToGlobal(QPoint(0, height())),
                           QStringLiteral("字母/数字需要搭配 Ctrl/Alt/Shift/Win"), this);
        return;   // 继续等待
    }

    const QString binding = mods.isEmpty() ? base : (mods.join('+') + '+' + base);
    m_binding = binding;
    m_capturing = false;
    setText(binding);
    clearFocus();
    emit captured(binding);
}

void HotkeyEdit::stopCapture(bool restore) {
    m_capturing = false;
    if (restore)
        m_binding = m_old;
    setText(m_binding.isEmpty() ? QStringLiteral("未设置") : m_binding);
    clearFocus();
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("SnipShot 设置"));
    const Settings st = Settings::load();

    auto* tabs = new QTabWidget;

    // --- API / 保存 ---
    auto* apiPage = new QWidget;
    auto* apiForm = new QFormLayout(apiPage);
    m_base = new QLineEdit(st.apiBase);
    m_base->setPlaceholderText("https://api.openai.com/v1");
    apiForm->addRow(QStringLiteral("API 地址（OpenAI 兼容）:"), m_base);
    m_key = new QLineEdit(st.apiKey);
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(QStringLiteral("sk-...（仅保存在本机，不入仓库）"));
    apiForm->addRow(QStringLiteral("API Key:"), m_key);
    m_model = new QLineEdit(st.model);
    m_model->setPlaceholderText("gpt-4o-mini");
    apiForm->addRow(QStringLiteral("模型:"), m_model);
    m_translateTarget = new QComboBox;
    m_translateTarget->addItems({QStringLiteral("简体中文"), QStringLiteral("繁體中文"),
                                 QStringLiteral("English"), QStringLiteral("日本語"),
                                 QStringLiteral("한국어"), QStringLiteral("Deutsch"),
                                 QStringLiteral("Français"), QStringLiteral("Español"),
                                 QStringLiteral("Русский")});
    m_translateTarget->setCurrentText(st.translateTarget);
    apiForm->addRow(QStringLiteral("默认翻译目标语言:"), m_translateTarget);
    m_dir = new QLineEdit(st.saveDir);
    auto* dirRow = new QHBoxLayout;
    dirRow->addWidget(m_dir);
    auto* browse = new QPushButton(QStringLiteral("浏览…"));
    connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseDir);
    dirRow->addWidget(browse);
    apiForm->addRow(QStringLiteral("截图保存目录:"), dirRow);
    m_pinBorder = new QCheckBox(QStringLiteral("新贴图默认显示边框"));
    m_pinBorder->setChecked(st.pinBorder);
    apiForm->addRow(QStringLiteral("贴图:"), m_pinBorder);
#ifdef Q_OS_WIN
    m_autoStart = new QCheckBox(QStringLiteral("开机自动启动（当前用户）"));
    m_autoStart->setChecked(st.autoStart);
    apiForm->addRow(QStringLiteral("启动:"), m_autoStart);
#endif
    tabs->addTab(apiPage, QStringLiteral("API / 保存"));

    // --- 快捷键 ---
    auto* keysPage = new QWidget;
    auto* keysForm = new QFormLayout(keysPage);
    auto addHotkeyRow = [&](const QString& label, const QString& value, bool enabled,
                            HotkeyEdit** edit, QCheckBox** check) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        *check = new QCheckBox;
        (*check)->setChecked(enabled);
        (*check)->setToolTip(QStringLiteral("启用该快捷键"));
        *edit = new HotkeyEdit;
        (*edit)->setBinding(value);
        h->addWidget(*check);
        h->addWidget(*edit, 1);
        keysForm->addRow(label, row);
        connect(*edit, &HotkeyEdit::captured, this,
                [this, edit](const QString& text) { checkHotkey(*edit, text); });
    };
    addHotkeyRow(QStringLiteral("截图:"), st.hkSnip, st.hkSnipEnabled, &m_hkSnip, &m_hkSnipEnabled);
    addHotkeyRow(QStringLiteral("固定贴图:"), st.hkPin, st.hkPinEnabled, &m_hkPin, &m_hkPinEnabled);
    addHotkeyRow(QStringLiteral("隐藏/显示贴图:"), st.hkTogglePins, st.hkTogglePinsEnabled, &m_hkToggle, &m_hkToggleEnabled);
    addHotkeyRow(QStringLiteral("截屏翻译:"), st.hkTranslate, st.hkTranslateEnabled, &m_hkTranslate, &m_hkTranslateEnabled);
    addHotkeyRow(QStringLiteral("文字提取:"), st.hkOcr, st.hkOcrEnabled, &m_hkOcr, &m_hkOcrEnabled);
    addHotkeyRow(QStringLiteral("剪贴板历史:"), st.hkClipboard, st.hkClipboardEnabled, &m_hkClipboard, &m_hkClipboardEnabled);
    addHotkeyRow(QStringLiteral("开始/停止录屏:"), st.hkRecord, st.hkRecordEnabled, &m_hkRecord, &m_hkRecordEnabled);
#ifdef Q_OS_WIN
    const QString hintText = QStringLiteral(
        "点击输入框后直接按键盘组合即可录入（不是打字）。\n"
        "左侧勾选表示启用；字母/数字必须搭配 Ctrl/Alt/Shift/Win。\n"
        "提示被占用时可以重新录入，或仍然保存（该键不生效）。");
#else
    const QString hintText = QStringLiteral(
        "Wayland 下第三方程序不能注册全局热键。\n"
        "点下面按钮写入 GNOME 自定义快捷键；以后也可在\n"
        "系统设置→键盘→自定义快捷键里修改。");
#endif
    auto* hint = new QLabel(hintText);
    hint->setWordWrap(true);
    keysForm->addRow(hint);
    tabs->addTab(keysPage, QStringLiteral("快捷键"));

    // --- 剪贴板 ---
    auto* clipPage = new QWidget;
    auto* clipForm = new QFormLayout(clipPage);
    m_clipEnabled = new QCheckBox(QStringLiteral("启用剪贴板历史（后台捕获）"));
    m_clipEnabled->setChecked(st.clipboardEnabled);
    clipForm->addRow(QStringLiteral("剪贴板:"), m_clipEnabled);
    m_clipMax = new QSpinBox;
    m_clipMax->setRange(10, 2000);
    m_clipMax->setValue(st.clipboardMax);
    clipForm->addRow(QStringLiteral("最多保留条数:"), m_clipMax);
    auto* clipNote = new QLabel(QStringLiteral(
        "后台记录文本与图片，可在“剪贴板历史”里搜索、固定、编辑、删除。\n"
        "图片按原图保存，双击条目可放大查看。"));
    clipNote->setWordWrap(true);
    clipForm->addRow(clipNote);
    tabs->addTab(clipPage, QStringLiteral("剪贴板"));

    // --- 录屏 ---
    auto* recPage = new QWidget;
    auto* recForm = new QFormLayout(recPage);
    m_recEnabled = new QCheckBox(QStringLiteral("启用录屏功能"));
    m_recEnabled->setChecked(st.recordingEnabled);
    recForm->addRow(QStringLiteral("录屏:"), m_recEnabled);
    m_recFps = new QSpinBox;
    m_recFps->setRange(5, 60);
    m_recFps->setValue(st.recordFps);
    recForm->addRow(QStringLiteral("录屏帧率:"), m_recFps);
    m_recCursor = new QCheckBox(QStringLiteral("录制鼠标指针"));
    m_recCursor->setChecked(st.recordCursor);
    recForm->addRow(QString(), m_recCursor);
    m_recordDir = new QLineEdit(st.recordDir.isEmpty() ? Settings::defaultRecordDir() : st.recordDir);
    auto* recRow = new QHBoxLayout;
    recRow->addWidget(m_recordDir);
    auto* recBrowse = new QPushButton(QStringLiteral("浏览…"));
    connect(recBrowse, &QPushButton::clicked, this, &SettingsDialog::browseRecordDir);
    recRow->addWidget(recBrowse);
    recForm->addRow(QStringLiteral("录屏保存目录:"), recRow);
#ifdef Q_OS_WIN
    m_ffmpeg = new QLineEdit(st.ffmpegPath);
    m_ffmpeg->setPlaceholderText(QStringLiteral("留空自动查找（程序目录 / PATH / E:\\Tools\\ffmpeg）"));
    auto* ffRow = new QHBoxLayout;
    ffRow->addWidget(m_ffmpeg);
    auto* ffBrowse = new QPushButton(QStringLiteral("浏览…"));
    connect(ffBrowse, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择 ffmpeg.exe"),
                                                       m_ffmpeg->text(), QStringLiteral("ffmpeg (ffmpeg.exe)"));
        if (!f.isEmpty())
            m_ffmpeg->setText(f);
    });
    ffRow->addWidget(ffBrowse);
    recForm->addRow(QStringLiteral("FFmpeg 路径:"), ffRow);
#endif
#ifdef Q_OS_WIN
    const QString recordNote = QStringLiteral(
        "录屏使用 ffmpeg（gdigrab），输出 MP4/H.264。\n"
        "ffmpeg.exe 可放在程序目录，或上面指定路径。");
#else
    const QString recordNote = QStringLiteral(
        "录屏使用 GNOME Shell 原生 Screencast（VP8，全桌面），\n"
        "启用后托盘/快捷键可随时开始、停止。");
#endif
    auto* note = new QLabel(recordNote);
    note->setWordWrap(true);
    recForm->addRow(note);
    tabs->addTab(recPage, QStringLiteral("录屏"));

    auto* btns = new QHBoxLayout;
#ifdef Q_OS_WIN
    const QString regText = QStringLiteral("重新注册快捷键");
#else
    const QString regText = QStringLiteral("注册 GNOME 快捷键");
#endif
    auto* reg = new QPushButton(regText);
    connect(reg, &QPushButton::clicked, this, &SettingsDialog::registerHotkeys);
    auto* ok = new QPushButton(QStringLiteral("确定"));
    auto* cancel = new QPushButton(QStringLiteral("取消"));
    connect(ok, &QPushButton::clicked, this, &SettingsDialog::applyAndClose);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    btns->addWidget(reg);
    btns->addStretch();
    btns->addWidget(ok);
    btns->addWidget(cancel);

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(tabs, 1);
    lay->addLayout(btns);
    resize(620, 560);
}

void SettingsDialog::browseDir() {
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择保存目录"), m_dir->text());
    if (!d.isEmpty())
        m_dir->setText(d);
}

void SettingsDialog::browseRecordDir() {
    QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择录屏目录"), m_recordDir->text());
    if (!d.isEmpty())
        m_recordDir->setText(d);
}

void SettingsDialog::applyAndClose() {
    persist();
#ifdef Q_OS_WIN
    if (m_autoStart) {
        QString err;
        if (!Settings::applyAutoStart(m_autoStart->isChecked(), &err) && m_autoStart->isChecked())
            QMessageBox::warning(this, QStringLiteral("SnipShot"),
                                 QStringLiteral("创建开机启动快捷方式失败：") + err);
    }
#endif
    accept();
}

void SettingsDialog::persist() {
    Settings st = Settings::load();
    st.apiBase = m_base->text().trimmed();
    st.apiKey = m_key->text().trimmed();
    st.model = m_model->text().trimmed();
    st.translateTarget = m_translateTarget->currentText();
    st.saveDir = m_dir->text().trimmed();
    st.hkSnip = m_hkSnip->binding();
    st.hkPin = m_hkPin->binding();
    st.hkTogglePins = m_hkToggle->binding();
    st.hkTranslate = m_hkTranslate->binding();
    st.hkOcr = m_hkOcr->binding();
    st.hkClipboard = m_hkClipboard->binding();
    st.hkRecord = m_hkRecord->binding();
    st.hkSnipEnabled = m_hkSnipEnabled->isChecked();
    st.hkPinEnabled = m_hkPinEnabled->isChecked();
    st.hkTogglePinsEnabled = m_hkToggleEnabled->isChecked();
    st.hkTranslateEnabled = m_hkTranslateEnabled->isChecked();
    st.hkOcrEnabled = m_hkOcrEnabled->isChecked();
    st.hkClipboardEnabled = m_hkClipboardEnabled->isChecked();
    st.hkRecordEnabled = m_hkRecordEnabled->isChecked();
    st.pinBorder = m_pinBorder->isChecked();
    st.clipboardEnabled = m_clipEnabled->isChecked();
    st.clipboardMax = m_clipMax->value();
    st.recordingEnabled = m_recEnabled->isChecked();
    st.recordFps = m_recFps->value();
    st.recordCursor = m_recCursor->isChecked();
    st.recordDir = m_recordDir->text().trimmed();
#ifdef Q_OS_WIN
    st.ffmpegPath = m_ffmpeg->text().trimmed();
    if (m_autoStart)
        st.autoStart = m_autoStart->isChecked();
#endif
    st.save();
}

void SettingsDialog::checkHotkey(HotkeyEdit* edit, const QString& text) {
#ifdef Q_OS_WIN
    if (text.trimmed().isEmpty())
        return;
    QString err;
    if (Hotkeys::testBinding(text, &err))
        return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("快捷键冲突"));
    box.setText(QStringLiteral("%1 已被其他程序占用（%2）。").arg(text, err));
    box.setInformativeText(QStringLiteral("换一个快捷键，还是仍然保存？"));
    auto* retry = box.addButton(QStringLiteral("重新录入"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("仍然保存"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == retry)
        edit->startCapture();
#else
    Q_UNUSED(edit);
    Q_UNUSED(text);
#endif
}

void SettingsDialog::registerHotkeys() {
    persist();
    Settings st = Settings::load();
#ifdef Q_OS_WIN
    QStringList problems;
    const bool ok = Hotkeys::reinstallWindows(st, &problems);
    if (ok)
        QMessageBox::information(this, QStringLiteral("SnipShot"),
                                 QStringLiteral("全局快捷键已按当前设置生效。"));
    else
        QMessageBox::warning(this, QStringLiteral("SnipShot"),
                             QStringLiteral("部分快捷键未能注册：\n") + problems.join('\n'));
#else
    if (Hotkeys::installGnome(st))
        QMessageBox::information(this, QStringLiteral("SnipShot"),
                                 QStringLiteral("已注册到 GNOME 自定义快捷键。\n可在 设置→键盘→自定义快捷键 里继续修改。"));
    else
        QMessageBox::warning(this, QStringLiteral("SnipShot"),
                             QStringLiteral("注册失败，请手动在 GNOME 设置→键盘→自定义快捷键 里添加。"));
#endif
}
