#include "OsuProfileDialog.h"

#include "BeatmapIndex.h"
#include "OsuApi.h"
#include "ReplayInfo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString sanitize(const QString &value)
{
    QString result = value;
    result.replace(QRegularExpression(R"([\\/:*?"<>|\x00-\x1f])"), "_");
    result = result.simplified();
    if (result.size() > 150) {
        result.truncate(150);
    }
    return result;
}

QString mapTitle(const QJsonObject &score)
{
    const QJsonObject beatmapset = score.value("beatmapset").toObject();
    const QJsonObject beatmap = score.value("beatmap").toObject();
    const QString artist = beatmapset.value("artist").toString();
    const QString title = beatmapset.value("title").toString();
    const QString version = beatmap.value("version").toString();
    QString text = artist.isEmpty() && title.isEmpty()
        ? QString("Карта #%1").arg(beatmap.value("id").toInteger())
        : artist + " - " + title;
    if (!version.isEmpty()) {
        text += " [" + version + "]";
    }
    return text;
}

QString modsText(const QJsonObject &score)
{
    QStringList mods;
    const QJsonArray list = score.value("mods").toArray();
    for (const QJsonValue &value : list) {
        const QString acronym = value.toObject().value("acronym").toString();
        if (!acronym.isEmpty()) {
            mods << acronym;
        }
    }
    return mods.isEmpty() ? "—" : mods.join(' ');
}

QString dateText(const QJsonObject &score)
{
    const QString raw = score.value("ended_at").toString();
    if (raw.isEmpty()) {
        return "—";
    }
    QDateTime date = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (!date.isValid()) {
        date = QDateTime::fromString(raw, Qt::ISODate);
    }
    return date.isValid() ? date.toLocalTime().toString("dd.MM.yyyy HH:mm") : raw;
}

qint64 scoreValue(const QJsonObject &score)
{
    qint64 value = score.value("total_score").toInteger();
    if (value <= 0) {
        value = score.value("legacy_total_score").toInteger();
    }
    return value;
}

QTableWidgetItem *makeItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

OsuProfileDialog::OsuProfileDialog(const State &state, QWidget *parent)
    : QDialog(parent)
    , m_state(state)
{
    setWindowTitle("Реплеи с серверов osu!");
    resize(900, 560);
    m_api = new OsuApi(this);
    connect(m_api, &OsuApi::status, this, [this](const QString &message) {
        setStatus(message);
    });
    buildUi();

    m_clientIdEdit->setText(state.clientId);
    m_clientSecretEdit->setText(state.clientSecret);
    m_api->setCredentials({state.clientId, state.clientSecret,
                           state.accessToken, state.tokenExpiry});
}

void OsuProfileDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    auto *credentials = new QFormLayout;
    m_clientIdEdit = new QLineEdit(this);
    m_clientIdEdit->setPlaceholderText("Client ID OAuth-приложения");
    credentials->addRow("Client ID:", m_clientIdEdit);
    m_clientSecretEdit = new QLineEdit(this);
    m_clientSecretEdit->setEchoMode(QLineEdit::Password);
    m_clientSecretEdit->setPlaceholderText("Client Secret");
    credentials->addRow("Client secret:", m_clientSecretEdit);
    layout->addLayout(credentials);

    auto *hint = new QLabel(
        "Создайте приложение на <a href=\"https://osu.ppy.sh/home/account/edit#oauth\">"
        "osu.ppy.sh → Settings → OAuth</a>: имя любое, Callback URL можно оставить пустым. "
        "Достаточно гостевого доступа (scope <b>public</b>).", this);
    hint->setWordWrap(true);
    hint->setOpenExternalLinks(true);
    layout->addWidget(hint);

    auto *profileRow = new QHBoxLayout;
    m_userEdit = new QLineEdit(this);
    m_userEdit->setPlaceholderText("Ник на osu! (или ID)");
    profileRow->addWidget(m_userEdit);
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem("Последние игры", "recent");
    m_typeCombo->addItem("Лучшие игры", "best");
    profileRow->addWidget(m_typeCombo);
    m_loadBtn = new QPushButton("Загрузить", this);
    profileRow->addWidget(m_loadBtn);
    layout->addLayout(profileRow);

    m_table = new QTableWidget(0, 7, this);
    m_table->setHorizontalHeaderLabels(
        {"Дата", "Карта", "Моды", "Точность", "Счёт", "PP", "Реплей"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column : {0, 2, 3, 4, 5, 6}) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    layout->addWidget(m_table, 1);

    auto *bottomRow = new QHBoxLayout;
    m_mapCheck = new QCheckBox("Скачивать карту, если её нет в Songs", this);
    m_mapCheck->setChecked(true);
    bottomRow->addWidget(m_mapCheck);
    bottomRow->addStretch();
    m_downloadBtn = new QPushButton("Скачать реплей", this);
    bottomRow->addWidget(m_downloadBtn);
    layout->addLayout(bottomRow);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    connect(m_loadBtn, &QPushButton::clicked, this, &OsuProfileDialog::loadScores);
    connect(m_downloadBtn, &QPushButton::clicked, this, &OsuProfileDialog::downloadSelected);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &OsuProfileDialog::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this]() {
        downloadSelected();
    });
    connect(m_userEdit, &QLineEdit::returnPressed, this, &OsuProfileDialog::loadScores);

    updateButtons();
}

