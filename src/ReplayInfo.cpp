#include "ReplayInfo.h"

#include <QFile>
#include <QJsonArray>
#include <QTimeZone>

namespace {

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}

    bool ok() const { return m_ok; }

    quint8 u8()
    {
        if (m_pos + 1 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        return static_cast<quint8>(m_data.at(m_pos++));
    }

    quint16 u16()
    {
        if (m_pos + 2 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        const quint16 value = static_cast<quint8>(m_data.at(m_pos))
                              | (static_cast<quint16>(static_cast<quint8>(m_data.at(m_pos + 1))) << 8);
        m_pos += 2;
        return value;
    }

    qint16 i16() { return static_cast<qint16>(u16()); }

    qint32 i32()
    {
        const quint32 value = static_cast<quint32>(u16()) | (static_cast<quint32>(u16()) << 16);
        return static_cast<qint32>(value);
    }

    qint64 i64()
    {
        const quint64 low = static_cast<quint32>(i32());
        const quint64 high = static_cast<quint32>(i32());
        return static_cast<qint64>(low | (high << 32));
    }

    QString str()
    {
        const quint8 marker = u8();
        if (!m_ok || marker == 0x00) {
            return {};
        }
        if (marker != 0x0b) {
            m_ok = false;
            return {};
        }

        quint64 length = 0;
        int shift = 0;
        for (;;) {
            const quint8 byte = u8();
            if (!m_ok) {
                return {};
            }
            length |= static_cast<quint64>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) {
                break;
            }
            shift += 7;
            if (shift > 63) {
                m_ok = false;
                return {};
            }
        }

        if (length > static_cast<quint64>(m_data.size() - m_pos)) {
            m_ok = false;
            return {};
        }
        const QString value = QString::fromUtf8(m_data.constData() + m_pos, static_cast<int>(length));
        m_pos += static_cast<int>(length);
        return value;
    }

private:
    const QByteArray &m_data;
    int m_pos = 0;
    bool m_ok = true;
};

struct ModName
{
    quint32 bit;
    const char *name;
};

const ModName MOD_NAMES[] = {
    {1u << 0, "NF"}, {1u << 1, "EZ"}, {1u << 2, "TD"}, {1u << 3, "HD"},
    {1u << 4, "HR"}, {1u << 5, "SD"}, {1u << 6, "DT"}, {1u << 7, "RX"},
    {1u << 8, "HT"}, {1u << 9, "NC"}, {1u << 10, "FL"}, {1u << 11, "AT"},
    {1u << 12, "SO"}, {1u << 13, "AP"}, {1u << 14, "PF"}, {1u << 15, "4K"},
    {1u << 16, "5K"}, {1u << 17, "6K"}, {1u << 18, "7K"}, {1u << 19, "8K"},
    {1u << 20, "FI"}, {1u << 21, "RN"}, {1u << 22, "CN"}, {1u << 23, "TG"},
    {1u << 24, "9K"}, {1u << 25, "CO"}, {1u << 26, "1K"}, {1u << 27, "3K"},
    {1u << 28, "2K"}, {1u << 29, "V2"}, {1u << 30, "MR"}
};

} // namespace

double ReplayInfo::accuracy() const
{
    const int total = count300 + count100 + count50 + misses;
    if (total <= 0) {
        return 0.0;
    }
    const double hits = 300.0 * count300 + 100.0 * count100 + 50.0 * count50;
    return hits / (300.0 * total) * 100.0;
}

QStringList ReplayInfo::modNames() const
{
    QStringList names;
    for (const ModName &mod : MOD_NAMES) {
        if ((mods & mod.bit) == 0) {
            continue;
        }
        // NC implies DT and PF implies SD; show only the stronger one.
        if (mod.bit == (1u << 6) && (mods & (1u << 9))) {
            continue;
        }
        if (mod.bit == (1u << 5) && (mods & (1u << 14))) {
            continue;
        }
        names << QString::fromLatin1(mod.name);
    }
    return names;
}

QJsonObject ReplayInfo::toJson() const
{
    QJsonObject json;
    json["player"] = player;
    json["score"] = score;
    json["accuracy"] = accuracy();
    json["combo"] = maxCombo;
    json["misses"] = misses;
    json["maxCombo"] = maxCombo;
    json["gameMode"] = gameMode;
    json["perfect"] = perfect;
    json["modsBits"] = static_cast<double>(mods);
    json["mods"] = QJsonArray::fromStringList(modNames());
    json["replayDate"] = playedAt.isValid()
        ? playedAt.toUTC().toString(Qt::ISODate)
        : QString();
    json["replayLength"] = replayLength;

    QJsonObject counts;
    counts["300"] = count300;
    counts["100"] = count100;
    counts["50"] = count50;
    counts["geki"] = geki;
    counts["katu"] = katu;
    counts["miss"] = misses;
    json["counts"] = counts;
    return json;
}

ReplayInfo ReplayInfo::read(const QString &path, QString *errorMessage)
{
    ReplayInfo info;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = "Не удалось открыть реплей: " + path;
        }
        return info;
    }
    const QByteArray data = file.readAll();
    Reader reader(data);

    info.gameMode = reader.u8();
    info.version = reader.i32();
    info.beatmapMd5 = reader.str();
    info.player = reader.str();
    info.replayMd5 = reader.str();
    info.count300 = reader.i16();
    info.count100 = reader.i16();
    info.count50 = reader.i16();
    info.geki = reader.i16();
    info.katu = reader.i16();
    info.misses = reader.i16();
    info.score = reader.i32();
    info.maxCombo = reader.i16();
    info.perfect = reader.u8() != 0;
    info.mods = static_cast<quint32>(reader.i32());
    reader.str(); // life bar graph
    const qint64 ticks = reader.i64();
    info.replayLength = reader.i32();

    if (!reader.ok()) {
        if (errorMessage) {
            *errorMessage = "Повреждённый или неподдерживаемый .osr: " + path;
        }
        return ReplayInfo();
    }

    if (ticks > 621355968000000000LL) {
        const qint64 seconds = (ticks - 621355968000000000LL) / 10000000LL;
        info.playedAt = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
    }

    return info;
}
