#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QInputDialog>
#include <QDirIterator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_renderer = new Renderer(this);

    connect(m_renderer, &Renderer::started, this, &MainWindow::onRenderStarted);
    connect(m_renderer, &Renderer::finished, this, &MainWindow::onRenderFinished);
    connect(m_renderer, &Renderer::progress, this, &MainWindow::onRenderProgress);
    connect(m_renderer, &Renderer::error, this, &MainWindow::onRenderError);

    setupUi();
    loadConfig();

    setWindowTitle("osu! Replay Renderer");
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    QTabWidget *tabs = new QTabWidget(this);
    QWidget *renderTab = new QWidget(tabs);
    QVBoxLayout *renderLayout = new QVBoxLayout(renderTab);
    tabs->addTab(renderTab, "Render");
    QWidget *videoTab = new QWidget(tabs);
    QVBoxLayout *videoLayout = new QVBoxLayout(videoTab);
    tabs->addTab(videoTab, "Video");
    QWidget *timelineTab = new QWidget(tabs);
    QVBoxLayout *timelineLayout = new QVBoxLayout(timelineTab);
    tabs->addTab(timelineTab, "Timeline & effects");
    QWidget *hudTab = new QWidget(tabs);
    QVBoxLayout *hudLayout = new QVBoxLayout(hudTab);
    tabs->addTab(hudTab, "HUD");
    QWidget *playfieldTab = new QWidget(tabs);
    QVBoxLayout *playfieldLayout = new QVBoxLayout(playfieldTab);
    tabs->addTab(playfieldTab, "Playfield");
    QWidget *audioTab = new QWidget(tabs);
    QFormLayout *audioLayout = new QFormLayout(audioTab);
    tabs->addTab(audioTab, "Audio");
    mainLayout->addWidget(tabs);

    // ===== Replay selection =====
    QGroupBox *replayGroup = new QGroupBox("Replay & Output", this);
    QFormLayout *replayLayout = new QFormLayout(replayGroup);

    m_danserEdit = new QLineEdit(this);
    m_danserEdit->setPlaceholderText("path/to/danser-cli");
    QPushButton *browseDanserBtn = new QPushButton("Browse…", this);
    connect(browseDanserBtn, &QPushButton::clicked, this, &MainWindow::browseDanser);
    QHBoxLayout *danserRow = new QHBoxLayout;
    danserRow->addWidget(m_danserEdit);
    danserRow->addWidget(browseDanserBtn);
    replayLayout->addRow("danser-cli:", danserRow);

    m_replayEdit = new QLineEdit(this);
    m_replayEdit->setPlaceholderText("path/to/replay.osr");

    QPushButton *browseReplayBtn = new QPushButton("Browse…", this);
    browseReplayBtn->setObjectName("browseReplayBtn");
    connect(browseReplayBtn, &QPushButton::clicked, this, &MainWindow::browseReplay);

    QHBoxLayout *replayRow = new QHBoxLayout;
    replayRow->addWidget(m_replayEdit);
    replayRow->addWidget(browseReplayBtn);
    replayLayout->addRow("Replay file:", replayRow);

    m_replayDirEdit = new QLineEdit(this);
    m_replayDirEdit->setPlaceholderText("Optional: folder with .osr replays");
    QPushButton *browseReplayDirBtn = new QPushButton("Browse…", this);
    connect(browseReplayDirBtn, &QPushButton::clicked, this, &MainWindow::browseReplayDirectory);
    QHBoxLayout *replayDirRow = new QHBoxLayout;
    replayDirRow->addWidget(m_replayDirEdit);
    replayDirRow->addWidget(browseReplayDirBtn);
    replayLayout->addRow("Replay folder:", replayDirRow);

    m_replayCombo = new QComboBox(this);
    m_replayCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_replayCombo->addItem("Choose a replay folder first…");
    connect(m_replayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const QString path = m_replayCombo->itemData(index).toString();
        if (!path.isEmpty()) m_replayEdit->setText(path);
    });
    replayLayout->addRow("From folder:", m_replayCombo);

    m_outputEdit = new QLineEdit(this);
    m_outputEdit->setPlaceholderText("output.mp4");

    QPushButton *browseOutputBtn = new QPushButton("Browse…", this);
    browseOutputBtn->setObjectName("browseOutputBtn");
    connect(browseOutputBtn, &QPushButton::clicked, this, &MainWindow::browseOutput);

    QHBoxLayout *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputEdit);
    outputRow->addWidget(browseOutputBtn);
    replayLayout->addRow("Output file:", outputRow);

    // ===== Settings =====
    QGroupBox *settingsGroup = new QGroupBox("Settings", this);
    QFormLayout *settingsLayout = new QFormLayout(settingsGroup);

    // Skin path
    m_skinEdit = new QLineEdit(this);
    m_skinEdit->setPlaceholderText("path/to/skin (optional)");
    QPushButton *browseSkinBtn = new QPushButton("Browse…", this);
    connect(browseSkinBtn, &QPushButton::clicked, this, &MainWindow::browseSkin);
    QHBoxLayout *skinRow = new QHBoxLayout;
    skinRow->addWidget(m_skinEdit);
    skinRow->addWidget(browseSkinBtn);
    settingsLayout->addRow("Skin:", skinRow);

    // FPS
    m_fpsSpin = new QSpinBox(this);
    m_fpsSpin->setRange(30, 240);
    m_fpsSpin->setValue(60);
    m_fpsSpin->setSuffix(" fps");
    settingsLayout->addRow("FPS:", m_fpsSpin);

    // Resolution presets
    m_resPresets = new QComboBox(this);
    m_resPresets->addItem("720p (1280x720)", QVariant("1280x720"));
    m_resPresets->addItem("1080p (1920x1080)", QVariant("1920x1080"));
    m_resPresets->addItem("1440p (2560x1440)", QVariant("2560x1440"));
    m_resPresets->addItem("4K (3840x2160)", QVariant("3840x2160"));
    m_resPresets->addItem("Custom", QVariant("custom"));
    connect(m_resPresets, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        bool custom = (m_resPresets->itemData(idx).toString() == "custom");
        m_widthSpin->setEnabled(custom);
        m_heightSpin->setEnabled(custom);
        if (!custom) {
            QString preset = m_resPresets->itemData(idx).toString();
            QStringList parts = preset.split('x');
            m_widthSpin->setValue(parts[0].toInt());
            m_heightSpin->setValue(parts[1].toInt());
        }
    });
    settingsLayout->addRow("Resolution preset:", m_resPresets);

    // Custom resolution width/height
    m_customResCheck = new QCheckBox("Custom resolution", this);
    m_customResCheck->setChecked(false);
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 7680);
    m_widthSpin->setValue(1280);
    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(1, 4320);
    m_heightSpin->setValue(720);
    QHBoxLayout *resRow = new QHBoxLayout;
    resRow->addWidget(m_widthSpin);
    resRow->addWidget(new QLabel("x", this));
    resRow->addWidget(m_heightSpin);
    resRow->addWidget(m_customResCheck);
    settingsLayout->addRow("Resolution:", resRow);

    m_widthSpin->setEnabled(false);
    m_heightSpin->setEnabled(false);

    // Connect custom checkbox
    connect(m_customResCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_widthSpin->setEnabled(checked);
        m_heightSpin->setEnabled(checked);
    });

    // Cursor scale
    m_cursorScaleSpin = new QDoubleSpinBox(this);
    m_cursorScaleSpin->setRange(0.1, 5.0);
    m_cursorScaleSpin->setSingleStep(0.1);
    m_cursorScaleSpin->setValue(1.0);
    m_cursorScaleSpin->setDecimals(2);
    settingsLayout->addRow("Cursor scale:", m_cursorScaleSpin);

    m_encoderCombo = new QComboBox(this);
    m_encoderCombo->addItem("H.264 (CPU, recommended)", "libx264");
    m_encoderCombo->addItem("H.265 (CPU)", "libx265");
    m_encoderCombo->addItem("H.264 NVIDIA", "h264_nvenc");
    settingsLayout->addRow("Encoder:", m_encoderCombo);

    m_crfSpin = new QSpinBox(this);
    m_crfSpin->setRange(0, 51);
    m_crfSpin->setValue(14);
    m_crfSpin->setToolTip("Меньше значение — выше качество и больше размер файла. Работает для H.264.");
    settingsLayout->addRow("H.264 quality (CRF):", m_crfSpin);

    QGroupBox *advancedGroup = new QGroupBox("Timeline & effects", this);
    QFormLayout *advancedLayout = new QFormLayout(advancedGroup);
    m_skipIntroCheck = new QCheckBox("Skip to first object", this);
    advancedLayout->addRow("Intro:", m_skipIntroCheck);
    m_startTimeSpin = new QDoubleSpinBox(this);
    m_startTimeSpin->setRange(0.0, 86400.0);
    m_startTimeSpin->setDecimals(3);
    m_startTimeSpin->setSuffix(" s");
    m_startTimeSpin->setToolTip("0 — начать с начала реплея");
    advancedLayout->addRow("Start at:", m_startTimeSpin);
    m_endTimeSpin = new QDoubleSpinBox(this);
    m_endTimeSpin->setRange(0.0, 86400.0);
    m_endTimeSpin->setDecimals(3);
    m_endTimeSpin->setSuffix(" s");
    m_endTimeSpin->setToolTip("0 — рендерить до конца реплея");
    advancedLayout->addRow("End at:", m_endTimeSpin);
    m_audioOffsetSpin = new QSpinBox(this);
    m_audioOffsetSpin->setRange(-5000, 5000);
    m_audioOffsetSpin->setSuffix(" ms");
    advancedLayout->addRow("Audio offset:", m_audioOffsetSpin);
    m_motionBlurCheck = new QCheckBox("Enable motion blur", this);
    advancedLayout->addRow("Motion blur:", m_motionBlurCheck);
    m_blurSamplesSpin = new QSpinBox(this);
    m_blurSamplesSpin->setRange(1, 64);
    m_blurSamplesSpin->setValue(16);
    m_blurSamplesSpin->setEnabled(false);
    connect(m_motionBlurCheck, &QCheckBox::toggled, m_blurSamplesSpin, &QSpinBox::setEnabled);
    advancedLayout->addRow("Blur samples:", m_blurSamplesSpin);

    // ===== Controls =====
    QGroupBox *controlGroup = new QGroupBox("Control", this);
    QHBoxLayout *controlLayout = new QHBoxLayout(controlGroup);

    m_startBtn = new QPushButton("Start Render", this);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::startRender);

    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancelRender);

    controlLayout->addWidget(m_startBtn);
    controlLayout->addWidget(m_cancelBtn);

    // ===== Progress =====
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // ===== Log =====
    QGroupBox *logGroup = new QGroupBox("Log", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);

    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(150);
    m_logEdit->setPlaceholderText("Render output will appear here...");

    logLayout->addWidget(m_logEdit);

    // Add to main layout
    renderLayout->addWidget(replayGroup);
    videoLayout->addWidget(settingsGroup);
    videoLayout->addStretch();
    timelineLayout->addWidget(advancedGroup);
    timelineLayout->addStretch();
    renderLayout->addWidget(controlGroup);
    renderLayout->addWidget(m_progressBar);
    renderLayout->addWidget(m_statusLabel);
    renderLayout->addWidget(logGroup);
    renderLayout->addStretch();

    QGroupBox *hudGroup = new QGroupBox("Visible HUD elements", hudTab);
    QGridLayout *hudGrid = new QGridLayout(hudGroup);
    const QList<QPair<QString, QCheckBox **>> hudItems = {{"Score / accuracy", &m_hudScore}, {"HP bar", &m_hudHp}, {"Combo", &m_hudCombo}, {"PP counter", &m_hudPp}, {"Error meter", &m_hudError}, {"Key overlay", &m_hudKeys}, {"Mods", &m_hudMods}, {"Leaderboard", &m_hudBoard}, {"Hit counter", &m_hudHits}, {"Strain graph", &m_hudStrain}};
    for (int i = 0; i < hudItems.size(); ++i) { *hudItems[i].second = new QCheckBox(hudItems[i].first, hudGroup); (*hudItems[i].second)->setChecked(true); hudGrid->addWidget(*hudItems[i].second, i / 2, i % 2); }
    hudLayout->addWidget(hudGroup); hudLayout->addStretch();

    QScrollArea *playScroll = new QScrollArea(playfieldTab); playScroll->setWidgetResizable(true);
    QWidget *playContent = new QWidget(playScroll); QFormLayout *playForm = new QFormLayout(playContent);
    m_backgroundDim = new QDoubleSpinBox(playContent); m_backgroundDim->setRange(0, 1); m_backgroundDim->setSingleStep(.05); m_backgroundDim->setValue(.95); playForm->addRow("Background dim:", m_backgroundDim);
    const QList<QPair<QString, QCheckBox **>> playItems = {{"Load storyboard", &m_storyboardCheck}, {"Load background video", &m_backgroundVideoCheck}, {"Background parallax", &m_parallaxCheck}, {"Background blur", &m_backgroundBlurCheck}, {"Bloom", &m_bloomCheck}, {"Draw objects", &m_drawObjectsCheck}, {"Approach circles", &m_approachCheck}, {"Combo numbers", &m_numbersCheck}, {"Follow points", &m_followPointsCheck}, {"Hit animations", &m_hitAnimationsCheck}, {"Scale to the beat", &m_beatScaleCheck}, {"Slider snaking in", &m_snakeInCheck}, {"Slider snaking out", &m_snakeOutCheck}};
    for (const auto &item : playItems) { *item.second = new QCheckBox(playContent); (*item.second)->setChecked(item.first != "Load background video" && item.first != "Background blur" && item.first != "Bloom"); playForm->addRow(item.first + ":", *item.second); }
    playScroll->setWidget(playContent); playfieldLayout->addWidget(playScroll);

    m_musicVolumeSlider = new QSlider(Qt::Horizontal, audioTab);
    m_musicVolumeSlider->setRange(0, 100); m_musicVolumeSlider->setValue(50);
    m_musicVolumeSlider->setTickPosition(QSlider::TicksBelow); m_musicVolumeSlider->setTickInterval(10);
    audioLayout->addRow("Music volume:", m_musicVolumeSlider);
    m_hitsoundVolumeSlider = new QSlider(Qt::Horizontal, audioTab);
    m_hitsoundVolumeSlider->setRange(0, 100); m_hitsoundVolumeSlider->setValue(50);
    m_hitsoundVolumeSlider->setTickPosition(QSlider::TicksBelow); m_hitsoundVolumeSlider->setTickInterval(10);
    audioLayout->addRow("Hitsound volume:", m_hitsoundVolumeSlider);

    QGroupBox *presetGroup = new QGroupBox("Quick presets", videoTab);
    QFormLayout *presetLayout = new QFormLayout(presetGroup);
    m_presetCombo = new QComboBox(presetGroup);
    m_presetCombo->addItem("Custom (keep current settings)");
    m_presetCombo->addItem("Clean gameplay");
    m_presetCombo->addItem("Streamer HUD");
    m_presetCombo->addItem("Cinematic");
    presetLayout->addRow("Preset:", m_presetCombo);
    QHBoxLayout *presetButtons = new QHBoxLayout;
    QPushButton *savePresetButton = new QPushButton("Save current…", presetGroup);
    QPushButton *deletePresetButton = new QPushButton("Delete", presetGroup);
    connect(savePresetButton, &QPushButton::clicked, this, &MainWindow::saveUserPreset);
    connect(deletePresetButton, &QPushButton::clicked, this, &MainWindow::deleteUserPreset);
    presetButtons->addWidget(savePresetButton); presetButtons->addWidget(deletePresetButton);
    presetLayout->addRow(presetButtons);
    videoLayout->insertWidget(1, presetGroup);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 4) { applyPreset(m_userPresets.at(index - 4).toObject()["settings"].toObject()); return; }
        if (index == 0) return;
        const bool cinematic = index == 3;
        const bool clean = index == 1 || cinematic;
        m_hudScore->setChecked(!clean); m_hudHp->setChecked(!clean); m_hudCombo->setChecked(!clean);
        m_hudPp->setChecked(index == 2); m_hudError->setChecked(index == 2); m_hudKeys->setChecked(index == 2);
        m_hudMods->setChecked(!clean); m_hudBoard->setChecked(index == 2); m_hudHits->setChecked(false); m_hudStrain->setChecked(false);
        m_backgroundDim->setValue(cinematic ? .65 : (index == 1 ? .8 : .9));
        m_backgroundBlurCheck->setChecked(cinematic); m_bloomCheck->setChecked(cinematic);
        m_storyboardCheck->setChecked(true); m_drawObjectsCheck->setChecked(true);
    });

    QWidget *skinsTab = new QWidget(tabs);
    QVBoxLayout *skinsLayout = new QVBoxLayout(skinsTab);
    QLabel *skinsHint = new QLabel("Добавьте папки скинов. Выбранный скин используется для следующего рендера.", skinsTab);
    skinsHint->setWordWrap(true);
    skinsLayout->addWidget(skinsHint);
    m_skinList = new QListWidget(skinsTab);
    m_skinList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_skinList, &QListWidget::itemClicked, this, &MainWindow::selectSkin);
    skinsLayout->addWidget(m_skinList);
    QHBoxLayout *skinButtons = new QHBoxLayout;
    QPushButton *addSkinButton = new QPushButton("Add skin folder…", skinsTab);
    QPushButton *removeSkinButton = new QPushButton("Remove selected", skinsTab);
    connect(addSkinButton, &QPushButton::clicked, this, &MainWindow::addSkin);
    connect(removeSkinButton, &QPushButton::clicked, this, &MainWindow::removeSkin);
    skinButtons->addWidget(addSkinButton);
    skinButtons->addWidget(removeSkinButton);
    skinButtons->addStretch();
    skinsLayout->addLayout(skinButtons);
    tabs->addTab(skinsTab, "Skins");

    resize(640, 520);
    setMinimumSize(460, 330);
}