OsuProfileDialog::State OsuProfileDialog::state() const
{
    State result = m_state;
    result.clientId = m_clientIdEdit->text().trimmed();
    result.clientSecret = m_clientSecretEdit->text().trimmed();
    const OsuApi::Credentials credentials = m_api->credentials();
    result.accessToken = credentials.accessToken;
    result.tokenExpiry = credentials.expiresAt;
    return result;
}

void OsuProfileDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? "color: #e07b7b;" : QString());
}

void OsuProfileDialog::updateButtons()
{
    const QJsonObject score = selectedScore();
    const bool hasReplay = score.value("has_replay").toBool();
    m_downloadBtn->setEnabled(!m_busy && !score.isEmpty() && hasReplay);
    m_loadBtn->setEnabled(!m_busy);

    if (!score.isEmpty() && !hasReplay) {
        setStatus("У этой игры нет сохранённого реплея на сервере osu!.");
    }
}

void OsuProfileDialog::loadScores()
{
    if (m_busy) {
        return;
    }

    const QString user = m_userEdit->text().trimmed();
    if (user.isEmpty()) {
        setStatus("Введите ник игрока на osu!.", true);
        m_userEdit->setFocus();
        return;
    }
    if (m_clientIdEdit->text().trimmed().isEmpty()
        || m_clientSecretEdit->text().trimmed().isEmpty()) {
        setStatus("Сначала укажите Client ID и Client Secret.", true);
        m_clientIdEdit->setFocus();
        return;
    }

    m_busy = true;
    updateButtons();
    setStatus("Запрашиваю игры игрока " + user + "...");
    const QString clientId = m_clientIdEdit->text().trimmed();
    const QString clientSecret = m_clientSecretEdit->text().trimmed();
    if (clientId != m_state.clientId || clientSecret != m_state.clientSecret) {
        // A token issued for another client is useless.
        m_state.accessToken.clear();
        m_state.tokenExpiry = 0;
    }
    m_api->setCredentials({clientId, clientSecret, m_state.accessToken, m_state.tokenExpiry});

    m_api->fetchScores(user, m_typeCombo->currentData().toString(), 50,
                       [this](const QJsonArray &scores, const QString &username,
                              const QString &error) {
        m_busy = false;
        const OsuApi::Credentials credentials = m_api->credentials();
        m_state.accessToken = credentials.accessToken;
        m_state.tokenExpiry = credentials.expiresAt;

        if (!error.isEmpty()) {
            setStatus("osu!api: " + error, true);
            updateButtons();
            return;
        }

        m_username = username;
        fillTable(scores);
        setStatus(QString("Игрок %1: загружено игр %2. Выберите игру и нажмите «Скачать реплей».")
                      .arg(username)
                      .arg(static_cast<int>(scores.size())));
        updateButtons();
    });
}

void OsuProfileDialog::fillTable(const QJsonArray &scores)
{
    m_scores = scores;
    m_table->setRowCount(0);

    for (const QJsonValue &value : scores) {
        const QJsonObject score = value.toObject();
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *dateItem = makeItem(dateText(score));
        dateItem->setData(Qt::UserRole, score);
        m_table->setItem(row, 0, dateItem);
        m_table->setItem(row, 1, makeItem(mapTitle(score)));
        m_table->setItem(row, 2, makeItem(modsText(score)));

        const double accuracy = score.value("accuracy").toDouble() * 100.0;
        m_table->setItem(row, 3, makeItem(QString("%1%").arg(accuracy, 0, 'f', 2)));
        m_table->setItem(row, 4, makeItem(QString::number(scoreValue(score))));

        const QJsonValue pp = score.value("pp");
        m_table->setItem(row, 5, makeItem(
            pp.isDouble() && pp.toDouble() > 0.0
                ? QString::number(pp.toDouble(), 'f', 0)
                : QString("—")));
        m_table->setItem(row, 6, makeItem(score.value("has_replay").toBool() ? "да" : "нет"));
    }

    if (m_table->rowCount() > 0) {
        m_table->selectRow(0);
    }
}

