#ifndef OSUPROFILEDIALOG_H
#define OSUPROFILEDIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class OsuApi;

// "Replays from osu! servers": sign in to the osu!api as a guest, list a
// player's recent or best scores and download the chosen replay (plus the
// beatmap, if it is not in Songs yet) so it can be rendered.
class OsuProfileDialog : public QDialog
{
    Q_OBJECT

public:
    struct State
    {
        QString clientId;
        QString clientSecret;
        QString accessToken;
        qint64 tokenExpiry = 0;
        QString replayDirectory;
        QString songsDirectory;
    };

    explicit OsuProfileDialog(const State &state, QWidget *parent = nullptr);

    State state() const;

signals:
    void replayReady(const QString &path, const QString &replayDirectory);

private:
    void buildUi();
    void loadScores();
    void downloadSelected();
    void fillTable(const QJsonArray &scores);
    void checkBeatmap(const QString &replayPath, const QJsonObject &beatmap,
                      const QJsonObject &beatmapset);
    QJsonObject selectedScore() const;
    void setStatus(const QString &text, bool error = false);
    void updateButtons();

    State m_state;
    OsuApi *m_api = nullptr;
    QJsonArray m_scores;

    QLineEdit *m_clientIdEdit;
    QLineEdit *m_clientSecretEdit;
    QLineEdit *m_userEdit;
    QComboBox *m_typeCombo;
    QPushButton *m_loadBtn;
    QTableWidget *m_table;
    QPushButton *m_downloadBtn;
    QCheckBox *m_mapCheck;
    QLabel *m_status;
    bool m_busy = false;
};

#endif // OSUPROFILEDIALOG_H