QJsonObject MainWindow::capturePreset() const
{
    return {{"fps", m_fpsSpin->value()}, {"width", m_widthSpin->value()}, {"height", m_heightSpin->value()}, {"cursor", m_cursorScaleSpin->value()}, {"encoder", m_encoderCombo->currentData().toString()}, {"crf", m_crfSpin->value()}, {"skin", m_skinEdit->text()}, {"skip", m_skipIntroCheck->isChecked()}, {"start", m_startTimeSpin->value()}, {"end", m_endTimeSpin->value()}, {"offset", m_audioOffsetSpin->value()}, {"motion", m_motionBlurCheck->isChecked()}, {"samples", m_blurSamplesSpin->value()}, {"dim", m_backgroundDim->value()}, {"storyboard", m_storyboardCheck->isChecked()}, {"video", m_backgroundVideoCheck->isChecked()}, {"parallax", m_parallaxCheck->isChecked()}, {"blur", m_backgroundBlurCheck->isChecked()}, {"bloom", m_bloomCheck->isChecked()}, {"objects", m_drawObjectsCheck->isChecked()}, {"approach", m_approachCheck->isChecked()}, {"numbers", m_numbersCheck->isChecked()}, {"follow", m_followPointsCheck->isChecked()}, {"animations", m_hitAnimationsCheck->isChecked()}, {"beat", m_beatScaleCheck->isChecked()}, {"snakeIn", m_snakeInCheck->isChecked()}, {"snakeOut", m_snakeOutCheck->isChecked()}, {"hud", QJsonArray{m_hudScore->isChecked(),m_hudHp->isChecked(),m_hudCombo->isChecked(),m_hudPp->isChecked(),m_hudError->isChecked(),m_hudKeys->isChecked(),m_hudMods->isChecked(),m_hudBoard->isChecked(),m_hudHits->isChecked(),m_hudStrain->isChecked()}}};
}

