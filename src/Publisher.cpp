#include "Publisher.h"

#include "BeatmapIndex.h"
#include "ReplayInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

namespace {

QString generateId()
{
    static const char alphabet[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    const int count = static_cast<int>(sizeof(alphabet)) - 1;
    QString id;
    id.reserve(10);
    for (int i = 0; i < 10; ++i) {
        id.append(QLatin1Char(alphabet[QRandomGenerator::global()->bounded(count)]));
    }
    return id;
}

QString normalizedBaseUrl(QString url)
{
    url = url.trimmed();
    while (url.endsWith('/')) {
        url.chop(1);
    }
    return url;
}

} // namespace

Publisher::Publisher(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

Publisher::~Publisher()
{
    cancel();
}

bool Publisher::isBusy() const
{
    return m_stage != Stage::Idle;
}

void Publisher::publish(const Job &job)
{
    if (isBusy()) {
        return;
    }

    m_job = job;
    m_job.workerUrl = normalizedBaseUrl(job.workerUrl);

    if (m_job.workerUrl.isEmpty() || m_job.token.isEmpty()) {
        emit error("Не заданы Worker URL или токен публикации.");
        return;
    }
    if (m_job.githubToken.isEmpty()) {
        emit error("Не задан GitHub token для загрузки видео.");
        return;
    }
    if (!QFileInfo(m_job.danserPath).isExecutable()) {
        emit error("Не найден danser-cli.");
        return;
    }
    if (!QFileInfo(m_job.replayPath).isFile()) {
        emit error("Не найден файл реплея: " + m_job.replayPath);
        return;
    }
    if (!QFileInfo(m_job.videoPath).isFile()) {
        emit error("Не найден готовый рендер: " + m_job.videoPath);
        return;
    }

    // An empty songsPath must be treated as "not set": QDir("") resolves to the
    // current working directory and exists(), so the autodetect below would be
    // skipped and the beatmap lookup would silently fail.
    m_job.songsPath = detectSongsDirectory(m_job.songsPath);
    if (m_job.songsPath.isEmpty()) {
        emit status("Папка Songs не найдена — название карты не попадёт в публикацию.");
    }

    if (m_tempDir) {
        delete m_tempDir;
    }
    m_tempDir = new QTemporaryDir;
    if (!m_tempDir->isValid()) {
        finishWithError("Не удалось создать временную папку.");
        return;
    }

    emit status("Чтение реплея...");
    QCoreApplication::processEvents();

    QString replayError;
    const ReplayInfo replay = ReplayInfo::read(m_job.replayPath, &replayError);
    if (!replayError.isEmpty()) {
        finishWithError(replayError);
        return;
    }
    m_replayJson = replay.toJson();

    emit status("Поиск карты в Songs...");
    QCoreApplication::processEvents();
    BeatmapIndex index;
    const BeatmapInfo beatmap = index.find(replay.beatmapMd5, m_job.songsPath);
    m_beatmapJson = beatmap.toJson();
    m_beatmapJson["md5"] = replay.beatmapMd5;
    if (replay.gameMode == 0 && !beatmap.path.isEmpty()) {
        calculatePp(replay, beatmap.path);
    }

    m_id = generateId();
    m_stage = Stage::Probing;
    m_uploadIndex = 0;
    m_uploadUrls.clear();
    m_uploadTypes.clear();
    m_uploadOrder.clear();

    probeVideo();
}

void Publisher::probeVideo()
{
    emit status("Определение длительности видео...");
    const QStringList args = {"-hide_banner", "-i", m_job.videoPath};
    runProcess(args, [this](int, const QString &output) {
        m_duration = parseDuration(output);
        if (m_duration <= 0.0) {
            finishWithError("Не удалось определить длительность видео.");
            return;
        }
        startEncode();
    });
}

void Publisher::startEncode()
{
    m_stage = Stage::Encoding;
    m_webVideo = m_tempDir->filePath("web.mp4");
    // Danser's bundled ffmpeg is a stripped build (no AAC decoder), so use the
    // system ffmpeg when available. Otherwise keep the master audio stream.
    QStringList args = {
        "-y", "-hide_banner", "-loglevel", "error",
        "-i", m_job.videoPath,
        "-vf", "scale=-2:720",
        "-r", "60",
        "-c:v", "libx264", "-preset", "medium", "-crf", "23",
        "-pix_fmt", "yuv420p"
    };
    if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
        args << "-c:a" << "copy";
    } else {
        args << "-c:a" << "aac" << "-b:a" << "160k";
    }
    args << "-movflags" << "+faststart" << m_webVideo;
    runFfmpeg(args, "Кодирование веб-версии 720p60...", [this]() { startThumbnail(); });
}

void Publisher::startThumbnail()
{
    m_stage = Stage::Thumbnail;
    m_thumb = m_tempDir->filePath("thumb.jpg");
    const double at = qBound(0.5, m_duration * 0.15, qMax(0.5, m_duration - 0.2));
    const QStringList args = {
        "-y", "-hide_banner", "-loglevel", "error",
        "-ss", QString::number(at, 'f', 2),
        "-i", m_job.videoPath,
        "-frames:v", "1",
        "-vf", "scale=1280:-2",
        "-q:v", "4",
        m_thumb
    };
    runFfmpeg(args, "Создание превью...", [this]() { startPreview(); });
}

void Publisher::startPreview()
{
    m_stage = Stage::Preview;
    m_preview = m_tempDir->filePath("preview.mp4");
    const double at = qBound(0.0, m_duration * 0.25, qMax(0.0, m_duration - 6.0));
    const QStringList args = {
        "-y", "-hide_banner", "-loglevel", "error",
        "-ss", QString::number(at, 'f', 2),
        "-t", "6",
        "-i", m_job.videoPath,
        "-vf", "scale=-2:360",
        "-r", "30",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "30",
        "-an",
        m_preview
    };
    runFfmpeg(args, "Создание короткого превью...", [this]() { prepareUpload(); });
}

void Publisher::prepareUpload()
{
    m_stage = Stage::Preparing;
    emit status("Подготовка загрузки...");

    QJsonObject files;
    files["video"] = QJsonObject{{"size", static_cast<double>(QFileInfo(m_webVideo).size())}};
    files["thumb"] = QJsonObject{{"size", static_cast<double>(QFileInfo(m_thumb).size())}};
    files["preview"] = QJsonObject{{"size", static_cast<double>(QFileInfo(m_preview).size())}};

    QJsonObject body;
    body["id"] = m_id;
    body["files"] = files;

    postJson(m_job.workerUrl + "/api/upload", body, [this](const QJsonObject &response) {
        const QJsonObject uploads = response.value("uploads").toObject();
        m_uploadUrls.clear();
        m_uploadTypes.clear();
        m_uploadOrder.clear();
        for (const QString &kind : {QStringLiteral("video"), QStringLiteral("thumb"), QStringLiteral("preview")}) {
            const QJsonObject entry = uploads.value(kind).toObject();
            const QString url = entry.value("url").toString();
            if (url.isEmpty()) {
                finishWithError("Worker не вернул ссылку для загрузки: " + kind);
                return;
            }
            m_uploadUrls.insert(kind, url);
            m_uploadTypes.insert(kind, entry.value("contentType").toString(
                kind == "thumb" ? "image/jpeg" : "video/mp4"));
            m_uploadOrder << kind;
        }
        m_uploadIndex = 0;
        m_stage = Stage::Uploading;
        uploadNext();
    });
}

void Publisher::uploadNext()
{
    if (m_uploadIndex >= m_uploadOrder.size()) {
        finalizeUpload();
        return;
    }

    const QString kind = m_uploadOrder.at(m_uploadIndex);
    const QString path = kind == "video" ? m_webVideo : (kind == "thumb" ? m_thumb : m_preview);
    const QString contentType = m_uploadTypes.value(kind, kind == "thumb" ? "image/jpeg" : "video/mp4");
    const QString label = kind == "video" ? "видео" : (kind == "thumb" ? "превью" : "клип");

    emit status(QString("Загрузка на GitHub: %1 (%2 из %3)...")
                    .arg(label)
                    .arg(m_uploadIndex + 1)
                    .arg(m_uploadOrder.size()));

    m_uploadFile = new QFile(path, this);
    if (!m_uploadFile->open(QIODevice::ReadOnly)) {
        finishWithError("Не удалось открыть файл для загрузки: " + path);
        return;
    }

    QNetworkRequest request{QUrl(m_uploadUrls.value(kind))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setRawHeader("Authorization", "Bearer " + m_job.githubToken.toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "osu-render");
    QNetworkReply *reply = m_network->post(request, m_uploadFile);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (m_uploadFile) {
            m_uploadFile->close();
            m_uploadFile->deleteLater();
            m_uploadFile = nullptr;
        }

        if (failed) {
            const QJsonObject response = QJsonDocument::fromJson(body).object();
            QString detail = response.value("message").toString();
            if (detail.isEmpty()) {
                detail = QString::fromUtf8(body).trimmed().left(300);
            }
            finishWithError("Ошибка загрузки: " + errorText
                            + (detail.isEmpty() ? QString() : " — " + detail));
            return;
        }

        ++m_uploadIndex;
        uploadNext();
    });
}

