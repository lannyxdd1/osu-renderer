#ifndef PUBLISHER_H
#define PUBLISHER_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;

// Encodes a 720p60 web copy of a finished render and uploads it to the
// Cloudflare Worker configured by the user.
class Publisher : public QObject
{
    Q_OBJECT

public:
    struct Job
    {
        QString workerUrl;
        QString token;
        QString githubToken;
        QString danserPath;
        QString songsPath;
        QString replayPath;
        QString videoPath;
        QJsonObject settings;
    };

    explicit Publisher(QObject *parent = nullptr);
    ~Publisher() override;

    void publish(const Job &job);
    void cancel();
    bool isBusy() const;

signals:
    void status(const QString &message);
    void finished(const QString &url);
    void error(const QString &message);

private:
    enum class Stage
    {
        Idle,
        Probing,
        Encoding,
        Thumbnail,
        Preview,
        Preparing,
        Uploading,
        Finalizing
    };

    void probeVideo();
    void startEncode();
    void startThumbnail();
    void startPreview();
    void prepareUpload();
    void uploadNext();
    void finalizeUpload();
    void runFfmpeg(const QStringList &args, const QString &statusText,
                   const std::function<void()> &next);
    void runProcess(const QStringList &args, const std::function<void(int, const QString &)> &next);
    void postJson(const QString &url, const QJsonObject &body,
                  const std::function<void(const QJsonObject &)> &next);
    void finishWithError(const QString &message);
    void finishWithUrl(const QString &url);
    void cleanup();

    QString ffmpegPath() const;
    QJsonObject buildMetadata() const;
    double parseDuration(const QString &text) const;

    Stage m_stage = Stage::Idle;
    Job m_job;
    QTemporaryDir *m_tempDir = nullptr;
    QString m_id;
    QJsonObject m_replayJson;
    QJsonObject m_beatmapJson;
    double m_duration = 0.0;
    QString m_webVideo;
    QString m_thumb;
    QString m_preview;
    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_uploadFile = nullptr;
    QStringList m_uploadOrder;
    QHash<QString, QString> m_uploadUrls;
    QHash<QString, QString> m_uploadTypes;
    int m_uploadIndex = 0;
};

#endif // PUBLISHER_H