void MainWindow::applyPreset(const QJsonObject &p)
{
    if (p.contains("fps")) m_fpsSpin->setValue(p["fps"].toInt()); if (p.contains("width")) m_widthSpin->setValue(p["width"].toInt()); if (p.contains("height")) m_heightSpin->setValue(p["height"].toInt()); if (p.contains("cursor")) m_cursorScaleSpin->setValue(p["cursor"].toDouble());
    int encoder = m_encoderCombo->findData(p["encoder"].toString()); if (encoder >= 0) m_encoderCombo->setCurrentIndex(encoder); m_crfSpin->setValue(p["crf"].toInt(14)); m_skinEdit->setText(p["skin"].toString()); m_skipIntroCheck->setChecked(p["skip"].toBool()); m_startTimeSpin->setValue(p["start"].toDouble()); m_endTimeSpin->setValue(p["end"].toDouble()); m_audioOffsetSpin->setValue(p["offset"].toInt()); m_motionBlurCheck->setChecked(p["motion"].toBool()); m_blurSamplesSpin->setValue(p["samples"].toInt(16));
    m_backgroundDim->setValue(p["dim"].toDouble(.95)); m_storyboardCheck->setChecked(p["storyboard"].toBool()); m_backgroundVideoCheck->setChecked(p["video"].toBool()); m_parallaxCheck->setChecked(p["parallax"].toBool()); m_backgroundBlurCheck->setChecked(p["blur"].toBool()); m_bloomCheck->setChecked(p["bloom"].toBool()); m_drawObjectsCheck->setChecked(p["objects"].toBool()); m_approachCheck->setChecked(p["approach"].toBool()); m_numbersCheck->setChecked(p["numbers"].toBool()); m_followPointsCheck->setChecked(p["follow"].toBool()); m_hitAnimationsCheck->setChecked(p["animations"].toBool()); m_beatScaleCheck->setChecked(p["beat"].toBool()); m_snakeInCheck->setChecked(p["snakeIn"].toBool()); m_snakeOutCheck->setChecked(p["snakeOut"].toBool());
    const QJsonArray hud = p["hud"].toArray(); const QList<QCheckBox *> widgets = {m_hudScore,m_hudHp,m_hudCombo,m_hudPp,m_hudError,m_hudKeys,m_hudMods,m_hudBoard,m_hudHits,m_hudStrain}; for (int i=0;i<hud.size() && i<widgets.size();++i) widgets[i]->setChecked(hud[i].toBool());
}

