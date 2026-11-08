#include "Renderer.h"
#include <QProcess>
#include <QStringList>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

Renderer::Renderer(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_fps(60)
    , m_width(1920)
    , m_height(1080)
    , m_cursorScale(1.0)
    , m_encoder("libx264")
    , m_crf(14)
    , m_skipIntro(false)
    , m_startTime(0.0)
    , m_endTime(0.0)
    , m_audioOffset(0)
    , m_motionBlur(false)
    , m_blurSamples(16)
{
    connect(m_process, &QProcess::started, this, &Renderer::onProcessStarted);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Renderer::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &Renderer::onProcessReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &Renderer::onProcessFinished);
    connect(m_process, QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this, &Renderer::onProcessError);
}

Renderer::~Renderer()
{
    if (m_process->state() == QProcess::Running) {
        m_process->kill();
    }
    m_process->waitForFinished();
}

void Renderer::setDanserPath(const QString &path)
{
    m_danserPath = path;
}

void Renderer::setReplayPath(const QString &path)
{
    m_replayPath = path;
}

void Renderer::setSkinPath(const QString &path)
{
    m_skinPath = path;
}

void Renderer::setOutputPath(const QString &path)
{
    m_outputPath = path;
}

void Renderer::setFps(int fps)
{
    m_fps = fps;
}

void Renderer::setResolution(int width, int height)
{
    m_width = width;
    m_height = height;
}

void Renderer::setCursorScale(double scale)
{
    m_cursorScale = scale;
}

void Renderer::setEncoder(const QString &encoder)
{
    m_encoder = encoder;
}

void Renderer::setCrf(int crf)
{
    m_crf = crf;
}

void Renderer::setSkipIntro(bool enabled) { m_skipIntro = enabled; }
void Renderer::setStartTime(double seconds) { m_startTime = seconds; }
void Renderer::setEndTime(double seconds) { m_endTime = seconds; }
void Renderer::setAudioOffset(int milliseconds) { m_audioOffset = milliseconds; }
void Renderer::setMotionBlur(bool enabled, int samples)
{
    m_motionBlur = enabled;
    m_blurSamples = samples;
}
void Renderer::setVolumes(double music, double hitsounds)
{
    m_musicVolume = music;
    m_hitsoundVolume = hitsounds;
}
void Renderer::setHudOptions(const QJsonObject &options) { m_hudOptions = options; }
void Renderer::setPlayfieldOptions(const QJsonObject &options) { m_playfieldOptions = options; }

void Renderer::start()
{
    if (m_danserPath.isEmpty()) {
        emit error("Danser path not set");
        return;
    }
    if (m_replayPath.isEmpty()) {
        emit error("Replay path not set");
        return;
    }
    if (m_outputPath.isEmpty()) {
        emit error("Output path not set");
        return;
    }

    QString profileError;
    if (!prepareDanserProfile(&profileError)) {
        emit error(profileError);
        return;
    }

    buildArgs("osu-render");
    m_process->setWorkingDirectory(QFileInfo(m_danserPath).absolutePath());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->start(m_danserPath, m_args);
}