void Publisher::finalizeUpload()
{
    m_stage = Stage::Finalizing;
    emit status("Сохранение метаданных...");

    const QJsonObject metadata = buildMetadata();
    const QUrl url(m_job.workerUrl + "/api/replays/" + m_id);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_job.token.toUtf8());
    QNetworkReply *reply = m_network->post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            return;
        }
        m_reply = nullptr;
        const QByteArray body = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const QString errorText = reply->errorString();
        const QJsonObject response = QJsonDocument::fromJson(body).object();
        reply->deleteLater();

        if (failed) {
            const QString detail = response.value("error").toString(
                QString::fromUtf8(body).trimmed());
            finishWithError("Не удалось сохранить реплей: " + errorText
                            + (detail.isEmpty() ? QString() : " — " + detail));
            return;
        }

        QString link = response.value("url").toString();
        if (link.isEmpty()) {
            link = m_job.workerUrl + "/r/" + m_id;
        }
        finishWithUrl(link);
    });
}

void Publisher::runProcess(const QStringList &args,
                           const std::function<void(int, const QString &)> &next)
{
    QProcess *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.startsWith(QFileInfo(m_job.danserPath).absolutePath() + "/ffmpeg/bin/")) {
        const QString libDir = QFileInfo(m_job.danserPath).absolutePath() + "/ffmpeg/lib";
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        const QString existing = environment.value("LD_LIBRARY_PATH");
        environment.insert("LD_LIBRARY_PATH",
                           existing.isEmpty() ? libDir : libDir + ":" + existing);
        process->setProcessEnvironment(environment);
    }

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, next](int exitCode, QProcess::ExitStatus) {
                if (m_process != process) {
                    return;
                }
                m_process = nullptr;
                const QString output = QString::fromUtf8(process->readAll());
                process->deleteLater();
                next(exitCode, output);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (m_process != process) {
            return;
        }
        const QString message = process->errorString();
        m_process = nullptr;
        process->deleteLater();
        finishWithError("Не удалось запустить ffmpeg: " + message);
    });

    process->start(ffmpeg, args);
}

