// SPDX-License-Identifier: GPL-3.0-or-later
// FixMyPlanet desktop application.
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPixmap>
#include <QTabWidget>
#include <clocale>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Qt switches the C library to the system locale; number formatting in
    // files must not follow it (decimal commas).
    std::setlocale(LC_NUMERIC, "C");
    QCoreApplication::setApplicationName(QStringLiteral("FixMyPlanet"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FIXMYPLANET_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("fixmyplanet"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/fixmyplanet.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Planetary derotation"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("A session file (.json) or images to add."), QStringLiteral("[files...]"));
    // The three options below exist for automated checks of the user interface.
    QCommandLineOption measureOpt(QStringLiteral("measure"), QStringLiteral("Measure all images after loading."));
    QCommandLineOption derotateOpt(QStringLiteral("derotate"), QStringLiteral("Derotate after loading."));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a picture of the window to <file> and quit."), QStringLiteral("file"));
    parser.addOption(measureOpt);
    parser.addOption(derotateOpt);
    parser.addOption(shotOpt);
    parser.process(app);

    MainWindow window;
    window.show();

    QStringList images;
    for (const QString& arg : parser.positionalArguments()) {
        if (arg.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) window.openSessionFile(arg);
        else images << arg;
    }
    if (!images.isEmpty()) window.addImageFiles(images);
    if (parser.isSet(measureOpt)) window.measureAll();
    if (parser.isSet(derotateOpt)) window.derotateBlocking();

    if (parser.isSet(shotOpt)) {
        QString base = parser.value(shotOpt);
        app.processEvents();
        bool ok = window.grab().save(base);
        if (parser.isSet(derotateOpt)) {
            // Also the first tab, next to the requested file.
            if (auto* tabs = window.findChild<QTabWidget*>()) {
                tabs->setCurrentIndex(0);
                app.processEvents();
                QString other = base;
                other.replace(QStringLiteral(".png"), QStringLiteral("_measure.png"));
                ok = window.grab().save(other) && ok;
            }
        }
        return ok ? 0 : 1;
    }
    return app.exec();
}
