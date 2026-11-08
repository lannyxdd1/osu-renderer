#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QProgressBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QJsonArray>
#include "Renderer.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void browseReplay();
    void browseReplayDirectory();
    void refreshReplayList();
    void browseDanser();
    void browseSkin();
    void addSkin();
    void removeSkin();
    void selectSkin(QListWidgetItem *item);
    void saveUserPreset();
    void deleteUserPreset();
    void browseOutput();
    void startRender();
    void cancelRender();
    void onRenderStarted();
    void onRenderFinished(int exitCode);
    void onRenderProgress(int percent, const QString &status);
    void onRenderError(const QString &msg);
    void toggleCustomResolution(bool enabled);
    void saveConfig();
    void loadConfig();

public:
    void setDanserPath(const QString &path);

private:
    void setupUi();
    QJsonObject capturePreset() const;
    void applyPreset(const QJsonObject &preset);
    void refreshUserPresets();

    Renderer *m_renderer;

    // UI elements
    QLineEdit *m_replayEdit;
    QLineEdit *m_replayDirEdit;
    QComboBox *m_replayCombo;
    QLineEdit *m_danserEdit;
    QLineEdit *m_skinEdit;
    QLineEdit *m_outputEdit;
    QSpinBox *m_fpsSpin;
    QComboBox *m_resPresets;
    QSpinBox *m_widthSpin;
    QSpinBox *m_heightSpin;
    QCheckBox *m_customResCheck;
    QDoubleSpinBox *m_cursorScaleSpin;
    QComboBox *m_encoderCombo;
    QSpinBox *m_crfSpin;
    QCheckBox *m_skipIntroCheck;
    QDoubleSpinBox *m_startTimeSpin;
    QDoubleSpinBox *m_endTimeSpin;
    QSpinBox *m_audioOffsetSpin;
    QCheckBox *m_motionBlurCheck;
    QSpinBox *m_blurSamplesSpin;
    QListWidget *m_skinList;
    QCheckBox *m_hudScore, *m_hudHp, *m_hudCombo, *m_hudPp, *m_hudError, *m_hudKeys, *m_hudMods, *m_hudBoard, *m_hudHits, *m_hudStrain;
    QDoubleSpinBox *m_backgroundDim;
    QCheckBox *m_storyboardCheck, *m_backgroundVideoCheck, *m_parallaxCheck, *m_backgroundBlurCheck, *m_bloomCheck;
    QCheckBox *m_drawObjectsCheck, *m_approachCheck, *m_numbersCheck, *m_followPointsCheck, *m_hitAnimationsCheck, *m_beatScaleCheck, *m_snakeInCheck, *m_snakeOutCheck;
    QComboBox *m_presetCombo;
    QSlider *m_musicVolumeSlider;
    QSlider *m_hitsoundVolumeSlider;
    QJsonArray m_userPresets;
    QPushButton *m_startBtn;
    QPushButton *m_cancelBtn;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPlainTextEdit *m_logEdit;
};

#endif // MAINWINDOW_H