void Publisher::runFfmpeg(const QStringList &args, const QString &statusText,
                          const std::function<void()> &next)
{
    emit status(statusText);
    runProcess(args, [this, next](int exitCode, const QString &output) {
        if (exitCode != 0) {
            finishWithError("ffmpeg завершился с ошибкой: " + output.trimmed());
            return;
        }
        next();
    });
}

void Publisher::postJson(const QString &url, const QJsonObject &body,
                         const std::function<void(const QJsonObject &)> &next)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_job.token.toUtf8());
    QNetworkReply *reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, next]() {
        if (m_reply != reply) {
            return;
        }
        m_reply = nullptr;
        const QByteArray data = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const QString errorText = reply->errorString();
        const QJsonObject response = QJsonDocument::fromJson(data).object();
        reply->deleteLater();

        if (failed) {
            const QString detail = response.value("error").toString(
                QString::fromUtf8(data).trimmed());
            finishWithError("Запрос к Worker не удался: " + errorText
                            + (detail.isEmpty() ? QString() : " — " + detail));
            return;
        }
        next(response);
    });
}

void Publisher::calculatePp(const ReplayInfo &replay, const QString &beatmapPath)
{
    // ppcalc is a small Go helper that links danser's own pp calculator, so the
    // number matches the PP counter visible in the render. It is optional: if
    // the helper is missing, publishing continues without PP.
    QString helper = qEnvironmentVariable("OSU_RENDER_PPCALC");
    if (helper.isEmpty()) {
        helper = QCoreApplication::applicationDirPath() + "/ppcalc";
    }
    if (!QFileInfo(helper).isExecutable()) {
        emit status("ppcalc не найден — PP не будет добавлен.");
        return;
    }

    emit status("Подсчёт PP...");
    QCoreApplication::processEvents();

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // ppcalc links libbass through danser's beatmap package; the shared
    // libraries live next to danser-cli.
    const QString danserDir = QFileInfo(m_job.danserPath).absolutePath();
    const QString libraryPath = environment.value("LD_LIBRARY_PATH");
    environment.insert("LD_LIBRARY_PATH",
                       libraryPath.isEmpty() ? danserDir : danserDir + ':' + libraryPath);
    process.setProcessEnvironment(environment);

    const QStringList args = {
        "-map", beatmapPath,
        "-mods", replay.modNames().join(QString()),
        "-acc", QString::number(replay.accuracy(), 'f', 4),
        "-combo", QString::number(replay.maxCombo),
        "-miss", QString::number(replay.misses),
        "-n300", QString::number(replay.count300),
        "-n100", QString::number(replay.count100),
        "-n50", QString::number(replay.count50),
        "-score", QString::number(replay.score)
    };

    process.start(helper, args);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(1000);
        emit status("PP не посчитан: ppcalc не ответил.");
        return;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        emit status("PP не посчитан: "
                    + QString::fromUtf8(process.readAllStandardError()).trimmed());
        return;
    }

    const QJsonObject result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    const double pp = result.value("pp").toDouble(-1.0);
    const double stars = result.value("stars").toDouble(-1.0);
    if (pp > 0.0) {
        m_replayJson["pp"] = pp;
    }
    if (stars > 0.0) {
        m_replayJson["stars"] = stars;
    }
}