QJsonObject OsuProfileDialog::selectedScore() const
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return {};
    }
    QTableWidgetItem *item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toJsonObject() : QJsonObject();
}

void OsuProfileDialog::downloadSelected()
{
    if (m_busy) {
        return;
    }
    const QJsonObject score = selectedScore();
    if (score.isEmpty()) {
        return;
    }
    if (!score.value("has_replay").toBool()) {
        setStatus("У этой игры нет сохранённого реплея на сервере osu!.", true);
        return;
    }

    const qint64 scoreId = score.value("id").toInteger();
    const qint64 legacyScoreId = score.value("legacy_score_id").toInteger(-1);
    if (scoreId <= 0) {
        setStatus("osu! не вернул ID игры.", true);
        return;
    }

    QString directory = m_state.replayDirectory;
    if (directory.isEmpty() || !QDir(directory).exists()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                    + "/osu_render/replays";
        QDir().mkpath(directory);
    }
    m_state.replayDirectory = directory;

    const QJsonObject beatmap = score.value("beatmap").toObject();
    const QJsonObject beatmapset = score.value("beatmapset").toObject();
    const QString player = m_username.isEmpty()
        ? m_userEdit->text().trimmed()
        : m_username;
    const QString artist = beatmapset.value("artist").toString(beatmap.value("artist").toString());
    const QString title = beatmapset.value("title").toString(beatmap.value("title").toString());
    const QString version = beatmap.value("version").toString();

    const QString dateRaw = score.value("ended_at").toString();
    const QDateTime ended = QDateTime::fromString(dateRaw, Qt::ISODateWithMs);
    const QString date = (ended.isValid() ? ended : QDateTime::currentDateTime())
                             .toString("yyyy-MM-dd");

    QString fileName = QString("%1 - %2 - %3 [%4] (%5) Osu.osr")
                           .arg(player, artist, title, version, date);
    const QString target = QDir(directory).filePath(sanitize(fileName));

    m_busy = true;
    updateButtons();
    setStatus("Скачиваю реплей...");

    m_api->downloadReplay(scoreId, legacyScoreId, target,
                          [this, target, beatmap, beatmapset](const QString &error) {
        m_busy = false;
        if (!error.isEmpty()) {
            setStatus("Реплей не скачался — " + error, true);
            updateButtons();
            return;
        }
        if (!QFileInfo(target).isFile()) {
            setStatus("osu! не отдал файл реплея.", true);
            updateButtons();
            return;
        }

        setStatus("Реплей сохранён: " + target);
        emit replayReady(target, m_state.replayDirectory);
        updateButtons();
        checkBeatmap(target, beatmap, beatmapset);
    });
}

void OsuProfileDialog::checkBeatmap(const QString &replayPath, const QJsonObject &beatmap,
                                    const QJsonObject &beatmapset)
{
    QString songs = m_state.songsDirectory;
    if (songs.isEmpty() || !QDir(songs).exists()) {
        songs = detectSongsDirectory(songs);
        m_state.songsDirectory = songs;
    }
    if (songs.isEmpty()) {
        setStatus(m_status->text() + "\nПапка Songs не найдена — проверить карту не удалось.", true);
        return;
    }

    QString replayError;
    const ReplayInfo replay = ReplayInfo::read(replayPath, &replayError);
    if (!replayError.isEmpty()) {
        setStatus(m_status->text() + "\nРеплей сохранён, но прочитать его не удалось: "
                  + replayError, true);
        return;
    }

    BeatmapIndex index;
    const BeatmapInfo info = index.find(replay.beatmapMd5, songs);
    if (!info.path.isEmpty()) {
        updateButtons();
        return;
    }

    if (!m_mapCheck->isChecked()) {
        setStatus(m_status->text() + "\nКарты нет в Songs — рендер не запустится, "
                  "пока вы её не скачаете.", true);
        return;
    }

    const qint64 setId = beatmapset.value("id").toInteger(beatmap.value("beatmapset_id").toInteger());
    if (setId <= 0) {
        setStatus(m_status->text() + "\nКарты нет в Songs, а её ID неизвестен.", true);
        return;
    }

    const QString artist = beatmapset.value("artist").toString(beatmap.value("artist").toString());
    const QString title = beatmapset.value("title").toString(beatmap.value("title").toString());
    const QString folder = sanitize(QString("%1 %2 - %3").arg(setId).arg(artist, title));

    setStatus("Карты нет в Songs. Скачиваю " + artist + " - " + title + "...");
    m_api->downloadBeatmapset(setId, folder, songs,
                              [this](const QString &directory, const QString &error) {
        if (!error.isEmpty()) {
            setStatus(m_status->text() + "\nКарта не скачалась: " + error, true);
            return;
        }
        setStatus(m_status->text() + "\nКарта распакована: " + directory);
    });
}
