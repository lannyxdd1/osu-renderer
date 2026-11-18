#include "BeatmapIndex.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

QString cacheFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                        + "/osu_render";
    QDir().mkpath(dir);
    return dir + "/beatmaps.json";
}

} // namespace

QJsonObject BeatmapInfo::toJson() const
{
    QJsonObject json;
    json["artist"] = artist;
    json["title"] = title;
    json["mapper"] = creator;
    json["difficulty"] = difficulty;
    json["ar"] = approachRate;
    json["od"] = overallDifficulty;
    json["cs"] = circleSize;
    json["hp"] = hpDrain;
    json["bpm"] = bpm;
    json["length"] = length;
    json["objects"] = hitObjects;
    return json;
}

BeatmapInfo parseOsuFile(const QString &path)
{
    BeatmapInfo info;
    info.path = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return info;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');

    QString section;
    QHash<QString, QString> values;
    double lastObjectTime = 0.0;
    int objects = 0;
    bool bpmFound = false;

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith("//")) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            section = line.mid(1, line.size() - 2);
            continue;
        }

        if (section == "Metadata" || section == "Difficulty") {
            const int colon = line.indexOf(':');
            if (colon > 0) {
                values[section + "/" + line.left(colon).trimmed()] = line.mid(colon + 1).trimmed();
            }
        } else if (section == "TimingPoints" && !bpmFound) {
            const QStringList fields = line.split(',');
            if (fields.size() >= 2) {
                bool ok = false;
                const double beatLength = fields.at(1).toDouble(&ok);
                if (ok && beatLength > 0.0) {
                    info.bpm = 60000.0 / beatLength;
                    bpmFound = true;
                }
            }
        } else if (section == "HitObjects") {
            const QStringList fields = line.split(',');
            if (fields.size() >= 3) {
                bool ok = false;
                const double time = fields.at(2).toDouble(&ok);
                if (ok) {
                    lastObjectTime = qMax(lastObjectTime, time);
                    ++objects;
                }
            }
        }
    }

    info.artist = values.value("Metadata/Artist", values.value("Metadata/ArtistUnicode"));
    info.title = values.value("Metadata/Title", values.value("Metadata/TitleUnicode"));
    info.creator = values.value("Metadata/Creator");
    info.difficulty = values.value("Metadata/Version");
    info.approachRate = values.value("Difficulty/ApproachRate").toDouble();
    info.overallDifficulty = values.value("Difficulty/OverallDifficulty").toDouble();
    info.circleSize = values.value("Difficulty/CircleSize").toDouble();
    info.hpDrain = values.value("Difficulty/HPDrainRate").toDouble();
    if (info.approachRate <= 0.0) {
        info.approachRate = info.overallDifficulty;
    }
    info.length = lastObjectTime / 1000.0;
    info.hitObjects = objects;
    return info;
}

BeatmapInfo BeatmapIndex::find(const QString &md5, const QString &songsDirectory)
{
    if (md5.isEmpty()) {
        return {};
    }
    if (!m_built || m_songsDirectory != songsDirectory) {
        build(songsDirectory);
    }
    const QString path = m_md5ToPath.value(md5);
    if (path.isEmpty()) {
        return {};
    }
    return parseOsuFile(path);
}

void BeatmapIndex::build(const QString &songsDirectory)
{
    m_built = true;
    m_songsDirectory = songsDirectory;
    m_md5ToPath.clear();
    m_error.clear();

    if (songsDirectory.isEmpty() || !QDir(songsDirectory).exists()) {
        m_error = "Папка Songs не найдена";
        return;
    }

    loadCache();
    QHash<QString, CacheEntry> updated;
    QDirIterator it(songsDirectory, {"*.osu"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (info.fileName().startsWith('.')) {
            continue;
        }

        CacheEntry entry;
        const auto cached = m_cache.constFind(path);
        if (cached != m_cache.constEnd()
            && cached->modified == info.lastModified().toMSecsSinceEpoch()
            && cached->size == info.size()) {
            entry = *cached;
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QCryptographicHash hash(QCryptographicHash::Md5);
            hash.addData(file.readAll());
            entry.md5 = QString::fromLatin1(hash.result().toHex());
            entry.modified = info.lastModified().toMSecsSinceEpoch();
            entry.size = info.size();
        }

        updated.insert(path, entry);
        if (!entry.md5.isEmpty()) {
            m_md5ToPath.insert(entry.md5, path);
        }
    }

    m_cache = updated;
    saveCache();
}

void BeatmapIndex::loadCache()
{
    QFile file(cacheFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray maps = document.object().value("maps").toArray();
    for (const QJsonValue &value : maps) {
        const QJsonObject item = value.toObject();
        const QString path = item.value("path").toString();
        if (path.isEmpty()) {
            continue;
        }
        CacheEntry entry;
        entry.modified = static_cast<qint64>(item.value("mtime").toDouble());
        entry.size = static_cast<qint64>(item.value("size").toDouble());
        entry.md5 = item.value("md5").toString();
        m_cache.insert(path, entry);
    }
}

void BeatmapIndex::saveCache()
{
    QJsonArray maps;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        QJsonObject item;
        item["path"] = it.key();
        item["mtime"] = static_cast<double>(it->modified);
        item["size"] = static_cast<double>(it->size);
        item["md5"] = it->md5;
        maps.append(item);
    }
    QJsonObject root;
    root["maps"] = maps;

    QFile file(cacheFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}