void Renderer::cancel()
{
    if (m_process->state() == QProcess::Running) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

bool Renderer::prepareDanserProfile(QString *errorMessage)
{
    const QDir danserDir(QFileInfo(m_danserPath).absolutePath());
    const QString settingsDir = danserDir.filePath("settings");
    const QString defaultPath = QDir(settingsDir).filePath("default.json");
    QFile defaultFile(defaultPath);
    if (!defaultFile.open(QIODevice::ReadOnly)) {
        *errorMessage = "Не найден профиль danser: " + defaultPath;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(defaultFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *errorMessage = "Не удалось прочитать default.json: " + parseError.errorString();
        return false;
    }

    QJsonObject profile = document.object();
    const QString songsDirectory = discoverSongsDirectory();
    if (songsDirectory.isEmpty()) {
        *errorMessage = "Не найдена папка Songs. Откройте osu!-winello, скачайте хотя бы одну карту "
                        "или укажите существующую папку Songs в settings/default.json danser.";
        return false;
    }
    QJsonObject general = profile.value("General").toObject();
    general["OsuSongsDir"] = songsDirectory;
    profile["General"] = general;
    QJsonObject audio = profile.value("Audio").toObject();
    audio["MusicVolume"] = m_musicVolume;
    audio["SampleVolume"] = m_hitsoundVolume;
    profile["Audio"] = audio;
    QJsonObject recording = profile.value("Recording").toObject();
    recording["FrameWidth"] = m_width;
    recording["FrameHeight"] = m_height;
    recording["FPS"] = m_fps;
    recording["Encoder"] = m_encoder;
    recording["OutputDir"] = QFileInfo(m_outputPath).absolutePath();
    recording["Container"] = QFileInfo(m_outputPath).suffix().isEmpty()
        ? QString("mp4") : QFileInfo(m_outputPath).suffix().toLower();
    QJsonObject x264 = recording.value("libx264").toObject();
    x264["CRF"] = m_crf;
    recording["libx264"] = x264;
    QJsonObject motionBlur = recording.value("MotionBlur").toObject();
    motionBlur["Enabled"] = m_motionBlur;
    motionBlur["OversampleMultiplier"] = m_blurSamples;
    recording["MotionBlur"] = motionBlur;
    profile["Recording"] = recording;

    QJsonObject skin = profile.value("Skin").toObject();
    // danser keeps its own colour pipeline by default. Enable the selected
    // skin's palette and combo colours so hit objects match the skin as well.
    skin["UseColorsFromSkin"] = true;
    skin["UseBeatmapColors"] = false;
    QJsonObject cursor = skin.value("Cursor").toObject();
    cursor["UseSkinCursor"] = true;
    cursor["Scale"] = m_cursorScale;
    skin["Cursor"] = cursor;
    if (!m_skinPath.isEmpty()) {
        const QFileInfo skinInfo(m_skinPath);
        if (!skinInfo.isDir()) {
            *errorMessage = "Папка скина не существует: " + m_skinPath;
            return false;
        }
        skin["CurrentSkin"] = skinInfo.fileName();
        general = profile.value("General").toObject();
        general["OsuSkinsDir"] = skinInfo.absolutePath();
        profile["General"] = general;
    }
    profile["Skin"] = skin;

    QJsonObject objects = profile.value("Objects").toObject();
    QJsonObject objectColors = objects.value("Colors").toObject();
    objectColors["UseSkinComboColors"] = true;
    objectColors["UseBeatmapComboColors"] = false;
    objects["Colors"] = objectColors;
    profile["Objects"] = objects;

    QJsonObject gameplay = profile.value("Gameplay").toObject();
    const QList<QPair<QString, QString>> hudGroups = {
        {"score", "Score"}, {"hp", "HpBar"}, {"combo", "ComboCounter"},
        {"pp", "PPCounter"}, {"error", "HitErrorMeter"}, {"keys", "KeyOverlay"},
        {"mods", "Mods"}, {"leaderboard", "ScoreBoard"}, {"hitCounter", "HitCounter"},
        {"strain", "StrainGraph"}
    };
    for (const auto &[key, group] : hudGroups) {
        if (m_hudOptions.contains(key)) {
            QJsonObject value = gameplay.value(group).toObject();
            value["Show"] = m_hudOptions.value(key).toBool();
            gameplay[group] = value;
        }
    }
    profile["Gameplay"] = gameplay;

    QJsonObject playfield = profile.value("Playfield").toObject();
    QJsonObject background = playfield.value("Background").toObject();
    if (m_playfieldOptions.contains("dim")) {
        QJsonObject dim = background.value("Dim").toObject();
        dim["Normal"] = m_playfieldOptions.value("dim").toDouble();
        background["Dim"] = dim;
    }
    for (const auto &[key, setting] : QList<QPair<QString, QString>>{{"storyboard", "LoadStoryboards"}, {"video", "LoadVideos"}, {"parallax", "Parallax"}, {"blur", "Blur"}, {"bloom", "Bloom"}}) {
        if (!m_playfieldOptions.contains(key)) continue;
        if (key == "parallax" || key == "blur") { QJsonObject value = background.value(setting).toObject(); value["Enabled"] = m_playfieldOptions.value(key).toBool(); background[setting] = value; }
        else if (key == "bloom") { QJsonObject value = playfield.value(setting).toObject(); value["Enabled"] = m_playfieldOptions.value(key).toBool(); playfield[setting] = value; }
        else background[setting] = m_playfieldOptions.value(key);
    }
    playfield["Background"] = background;
    profile["Playfield"] = playfield;
    objects = profile.value("Objects").toObject();
    for (const auto &[key, setting] : QList<QPair<QString, QString>>{{"objects", "DrawObjects"}, {"approach", "DrawApproachCircles"}, {"numbers", "DrawComboNumbers"}, {"follow", "DrawFollowPoints"}, {"animations", "HitAnimations"}, {"beatScale", "ScaleToTheBeat"}})
        if (m_playfieldOptions.contains(key)) objects[setting] = m_playfieldOptions.value(key);
    QJsonObject sliders = objects.value("Sliders").toObject();
    QJsonObject snaking = sliders.value("Snaking").toObject();
    if (m_playfieldOptions.contains("snakeIn")) snaking["In"] = m_playfieldOptions.value("snakeIn");
    if (m_playfieldOptions.contains("snakeOut")) snaking["Out"] = m_playfieldOptions.value("snakeOut");
    sliders["Snaking"] = snaking;
    objects["Sliders"] = sliders;
    profile["Objects"] = objects;

    if (!QDir().mkpath(settingsDir)) {
        *errorMessage = "Не удалось создать папку настроек: " + settingsDir;
        return false;
    }
    QFile profileFile(QDir(settingsDir).filePath("osu-render.json"));
    if (!profileFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *errorMessage = "Не удалось записать профиль danser";
        return false;
    }
    profileFile.write(QJsonDocument(profile).toJson(QJsonDocument::Indented));
    return true;
}

QString Renderer::discoverSongsDirectory() const
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QStringList candidates = {
        home + "/.local/share/osu-wine/osu!/Songs", // osu-winello
        home + "/.osu/Songs",                       // osu! stable / legacy setups
        home + "/.local/share/osu/Songs",
        home + "/osu!/Songs"
    };

    for (const QString &path : candidates) {
        const QDir directory(path);
        if (directory.exists()) {
            return directory.absolutePath();
        }
    }
    return {};
}

void Renderer::buildArgs(const QString &profileName)
{
    m_args.clear();

    // danser keeps render options in settings/<profileName>.json.
    // -out is only a filename; the profile's Recording.OutputDir supplies its directory.
    m_args << "-settings=" + profileName;
    m_args << "-replay=" + m_replayPath;
    m_args << "-record";
    m_args << "-out=" + QFileInfo(m_outputPath).completeBaseName();
    m_args << "-preciseprogress";
    if (!m_skinPath.isEmpty()) {
        // Explicitly repeat the selected profile value: this also works with
        // danser versions which resolve -skin later than -settings.
        m_args << "-skin=" + QFileInfo(m_skinPath).fileName();
    }
    if (m_skipIntro) m_args << "-skip";
    if (m_startTime > 0.0) m_args << "-start=" + QString::number(m_startTime, 'f', 3);
    if (m_endTime > 0.0) m_args << "-end=" + QString::number(m_endTime, 'f', 3);
    if (m_audioOffset != 0) m_args << "-offset=" + QString::number(m_audioOffset);
}

void Renderer::onProcessStarted()
{
    emit started();
}

void Renderer::onProcessReadyRead()
{
    readProcessOutput();
}

void Renderer::readProcessOutput()
{
    while (m_process->canReadLine()) {
        QString line = m_process->readLine().trimmed();
        if (line.isEmpty()) continue;

        // Parse daneser progress output
        // Expected formats:
        // [Progress] 42% - Rendering frame 123/300
        // Status: rendering...
        if (line.contains('%', Qt::CaseInsensitive)) {
            int percent = 0;
            int idx = line.indexOf('%');
            if (idx > 0) {
                // Extract number before %
                int start = idx - 1;
                while (start > 0 && line[start - 1].isDigit()) start--;
                bool ok = false;
                percent = line.mid(start, idx - start).toInt(&ok);
                if (ok) {
                    emit progress(percent, line);
                }
            }
        } else {
            emit progress(-1, line);
        }
    }
}

void Renderer::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    emit finished(exitCode);
}

void Renderer::onProcessError(QProcess::ProcessError processError)
{
    Q_UNUSED(processError)
    emit error("Process error: " + m_process->errorString());
}
