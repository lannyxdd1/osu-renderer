#include "OsuApi.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QUrlQuery>

namespace {

const char *kApiBase = "https://osu.ppy.sh";
const char *kTokenPath = "/oauth/token";
const char *kUserAgent = "osu-render/0.1 (+https://github.com/lannyxdd1/osu-renderer)";
const char *kMirrorBase = "https://catboy.best";

QString errorText(QNetworkReply *reply, const QByteArray &body)
{
    QString detail;
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    if (json.contains("error_description")) {
        detail = json.value("error_description").toString();
    } else if (json.contains("error")) {
        detail = json.value("error").toString();
    } else if (json.contains("message")) {
        detail = json.value("message").toString();
    }
    if (detail.isEmpty()) {
        detail = QString::fromUtf8(body).trimmed().left(200);
    }

    QString text = QString("HTTP %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    if (!detail.isEmpty()) {
        text += ": " + detail;
    }
    return text;
}

QNetworkRequest apiRequest(const QString &path)
{
    QNetworkRequest request(QUrl(QString::fromLatin1(kApiBase) + path));
    request.setRawHeader("Accept", "application/json");
    // 20220705 is the first response version with has_replay / ended_at /
    // total_score; without the header the api returns the legacy shape.
    request.setRawHeader("x-api-version", "20220705");
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    return request;
}

} // namespace

OsuApi::OsuApi(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

void OsuApi::setCredentials(const Credentials &credentials)
{
    m_credentials = credentials;
}

void OsuApi::ensureToken(std::function<void(const QString &)> done)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_credentials.accessToken.isEmpty() && m_credentials.expiresAt > now + 120) {
        done({});
        return;
    }
    if (m_credentials.clientId.isEmpty() || m_credentials.clientSecret.isEmpty()) {
        done("Укажите Client ID и Client Secret OAuth-приложения.");
        return;
    }

    emit status("Получаю токен osu!api...");
    QNetworkRequest request = apiRequest(QString::fromLatin1(kTokenPath));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery form;
    form.addQueryItem("client_id", m_credentials.clientId);
    form.addQueryItem("client_secret", m_credentials.clientSecret);
    form.addQueryItem("grant_type", "client_credentials");
    form.addQueryItem("scope", "public");

    QNetworkReply *reply = m_manager->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            done("Токен osu! не получен — " + errorText(reply, body));
            return;
        }

        const QJsonObject json = QJsonDocument::fromJson(body).object();
        const QString token = json.value("access_token").toString();
        if (token.isEmpty()) {
            done("osu! вернул пустой токен.");
            return;
        }
        m_credentials.accessToken = token;
        m_credentials.expiresAt = QDateTime::currentSecsSinceEpoch()
                                  + json.value("expires_in").toInt(86400);
        emit credentialsChanged();
        emit status("Токен osu! получен.");
        done({});
    });
}

void OsuApi::getJson(const QString &path,
                     std::function<void(const QByteArray &, const QString &)> done,
                     bool retried)
{
    ensureToken([this, path, done, retried](const QString &error) {
        if (!error.isEmpty()) {
            done({}, error);
            return;
        }

        QNetworkRequest request = apiRequest(path);
        request.setRawHeader("Authorization", "Bearer " + m_credentials.accessToken.toUtf8());

        QNetworkReply *reply = m_manager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, path, done, retried]() {
            reply->deleteLater();
            const QByteArray body = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (status == 401 && !retried) {
                m_credentials.accessToken.clear();
                m_credentials.expiresAt = 0;
                getJson(path, done, true);
                return;
            }
            if (reply->error() != QNetworkReply::NoError) {
                done({}, errorText(reply, body));
                return;
            }
            done(body, {});
        });
    });
}

void OsuApi::fetchScores(const QString &user, const QString &type, int limit,
                         std::function<void(const QJsonArray &, const QString &, const QString &)> done)
{
    const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(user));
    getJson(QString("/api/v2/users/%1").arg(encoded),
            [this, encoded, user, type, limit, done](const QByteArray &body, const QString &error) {
        if (!error.isEmpty()) {
            done({}, user, QString("Игрок «%1» не найден на osu! — %2").arg(user, error));
            return;
        }

        const QJsonObject profile = QJsonDocument::fromJson(body).object();
        const QString username = profile.value("username").toString(user);
        const qint64 userId = profile.value("id").toInteger();
        if (userId <= 0) {
            done({}, username, "osu! не вернул ID игрока.");
            return;
        }

        const QString path = QString("/api/v2/users/%1/scores/%2?mode=osu&limit=%3&include_fails=0")
                                 .arg(QString::number(userId), type)
                                 .arg(limit);
        getJson(path, [done, username](const QByteArray &scoresBody, const QString &scoresError) {
            if (!scoresError.isEmpty()) {
                done({}, username, scoresError);
                return;
            }

            const QJsonDocument document = QJsonDocument::fromJson(scoresBody);
            if (document.isArray()) {
                done(document.array(), username, {});
                return;
            }
            const QJsonObject object = document.object();
            if (object.value("scores").isArray()) {
                done(object.value("scores").toArray(), username, {});
                return;
            }
            done({}, username, "Неожиданный ответ osu!api.");
        });
    });
}