void MainWindow::refreshUserPresets()
{
    while (m_presetCombo->count() > 4) m_presetCombo->removeItem(4);
    for (const QJsonValue &value : m_userPresets) m_presetCombo->addItem(value.toObject()["name"].toString());
}

void MainWindow::saveUserPreset()
{
    bool ok = false; const QString name = QInputDialog::getText(this, "Save preset", "Preset name:", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    m_userPresets.append(QJsonObject{{"name", name}, {"settings", capturePreset()}}); refreshUserPresets(); m_presetCombo->setCurrentIndex(m_presetCombo->count()-1); saveConfig();
}

void MainWindow::deleteUserPreset()
{
    const int index = m_presetCombo->currentIndex() - 4; if (index < 0 || index >= m_userPresets.size()) return;
    m_userPresets.removeAt(index); refreshUserPresets(); m_presetCombo->setCurrentIndex(0); saveConfig();
}

void MainWindow::setDanserPath(const QString &path)
{
    m_renderer->setDanserPath(path);
    m_danserEdit->setText(path);
}

void MainWindow::browseDanser()
{
    const QString path = QFileDialog::getOpenFileName(this, "Select danser-cli", "", "Executable files (*)");
    if (!path.isEmpty()) {
        m_danserEdit->setText(path);
        m_renderer->setDanserPath(path);
    }
}

void MainWindow::browseReplay()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Replay", "", "osu! Replays (*.osr)");
    if (!path.isEmpty()) {
        m_replayEdit->setText(path);
    }
}

