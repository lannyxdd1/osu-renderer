#ifndef BEATMAPINDEX_H
#define BEATMAPINDEX_H

#include <QHash>
#include <QJsonObject>
#include <QString>

struct BeatmapInfo
{
    QString path;
    QString artist;
    QString title;
    QString creator;
    QString difficulty;
    double approachRate = 0.0;
    double overallDifficulty = 0.0;
    double circleSize = 0.0;
    double hpDrain = 0.0;
    double bpm = 0.0;
    double length = 0.0;
    int hitObjects = 0;

    QJsonObject toJson() const;
};

// Maps beatmap MD5 (stored in .osr) to the local .osu file.
// Hashing is cached in ~/.config/osu_render/beatmaps.json so only new or
// changed maps are hashed again.
class BeatmapIndex
{
public:
    BeatmapInfo find(const QString &md5, const QString &songsDirectory);
    QString lastError() const { return m_error; }

private:
    struct CacheEntry
    {
        qint64 modified = 0;
        qint64 size = 0;
        QString md5;
    };

    void build(const QString &songsDirectory);
    void loadCache();
    void saveCache();

    QString m_songsDirectory;
    QHash<QString, QString> m_md5ToPath;
    QHash<QString, CacheEntry> m_cache;
    bool m_built = false;
    QString m_error;
};

BeatmapInfo parseOsuFile(const QString &path);

#endif // BEATMAPINDEX_H
