#ifndef RENDERER_H
#define RENDERER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QJsonObject>

class Renderer : public QObject
{
    Q_OBJECT

public:
    explicit Renderer(QObject *parent = nullptr);
    ~Renderer() override;

    void setDanserPath(const QString &path);
    void setReplayPath(const QString &path);
    void setSkinPath(const QString &path);
    void setSongsPath(const QString &path);
    void setOutputPath(const QString &path);
    void setFps(int fps);
    void setResolution(int width, int height);
    void setCursorScale(double scale);
    void setEncoder(const QString &encoder);
    void setCrf(int crf);
    void setSkipIntro(bool enabled);
    void setStartTime(double seconds);
    void setEndTime(double seconds);
    void setAudioOffset(int milliseconds);
    void setMotionBlur(bool enabled, int samples);
    void setVolumes(double music, double hitsounds);
    void setHudOptions(const QJsonObject &options);
    void setPlayfieldOptions(const QJsonObject &options);

    void start();
    void cancel();

signals:
    void started();
    void finished(int exitCode);
    void progress(int percent, const QString &status);
    void error(const QString &msg);

private slots:
    void onProcessStarted();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    bool prepareDanserProfile(QString *errorMessage);
    QString discoverSongsDirectory(const QJsonObject &profile) const;
    void buildArgs(const QString &profileName);
    void readProcessOutput();

    QProcess *m_process;
    QString m_danserPath;
    QString m_replayPath;
    QString m_skinPath;
    QString m_songsPath;
    QString m_outputPath;
    int m_fps;
    int m_width;
    int m_height;
    double m_cursorScale;
    QString m_encoder;
    int m_crf;
    bool m_skipIntro;
    double m_startTime;
    double m_endTime;
    int m_audioOffset;
    bool m_motionBlur;
    int m_blurSamples;
    double m_musicVolume = 0.5;
    double m_hitsoundVolume = 0.5;
    QJsonObject m_hudOptions;
    QJsonObject m_playfieldOptions;
    QStringList m_args;
};

#endif // RENDERER_H
