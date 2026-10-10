// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>
#include <vector>

#include "derotate.h"
#include "session.h"

class ImageView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Used by the command line and the screenshot mode.
    bool openSessionFile(const QString& path);
    void addImageFiles(const QStringList& paths);
    void measureAll();
    void derotateBlocking();

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private slots:
    void onAddImages();
    void onRemoveImage();
    void onOpenSession();
    bool onSaveSession();
    bool onSaveSessionAs();
    void onSaveResult();
    void onSelectionChanged();
    void onTableEdited(int row, int column);
    void onGeometrySpin();
    void onViewGeometryEdited(const fixmyplanet::ImageGeometry& g);
    void onPlanetChanged();
    void onMeasureSelected();
    void onMeasureAll();
    void onEvenOut();
    void onApplyOrientationToAll();
    void onDerotate();
    void onDerotateFinished();
    void onAbout();

private:
    struct Item {
        QString path;            // absolute
        fixmyplanet::Image image;
        double jdUtc = 0;
        QString channel;
        fixmyplanet::ImageGeometry geometry;
        bool measured = false;
        double weight = 1.0;
    };

    void buildUi();
    void buildMenus();
    void refreshTable();
    void refreshRow(int row);
    void showSelected();
    void updateOverlay();
    void updateEphemerisLabel();
    void updateReferenceDisplay();
    void updateSystemCombo();
    void updateButtons();
    int currentRow() const;
    fixmyplanet::Planet planet() const;
    bool measureItem(Item& item, QString* message);
    bool collect(std::vector<fixmyplanet::Frame>& frames, fixmyplanet::DerotateOptions& options, QString& error);
    void showResult();
    void log(const QString& text);
    void setModified(bool on);
    bool maybeSave();
    fixmyplanet::Session toSession(const QString& sessionPath) const;
    void applySession(const fixmyplanet::Session& s, const QString& sessionPath);

    std::vector<Item> items_;
    QString sessionPath_;
    bool modified_ = false;
    bool updating_ = false;       // guard against feedback while widgets are being filled

    QTableWidget* table_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    ImageView* view_ = nullptr;
    ImageView* resultView_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    QComboBox* planetCombo_ = nullptr;
    QComboBox* systemCombo_ = nullptr;
    QCheckBox* rateCheck_ = nullptr;
    QDoubleSpinBox* rateSpin_ = nullptr;
    QCheckBox* refAutoCheck_ = nullptr;
    QLineEdit* refEdit_ = nullptr;

    QLabel* ephemLabel_ = nullptr;
    QDoubleSpinBox* cxSpin_ = nullptr;
    QDoubleSpinBox* cySpin_ = nullptr;
    QDoubleSpinBox* radiusSpin_ = nullptr;
    QDoubleSpinBox* angleSpin_ = nullptr;
    QCheckBox* mirrorCheck_ = nullptr;
    QPushButton* measureButton_ = nullptr;
    QPushButton* measureAllButton_ = nullptr;
    QPushButton* evenOutButton_ = nullptr;
    QPushButton* applyAllButton_ = nullptr;

    QCheckBox* ldAutoCheck_ = nullptr;
    QDoubleSpinBox* ldSpin_ = nullptr;
    QCheckBox* alignCheck_ = nullptr;
    QCheckBox* normalizeCheck_ = nullptr;
    QCheckBox* northUpCheck_ = nullptr;
    QPushButton* derotateButton_ = nullptr;
    QPushButton* saveResultButton_ = nullptr;

    QAction* gridAction_ = nullptr;
    QAction* stretchAction_ = nullptr;

    QThread* worker_ = nullptr;
    std::shared_ptr<fixmyplanet::DerotateResult> pending_;
    std::shared_ptr<fixmyplanet::DerotateResult> result_;
    std::vector<QString> resultNames_;
    std::vector<QString> resultChannels_;
    bool resultCustomRate_ = false;   // settings the result was computed with
    double resultRate_ = 0;
    QWidget* settingsPanel_ = nullptr;
};
