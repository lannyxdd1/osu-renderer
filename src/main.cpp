#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include "MainWindow.h"

static bool fileExists(const QString &path)
{
    if (path.isEmpty()) return false;
    return QFileInfo::exists(path) && !QFileInfo(path).isDir();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("osu! Replay Renderer");
    app.setOrganizationName("osu_render");
    app.setApplicationVersion("1.0.0");

    const QString userName = qEnvironmentVariable("USER");

    // Search for danser in common locations
    QStringList searchPaths = {
        "danser-cli",
        "danser",
        QStandardPaths::findExecutable("danser-cli"),
        QStandardPaths::findExecutable("danser"),
        "/usr/local/bin/danser-cli",
        "/usr/bin/danser-cli",
        "/usr/local/bin/danser",
        "/usr/bin/danser",
        "/opt/danser/danser-cli",
        "/opt/danser/danser",
        "/opt/danser-go/danser-cli",
        "/opt/danser-go/danser",
        "/home/" + userName + "/go/bin/danser-cli",
        "/home/" + userName + "/go/bin/danser",
        "/home/" + userName + "/danser/danser-cli",
        "/home/" + userName + "/danser-go/danser-cli",
        "/home/" + userName + "/.local/bin/danser-cli",
        "/home/" + userName + "/.local/bin/danser",
        "/usr/local/bin/danser-cli",
        "/usr/local/bin/danser",
        "/usr/bin/danser-cli",
        "/usr/bin/danser"
    };

    QString danserPath;
    for (const QString &path : searchPaths) {
        if (!path.isEmpty() && fileExists(path)) {
            danserPath = path;
            break;
        }
    }

    MainWindow w;
    w.show();

    if (!danserPath.isEmpty()) {
        w.setDanserPath(danserPath);
    }

    return app.exec();
}
