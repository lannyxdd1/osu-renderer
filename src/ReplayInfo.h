#ifndef REPLAYINFO_H
#define REPLAYINFO_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Metadata stored in the header of an osu! .osr replay.
struct ReplayInfo
{
    int gameMode = 0;
    int version = 0;
    QString beatmapMd5;
    QString player;
    QString replayMd5;
    int count300 = 0;
    int count100 = 0;
    int count50 = 0;
    int geki = 0;
    int katu = 0;
    int misses = 0;
    int score = 0;
    int maxCombo = 0;
    bool perfect = false;
    quint32 mods = 0;
    QDateTime playedAt;
    int replayLength = 0;

    double accuracy() const;
    QStringList modNames() const;
    QJsonObject toJson() const;

    static ReplayInfo read(const QString &path, QString *errorMessage = nullptr);
};

#endif // REPLAYINFO_H