void MainWindow::browseReplayDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, "Select Replay Folder", m_replayDirEdit->text());
    if (path.isEmpty()) return;
    m_replayDirEdit->setText(path);
    refreshReplayList();
    saveConfig();
}

void MainWindow::refreshReplayList()
{
    const QString directory = m_replayDirEdit->text();
    m_replayCombo->blockSignals(true);
    m_replayCombo->clear();
    if (!QFileInfo(directory).isDir()) {
        m_replayCombo->addItem("Replay folder not found");
    } else {
        QDirIterator iterator(directory, {"*.osr", "*.OSR"}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QFileInfo file(path);
            m_replayCombo->addItem(file.completeBaseName(), path);
        }
        if (m_replayCombo->count() == 0) m_replayCombo->addItem("No .osr files found");
    }
    m_replayCombo->blockSignals(false);
}

void MainWindow::browseSkin()
{
    QString path = QFileDialog::getExistingDirectory(this, "Select Skin Directory");
    if (!path.isEmpty()) {
        m_skinEdit->setText(path);
    }
}

void MainWindow::addSkin()
{
    const QString path = QFileDialog::getExistingDirectory(this, "Select Skin Directory");
    if (path.isEmpty()) return;

    for (int i = 0; i < m_skinList->count(); ++i) {
        if (m_skinList->item(i)->data(Qt::UserRole).toString() == path) {
            m_skinList->setCurrentRow(i);
            selectSkin(m_skinList->item(i));
            return;
        }
    }
    auto *item = new QListWidgetItem(QFileInfo(path).fileName(), m_skinList);
    item->setData(Qt::UserRole, path);
    m_skinList->setCurrentItem(item);
    selectSkin(item);
    saveConfig();
}

