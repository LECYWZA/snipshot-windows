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
    tabs->addTab(apiPage, QStringLiteral("API / 保存"));

    // --- 快捷键 ---
    auto* keysPage = new QWidget;
    auto* keysForm = new QFormLayout(keysPage);
    m_hkSnip = new QLineEdit(st.hkSnip);
    keysForm->addRow(QStringLiteral("截图:"), m_hkSnip);
    m_hkPin = new QLineEdit(st.hkPin);
    keysForm->addRow(QStringLiteral("固定贴图:"), m_hkPin);
    m_hkToggle = new QLineEdit(st.hkTogglePins);
    keysForm->addRow(QStringLiteral("隐藏/显示贴图:"), m_hkToggle);
    m_hkTranslate = new QLineEdit(st.hkTranslate);
    keysForm->addRow(QStringLiteral("截屏翻译:"), m_hkTranslate);
    m_hkOcr = new QLineEdit(st.hkOcr);
    keysForm->addRow(QStringLiteral("文字提取:"), m_hkOcr);
    m_hkClipboard = new QLineEdit(st.hkClipboard);
    keysForm->addRow(QStringLiteral("剪贴板历史:"), m_hkClipboard);
    m_hkRecord = new QLineEdit(st.hkRecord);
    keysForm->addRow(QStringLiteral("开始/停止录屏:"), m_hkRecord);
#ifdef Q_OS_WIN
    const QString hintText = QStringLiteral(
        "Windows 下由程序直接注册全局热键（RegisterHotKey）。\n"
        "若提示被占用，换一个组合即可；修改后点“应用”自动生效。");
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
    Settings st = Settings::load();
    st.apiBase = m_base->text().trimmed();
    st.apiKey = m_key->text().trimmed();
    st.model = m_model->text().trimmed();
    st.translateTarget = m_translateTarget->currentText();
    st.saveDir = m_dir->text().trimmed();
    st.hkSnip = m_hkSnip->text().trimmed();
    st.hkPin = m_hkPin->text().trimmed();
    st.hkTogglePins = m_hkToggle->text().trimmed();
    st.hkTranslate = m_hkTranslate->text().trimmed();
    st.hkOcr = m_hkOcr->text().trimmed();
    st.hkClipboard = m_hkClipboard->text().trimmed();
    st.hkRecord = m_hkRecord->text().trimmed();
    st.pinBorder = m_pinBorder->isChecked();
    st.clipboardEnabled = m_clipEnabled->isChecked();
    st.clipboardMax = m_clipMax->value();
    st.recordingEnabled = m_recEnabled->isChecked();
    st.recordFps = m_recFps->value();
    st.recordCursor = m_recCursor->isChecked();
    st.recordDir = m_recordDir->text().trimmed();
#ifdef Q_OS_WIN
    st.ffmpegPath = m_ffmpeg->text().trimmed();
#endif
    st.save();
    accept();
}

void SettingsDialog::registerHotkeys() {
    applyAndClose();
    Settings st = Settings::load();
#ifdef Q_OS_WIN
    QStringList problems;
    const bool ok = Hotkeys::reinstallWindows(st, &problems);
    if (ok)
        QMessageBox::information(this, QStringLiteral("SnipShot"),
                                 QStringLiteral("全局快捷键已生效。"));
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
