#ifndef OSUAPI_H
#define OSUAPI_H

#include <QJsonArray>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

// Thin client for the bits of the osu!api v2 needed to render other people's
// plays: guest tokens, a user's score list and replay / beatmap downloads.
//
// The api is used with the Client Credentials grant and the "public" scope,
// which is enough for both `/users/{user}/scores/{type}` and
// `/scores/{score}/download` according to the official docs.
class OsuApi : public QObject
{
    Q_OBJECT

public:
    struct Credentials
    {
        QString clientId;
        QString clientSecret;
        QString accessToken;
        qint64 expiresAt = 0; // unix seconds
    };

    explicit OsuApi(QObject *parent = nullptr);

    void setCredentials(const Credentials &credentials);
    Credentials credentials() const { return m_credentials; }

    // type is "recent" or "best". The player is looked up first so a typo in
    // the nickname produces a clear message instead of a bare 404, and the
    // canonical nickname is returned for file names.
    void fetchScores(const QString &user, const QString &type, int limit,
                     std::function<void(const QJsonArray &scores, const QString &username,
                                        const QString &error)> done);

    // Writes the .osr to targetPath. legacyScoreId may be -1.
    void downloadReplay(qint64 scoreId, qint64 legacyScoreId, const QString &targetPath,
                        std::function<void(const QString &error)> done);

    // Downloads the .osz from a public mirror and unpacks it into
    // songsDirectory/<folderName>. Returns the directory through done.
    void downloadBeatmapset(qint64 beatmapsetId, const QString &folderName,
                            const QString &songsDirectory,
                            std::function<void(const QString &directory, const QString &error)> done);

signals:
    void status(const QString &message);
    void credentialsChanged();

private:
    void ensureToken(std::function<void(const QString &error)> done);
    void getJson(const QString &path,
                 std::function<void(const QByteArray &body, const QString &error)> done,
                 bool retried = false);
    void getFile(const QString &url, const QString &targetPath, bool authorized,
                 std::function<void(const QString &error)> done, bool retried = false);

    QNetworkAccessManager *m_manager;
    Credentials m_credentials;
};

#endif // OSUAPI_H