void MainWindow::removeSkin()
{
    const int row = m_skinList->currentRow();
    if (row < 0) return;
    const QString removedPath = m_skinList->item(row)->data(Qt::UserRole).toString();
    delete m_skinList->takeItem(row);
    if (m_skinEdit->text() == removedPath) m_skinEdit->clear();
    saveConfig();
}

void MainWindow::selectSkin(QListWidgetItem *item)
{
    if (item) m_skinEdit->setText(item->data(Qt::UserRole).toString());
}

void MainWindow::browseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Output", "", "MP4 Video (*.mp4);;AVI Video (*.avi)");
    if (!path.isEmpty()) {
        m_outputEdit->setText(path);
    }
}

void MainWindow::startRender()
{
    if (m_replayEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a replay file!");
        return;
    }
    if (m_danserEdit->text().isEmpty() || !QFileInfo::exists(m_danserEdit->text())) {
        QMessageBox::warning(this, "Warning", "Please select a valid danser-cli executable!");
        return;
    }
    if (m_outputEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select an output file!");
        return;
    }

    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting...");
    m_logEdit->clear();
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    saveConfig();
    m_renderer->setDanserPath(m_danserEdit->text());
    m_renderer->setReplayPath(m_replayEdit->text());
    m_renderer->setOutputPath(m_outputEdit->text());
    m_renderer->setSkinPath(m_skinEdit->text());
    m_renderer->setFps(m_fpsSpin->value());
    m_renderer->setResolution(m_widthSpin->value(), m_heightSpin->value());
    m_renderer->setCursorScale(m_cursorScaleSpin->value());
    m_renderer->setEncoder(m_encoderCombo->currentData().toString());
    m_renderer->setCrf(m_crfSpin->value());
    m_renderer->setSkipIntro(m_skipIntroCheck->isChecked());
    m_renderer->setStartTime(m_startTimeSpin->value());
    m_renderer->setEndTime(m_endTimeSpin->value());
    m_renderer->setAudioOffset(m_audioOffsetSpin->value());
    m_renderer->setMotionBlur(m_motionBlurCheck->isChecked(), m_blurSamplesSpin->value());
    m_renderer->setVolumes(m_musicVolumeSlider->value() / 100.0, m_hitsoundVolumeSlider->value() / 100.0);
    m_renderer->setHudOptions({{"score",m_hudScore->isChecked()},{"hp",m_hudHp->isChecked()},{"combo",m_hudCombo->isChecked()},{"pp",m_hudPp->isChecked()},{"error",m_hudError->isChecked()},{"keys",m_hudKeys->isChecked()},{"mods",m_hudMods->isChecked()},{"leaderboard",m_hudBoard->isChecked()},{"hitCounter",m_hudHits->isChecked()},{"strain",m_hudStrain->isChecked()}});
    m_renderer->setPlayfieldOptions({{"dim",m_backgroundDim->value()},{"storyboard",m_storyboardCheck->isChecked()},{"video",m_backgroundVideoCheck->isChecked()},{"parallax",m_parallaxCheck->isChecked()},{"blur",m_backgroundBlurCheck->isChecked()},{"bloom",m_bloomCheck->isChecked()},{"objects",m_drawObjectsCheck->isChecked()},{"approach",m_approachCheck->isChecked()},{"numbers",m_numbersCheck->isChecked()},{"follow",m_followPointsCheck->isChecked()},{"animations",m_hitAnimationsCheck->isChecked()},{"beatScale",m_beatScaleCheck->isChecked()},{"snakeIn",m_snakeInCheck->isChecked()},{"snakeOut",m_snakeOutCheck->isChecked()}});
    m_renderer->start();
}