QJsonObject Publisher::buildMetadata() const
{
    QJsonObject metadata = m_replayJson;
    metadata["beatmap"] = m_beatmapJson;

    QJsonObject video;
    video["width"] = 1280;
    video["height"] = 720;
    video["fps"] = 60;
    video["duration"] = m_duration;
    video["size"] = static_cast<double>(QFileInfo(m_webVideo).size());
    metadata["video"] = video;

    metadata["settings"] = m_job.settings;

    const QString profilePath = QFileInfo(m_job.danserPath).absolutePath()
                                + "/settings/osu-render.json";
    QFile profileFile(profilePath);
    if (profileFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument profile = QJsonDocument::fromJson(profileFile.readAll());
        if (profile.isObject()) {
            metadata["profile"] = profile.object();
        }
    }

    return metadata;
}

double Publisher::parseDuration(const QString &text) const
{
    const QRegularExpression expression(
        QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = expression.match(text);
    if (!match.hasMatch()) {
        return 0.0;
    }
    return match.captured(1).toInt() * 3600.0
           + match.captured(2).toInt() * 60.0
           + match.captured(3).toDouble();
}

QString Publisher::ffmpegPath() const
{
    // Prefer ffmpeg from PATH: danser's bundled build is stripped down and
    // cannot decode AAC audio.
    const QString system = QStandardPaths::findExecutable("ffmpeg");
    if (!system.isEmpty()) {
        return system;
    }

    const QString base = QFileInfo(m_job.danserPath).absolutePath();
    // Danser ships ffmpeg as <danser>/ffmpeg/bin/ffmpeg (newer releases) or
    // as a single <danser>/ffmpeg binary (older releases).
    const QStringList candidates = {
        base + "/ffmpeg/bin/ffmpeg",
        base + "/ffmpeg"
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QStringLiteral("ffmpeg");
}

void Publisher::finishWithError(const QString &message)
{
    cleanup();
    emit error(message);
}

void Publisher::finishWithUrl(const QString &url)
{
    cleanup();
    emit finished(url);
}

void Publisher::cancel()
{
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
        process->deleteLater();
    }
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_uploadFile) {
        m_uploadFile->close();
        m_uploadFile->deleteLater();
        m_uploadFile = nullptr;
    }
    if (isBusy()) {
        cleanup();
        emit status("Публикация отменена.");
    }
}

void Publisher::cleanup()
{
    m_stage = Stage::Idle;
    m_uploadIndex = 0;
    m_uploadOrder.clear();
    m_uploadUrls.clear();
    m_uploadTypes.clear();
    if (m_tempDir) {
        delete m_tempDir;
        m_tempDir = nullptr;
    }
}