void OsuApi::downloadReplay(qint64 scoreId, qint64 legacyScoreId, const QString &targetPath,
                            std::function<void(const QString &)> done)
{
    const QString primary = QString("/api/v2/scores/%1/download").arg(scoreId);
    const QString legacy = legacyScoreId > 0
        ? QString("/api/v2/scores/osu/%1/download").arg(legacyScoreId)
        : QString();

    // Simple two-step flow: try the solo score id, then the legacy one.
    getFile(QString::fromLatin1(kApiBase) + primary, targetPath, true,
            [this, legacy, targetPath, done](const QString &error) {
        if (error.isEmpty() || legacy.isEmpty()) {
            done(error);
            return;
        }
        getFile(QString::fromLatin1(kApiBase) + legacy, targetPath, true, done);
    });
}

void OsuApi::getFile(const QString &url, const QString &targetPath, bool authorized,
                     std::function<void(const QString &)> done, bool retried)
{
    const auto start = [this, url, targetPath, authorized, done, retried]() {
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kUserAgent));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        // Beatmap archives can be tens of megabytes.
        request.setTransferTimeout(300000);
        if (authorized) {
            request.setRawHeader("Authorization", "Bearer " + m_credentials.accessToken.toUtf8());
        }

        QNetworkReply *reply = m_manager->get(request);
        auto *file = new QSaveFile(targetPath, reply);
        if (!file->open(QIODevice::WriteOnly)) {
            reply->abort();
            reply->deleteLater();
            done("Не удалось записать файл: " + targetPath);
            return;
        }

        connect(reply, &QNetworkReply::readyRead, reply, [reply, file]() {
            file->write(reply->readAll());
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, file, url, targetPath,
                                                        authorized, done, retried]() {
            reply->deleteLater();
            const QByteArray tail = reply->readAll();
            if (!tail.isEmpty()) {
                file->write(tail);
            }

            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && authorized && !retried) {
                file->cancelWriting();
                m_credentials.accessToken.clear();
                m_credentials.expiresAt = 0;
                getFile(url, targetPath, authorized, done, true);
                return;
            }

            if (reply->error() != QNetworkReply::NoError) {
                file->cancelWriting();
                done("Скачивание не удалось — " + errorText(reply, {}));
                return;
            }
            if (!file->commit()) {
                done("Не удалось сохранить файл: " + targetPath);
                return;
            }
            done({});
        });
    };

    if (authorized) {
        ensureToken([this, start, done](const QString &error) {
            if (!error.isEmpty()) {
                done(error);
                return;
            }
            start();
        });
    } else {
        start();
    }
}

void OsuApi::downloadBeatmapset(qint64 beatmapsetId, const QString &folderName,
                                const QString &songsDirectory,
                                std::function<void(const QString &, const QString &)> done)
{
    if (songsDirectory.isEmpty() || !QDir(songsDirectory).exists()) {
        done({}, "Папка Songs не найдена, карту скачать некуда.");
        return;
    }

    emit status("Скачиваю карту с зеркала catboy.best...");
    const QString archive = QDir::temp().filePath(
        QString("osu-render-%1.osz").arg(beatmapsetId));

    getFile(QString("%1/d/%2").arg(QString::fromLatin1(kMirrorBase)).arg(beatmapsetId),
            archive, false, [this, archive, folderName, songsDirectory, done](const QString &error) {
        if (!error.isEmpty()) {
            done({}, "Карта не скачалась — " + error);
            return;
        }

        const QString target = QDir(songsDirectory).filePath(folderName);
        QDir().mkpath(target);

        emit status("Распаковываю карту...");
        QProcess unzip;
        unzip.start("unzip", {"-oq", archive, "-d", target});
        bool ok = unzip.waitForStarted(5000) && unzip.waitForFinished(180000)
                  && unzip.exitStatus() == QProcess::NormalExit && unzip.exitCode() == 0;
        if (!ok) {
            // bsdtar handles some archives that unzip rejects.
            QProcess tar;
            tar.start("bsdtar", {"-xf", archive, "-C", target});
            ok = tar.waitForStarted(5000) && tar.waitForFinished(180000)
                 && tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0;
        }
        QFile::remove(archive);

        if (!ok) {
            done({}, "Не удалось распаковать карту (нужен unzip или bsdtar).");
            return;
        }

        bool hasMap = false;
        QDirIterator it(target, {"*.osu"}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            hasMap = true;
            break;
        }
        if (!hasMap) {
            done({}, "В архиве карты не оказалось .osu файла.");
            return;
        }

        done(target, {});
    });
}