void MainWindow::cancelRender()
{
    m_renderer->cancel();
}

void MainWindow::onRenderStarted()
{
    m_statusLabel->setText("Danser started...");
    m_logEdit->setPlainText("=== Render started ===");
}

void MainWindow::onRenderFinished(int exitCode)
{
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setValue(100);
    m_statusLabel->setText(exitCode == 0 ? "Completed!" : "Failed with code " + QString::number(exitCode));
    m_logEdit->appendPlainText("=== Render finished (code " + QString::number(exitCode) + ") ===");
}

void MainWindow::onRenderProgress(int percent, const QString &status)
{
    if (percent >= 0) {
        m_progressBar->setValue(percent);
    }
    if (!status.isEmpty()) {
        m_logEdit->appendPlainText(status);
    }
    m_statusLabel->setText(status);
}

void MainWindow::onRenderError(const QString &msg)
{
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_statusLabel->setText("Error: " + msg);
    m_logEdit->appendPlainText("ERROR: " + msg);
}

void MainWindow::toggleCustomResolution(bool enabled)
{
    m_widthSpin->setEnabled(enabled);
    m_heightSpin->setEnabled(enabled);
}

void MainWindow::saveConfig()
{
    QJsonObject config;
    config["fps"] = m_fpsSpin->value();
    config["width"] = m_widthSpin->value();
    config["height"] = m_heightSpin->value();
    config["cursorScale"] = m_cursorScaleSpin->value();
    config["skinPath"] = m_skinEdit->text();
    config["outputPath"] = m_outputEdit->text();
    config["replayDirectory"] = m_replayDirEdit->text();
    config["danserPath"] = m_danserEdit->text();
    config["encoder"] = m_encoderCombo->currentData().toString();
    config["crf"] = m_crfSpin->value();
    config["skipIntro"] = m_skipIntroCheck->isChecked();
    config["startTime"] = m_startTimeSpin->value();
    config["endTime"] = m_endTimeSpin->value();
    config["audioOffset"] = m_audioOffsetSpin->value();
    config["motionBlur"] = m_motionBlurCheck->isChecked();
    config["blurSamples"] = m_blurSamplesSpin->value();
    config["musicVolume"] = m_musicVolumeSlider->value();
    config["hitsoundVolume"] = m_hitsoundVolumeSlider->value();
    QJsonArray skins;
    for (int i = 0; i < m_skinList->count(); ++i) {
        QJsonObject skin;
        skin["name"] = m_skinList->item(i)->text();
        skin["path"] = m_skinList->item(i)->data(Qt::UserRole).toString();
        skins.append(skin);
    }
    config["skins"] = skins;
    config["presets"] = m_userPresets;

    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/osu_render/config.json";
    QDir dir(QFileInfo(configPath).absoluteDir());
    dir.mkpath(dir.path());

    QFile file(configPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(config).toJson());
        file.close();
    }
}

void MainWindow::loadConfig()
{
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/osu_render/config.json";
    QFile file(configPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject config = doc.object();
    if (config.contains("fps")) m_fpsSpin->setValue(config["fps"].toInt());
    if (config.contains("width")) m_widthSpin->setValue(config["width"].toInt());
    if (config.contains("height")) m_heightSpin->setValue(config["height"].toInt());
    if (config.contains("cursorScale")) m_cursorScaleSpin->setValue(config["cursorScale"].toDouble());
    if (config.contains("skinPath")) m_skinEdit->setText(config["skinPath"].toString());
    if (config.contains("outputPath")) m_outputEdit->setText(config["outputPath"].toString());
    if (config.contains("replayDirectory")) {
        m_replayDirEdit->setText(config["replayDirectory"].toString());
        refreshReplayList();
    }
    if (config.contains("danserPath")) setDanserPath(config["danserPath"].toString());
    if (config.contains("encoder")) {
        const int index = m_encoderCombo->findData(config["encoder"].toString());
        if (index >= 0) m_encoderCombo->setCurrentIndex(index);
    }
    if (config.contains("crf")) m_crfSpin->setValue(config["crf"].toInt());
    if (config.contains("skipIntro")) m_skipIntroCheck->setChecked(config["skipIntro"].toBool());
    if (config.contains("startTime")) m_startTimeSpin->setValue(config["startTime"].toDouble());
    if (config.contains("endTime")) m_endTimeSpin->setValue(config["endTime"].toDouble());
    if (config.contains("audioOffset")) m_audioOffsetSpin->setValue(config["audioOffset"].toInt());
    if (config.contains("motionBlur")) m_motionBlurCheck->setChecked(config["motionBlur"].toBool());
    if (config.contains("blurSamples")) m_blurSamplesSpin->setValue(config["blurSamples"].toInt());
    if (config.contains("musicVolume")) m_musicVolumeSlider->setValue(config["musicVolume"].toInt());
    if (config.contains("hitsoundVolume")) m_hitsoundVolumeSlider->setValue(config["hitsoundVolume"].toInt());
    for (const QJsonValue &value : config["skins"].toArray()) {
        const QJsonObject skin = value.toObject();
        const QString path = skin["path"].toString();
        if (QFileInfo(path).isDir()) {
            auto *item = new QListWidgetItem(skin["name"].toString(QFileInfo(path).fileName()), m_skinList);
            item->setData(Qt::UserRole, path);
            if (path == m_skinEdit->text()) m_skinList->setCurrentItem(item);
        }
    }
    m_userPresets = config["presets"].toArray();
    refreshUserPresets();
}
