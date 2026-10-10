// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>
#include <new>

#include "ephem.h"
#include "imageio.h"
#include "imageview.h"
#include "measure.h"
#include "overlay.h"
#include "strutil.h"
#include "timeutil.h"

using namespace fixmyplanet;

namespace {

std::string utf8(const QString& s) { return s.toUtf8().toStdString(); }
QString qs(const std::string& s) { return QString::fromUtf8(s.c_str()); }

const char* kImageFilter = "Images (*.tif *.tiff *.png *.fit *.fits *.fts *.jpg *.jpeg *.bmp);;All files (*)";

enum Column { ColFile = 0, ColTime, ColFilter, ColMeasured, ColCount };

QDoubleSpinBox* makeSpin(double lo, double hi, double step, int decimals, const QString& suffix = QString()) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    s->setAlignment(Qt::AlignRight);
    return s;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    qRegisterMetaType<fixmyplanet::ImageGeometry>("fixmyplanet::ImageGeometry");
    setAcceptDrops(true);
    buildUi();
    buildMenus();
    onPlanetChanged();
    updateButtons();
    setModified(false);
    resize(1320, 820);
    statusBar()->showMessage(tr("Add the stacked images of one session to begin."));
}

MainWindow::~MainWindow() {
    if (worker_) {
        worker_->wait();
        delete worker_;
    }
}

// ----------------------------------------------------------------- layout --

void MainWindow::buildUi() {
    // Left: the images of the session.
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 6, 0, 6);
    table_ = new QTableWidget(0, ColCount);
    table_->setHorizontalHeaderLabels({tr("Image"), tr("Time (UTC)"), tr("Filter"), tr("Measured")});
    table_->horizontalHeader()->setSectionResizeMode(ColFile, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColTime, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColFilter, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ColMeasured, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setToolTip(tr("Double-click a time or filter to change it. Times are UTC, the middle of each capture."));
    leftLayout->addWidget(table_, 1);
    auto* listButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add images…"));
    auto* removeButton = new QPushButton(tr("Remove"));
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    listButtons->addStretch(1);
    leftLayout->addLayout(listButtons);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddImages);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::onRemoveImage);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(table_, &QTableWidget::cellChanged, this, &MainWindow::onTableEdited);

    // Middle: the image being measured, and the result.
    tabs_ = new QTabWidget;
    view_ = new ImageView;
    view_->setPlaceholder(tr("Add the stacked images of one session.\n\n"
                             "Drag the outline to move it. Ctrl + wheel changes its size, Shift + wheel turns it.\n"
                             "Arrow keys move it by 0.1 pixel, Page Up/Down change the size, Home/End turn it\n"
                             "(with Shift: ten times as much)."));
    resultView_ = new ImageView;
    resultView_->setPlaceholder(tr("The derotated image appears here."));
    tabs_->addTab(view_, tr("Measure"));
    tabs_->addTab(resultView_, tr("Result"));
    connect(view_, &ImageView::geometryEdited, this, &MainWindow::onViewGeometryEdited);

    // Right: settings.
    auto* panel = new QWidget;
    settingsPanel_ = panel;
    auto* panelLayout = new QVBoxLayout(panel);

    auto* planetBox = new QGroupBox(tr("Planet"));
    auto* planetForm = new QFormLayout(planetBox);
    planetCombo_ = new QComboBox;
    for (Planet p : allPlanets()) planetCombo_->addItem(qs(planetInfo(p).name), static_cast<int>(p));
    planetCombo_->setCurrentIndex(planetCombo_->findData(static_cast<int>(Planet::Jupiter)));
    systemCombo_ = new QComboBox;
    rateCheck_ = new QCheckBox(tr("Own rate"));
    rateCheck_->setToolTip(tr("Use this rotation rate instead of a longitude system, for features that drift."));
    rateSpin_ = makeSpin(-2000, 2000, 0.1, 4, tr(" °/day"));
    rateSpin_->setEnabled(false);
    auto* rateRow = new QHBoxLayout;
    rateRow->addWidget(rateCheck_);
    rateRow->addWidget(rateSpin_, 1);
    refAutoCheck_ = new QCheckBox(tr("Middle of the session"));
    refAutoCheck_->setChecked(true);
    refEdit_ = new QLineEdit;
    refEdit_->setPlaceholderText(QStringLiteral("2026-01-10T22:08:00"));
    refEdit_->setEnabled(false);
    planetForm->addRow(tr("Planet"), planetCombo_);
    planetForm->addRow(tr("Longitude system"), systemCombo_);
    planetForm->addRow(rateRow);
    planetForm->addRow(tr("Derotate to"), refAutoCheck_);
    planetForm->addRow(tr("Time (UTC)"), refEdit_);
    panelLayout->addWidget(planetBox);
    connect(planetCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::onPlanetChanged);
    connect(systemCombo_, &QComboBox::currentIndexChanged, this, [this] { if (!updating_) setModified(true); });
    connect(rateCheck_, &QCheckBox::toggled, this, [this](bool on) {
        rateSpin_->setEnabled(on);
        systemCombo_->setEnabled(!on);
        if (!updating_) setModified(true);
    });
    connect(rateSpin_, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) setModified(true); });
    connect(refAutoCheck_, &QCheckBox::toggled, this, [this](bool on) {
        refEdit_->setEnabled(!on);
        updateReferenceDisplay();
        if (!updating_) setModified(true);
    });
    connect(refEdit_, &QLineEdit::textEdited, this, [this] { setModified(true); });

    auto* imageBox = new QGroupBox(tr("Selected image"));
    auto* imageForm = new QFormLayout(imageBox);
    cxSpin_ = makeSpin(-10000, 20000, 0.1, 2, tr(" px"));
    cySpin_ = makeSpin(-10000, 20000, 0.1, 2, tr(" px"));
    radiusSpin_ = makeSpin(3, 20000, 0.1, 2, tr(" px"));
    angleSpin_ = makeSpin(-180, 180, 0.1, 2, tr(" °"));
    angleSpin_->setWrapping(true);
    angleSpin_->setToolTip(tr("Direction of the planet's north pole, counter-clockwise from up.\n"
                              "0 = north up, 180 = south up. A rough value is enough before measuring."));
    mirrorCheck_ = new QCheckBox(tr("Mirror-reversed image"));
    mirrorCheck_->setToolTip(tr("Tick when the image is flipped, for example by a star diagonal.\n"
                                "The rotation carries features towards the limb marked p."));
    imageForm->addRow(tr("Centre X"), cxSpin_);
    imageForm->addRow(tr("Centre Y"), cySpin_);
    imageForm->addRow(tr("Equatorial radius"), radiusSpin_);
    imageForm->addRow(tr("North angle"), angleSpin_);
    imageForm->addRow(mirrorCheck_);
    measureButton_ = new QPushButton(tr("Measure this image"));
    measureAllButton_ = new QPushButton(tr("Measure all"));
    evenOutButton_ = new QPushButton(tr("Even out"));
    evenOutButton_->setToolTip(tr("Give all images the median radius and a north angle that changes smoothly with time."));
    applyAllButton_ = new QPushButton(tr("Angle and mirror to all"));
    applyAllButton_->setToolTip(tr("Copy this image's north angle and mirror setting to every image."));
    auto* m1 = new QHBoxLayout;
    m1->addWidget(measureButton_);
    m1->addWidget(measureAllButton_);
    auto* m2 = new QHBoxLayout;
    m2->addWidget(applyAllButton_);
    m2->addWidget(evenOutButton_);
    imageForm->addRow(m1);
    imageForm->addRow(m2);
    ephemLabel_ = new QLabel;
    ephemLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ephemLabel_->setMinimumHeight(5 * ephemLabel_->fontMetrics().lineSpacing() + 4);
    ephemLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    imageForm->addRow(ephemLabel_);
    panelLayout->addWidget(imageBox);
    for (QDoubleSpinBox* s : {cxSpin_, cySpin_, radiusSpin_, angleSpin_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::onGeometrySpin);
    connect(mirrorCheck_, &QCheckBox::toggled, this, &MainWindow::onGeometrySpin);
    connect(measureButton_, &QPushButton::clicked, this, &MainWindow::onMeasureSelected);
    connect(measureAllButton_, &QPushButton::clicked, this, &MainWindow::onMeasureAll);
    connect(evenOutButton_, &QPushButton::clicked, this, &MainWindow::onEvenOut);
    connect(applyAllButton_, &QPushButton::clicked, this, &MainWindow::onApplyOrientationToAll);

    auto* runBox = new QGroupBox(tr("Derotation"));
    auto* runForm = new QFormLayout(runBox);
    ldAutoCheck_ = new QCheckBox(tr("Measure on the images"));
    ldAutoCheck_->setChecked(true);
    ldSpin_ = makeSpin(0, 1, 0.05, 2);
    ldSpin_->setEnabled(false);
    ldSpin_->setToolTip(tr("0 = no compensation, 1 = brightness falls to zero at the limb."));
    alignCheck_ = new QCheckBox(tr("Fine-tune the image positions"));
    alignCheck_->setChecked(true);
    normalizeCheck_ = new QCheckBox(tr("Match brightness between images"));
    normalizeCheck_->setChecked(true);
    northUpCheck_ = new QCheckBox(tr("Output centred, north up"));
    derotateButton_ = new QPushButton(tr("Derotate"));
    derotateButton_->setDefault(true);
    saveResultButton_ = new QPushButton(tr("Save result…"));
    runForm->addRow(tr("Limb darkening"), ldAutoCheck_);
    runForm->addRow(tr("Coefficient"), ldSpin_);
    runForm->addRow(alignCheck_);
    runForm->addRow(normalizeCheck_);
    runForm->addRow(northUpCheck_);
    auto* r1 = new QHBoxLayout;
    r1->addWidget(derotateButton_);
    r1->addWidget(saveResultButton_);
    runForm->addRow(r1);
    panelLayout->addWidget(runBox);
    panelLayout->addStretch(1);
    connect(ldAutoCheck_, &QCheckBox::toggled, this, [this](bool on) {
        ldSpin_->setEnabled(!on);
        if (!updating_) setModified(true);
    });
    for (QCheckBox* c : {alignCheck_, normalizeCheck_, northUpCheck_})
        connect(c, &QCheckBox::toggled, this, [this] { if (!updating_) setModified(true); });
    connect(ldSpin_, &QDoubleSpinBox::valueChanged, this, [this] { if (!updating_) setModified(true); });
    connect(derotateButton_, &QPushButton::clicked, this, &MainWindow::onDerotate);
    connect(saveResultButton_, &QPushButton::clicked, this, &MainWindow::onSaveResult);

    auto* scroll = new QScrollArea;
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(330);

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(tabs_);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({360, 620, 340});
    setCentralWidget(splitter);

    auto* dock = new QDockWidget(tr("Report"), this);
    dock->setObjectName(QStringLiteral("reportDock"));
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMaximumBlockCount(2000);
    dock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    resizeDocks({dock}, {170}, Qt::Vertical);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Add images…"), this, &MainWindow::onAddImages)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    file->addSeparator();
    file->addAction(tr("&Open session…"), this, &MainWindow::onOpenSession)->setShortcut(QKeySequence::Open);
    file->addAction(tr("&Save session"), this, &MainWindow::onSaveSession)->setShortcut(QKeySequence::Save);
    file->addAction(tr("Save session &as…"), this, &MainWindow::onSaveSessionAs)->setShortcut(QKeySequence::SaveAs);
    file->addSeparator();
    file->addAction(tr("Save &result…"), this, &MainWindow::onSaveResult)->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    file->addSeparator();
    file->addAction(tr("&Quit"), this, &QWidget::close)->setShortcut(QKeySequence::Quit);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    auto current = [this]() { return tabs_->currentIndex() == 0 ? view_ : resultView_; };
    view->addAction(tr("Zoom &in"), this, [current] { current()->zoomIn(); })->setShortcut(QKeySequence::ZoomIn);
    view->addAction(tr("Zoom &out"), this, [current] { current()->zoomOut(); })->setShortcut(QKeySequence::ZoomOut);
    view->addAction(tr("&Fit to window"), this, [current] { current()->zoomToFit(); })->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    view->addAction(tr("&Actual size"), this, [current] { current()->zoomActual(); })->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    view->addSeparator();
    gridAction_ = view->addAction(tr("&Latitude lines"));
    gridAction_->setCheckable(true);
    gridAction_->setChecked(true);
    connect(gridAction_, &QAction::toggled, this, [this](bool on) { view_->setGridVisible(on); });
    stretchAction_ = view->addAction(tr("&Brighten dark images"));
    stretchAction_->setCheckable(true);
    connect(stretchAction_, &QAction::toggled, this, [this](bool on) {
        view_->setStretch(on);
        resultView_->setStretch(on);
    });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About FixMyPlanet"), this, &MainWindow::onAbout);
}

// ---------------------------------------------------------------- helpers --

Planet MainWindow::planet() const { return static_cast<Planet>(planetCombo_->currentData().toInt()); }

int MainWindow::currentRow() const {
    int row = table_->currentRow();
    return row >= 0 && row < static_cast<int>(items_.size()) ? row : -1;
}

void MainWindow::log(const QString& text) { log_->appendPlainText(text); }

void MainWindow::setModified(bool on) {
    modified_ = on;
    QString name = sessionPath_.isEmpty() ? tr("new session") : QFileInfo(sessionPath_).fileName();
    setWindowTitle(QStringLiteral("%1%2 - FixMyPlanet").arg(name, on ? QStringLiteral("*") : QString()));
}

void MainWindow::updateButtons() {
    const bool any = !items_.empty();
    const bool sel = currentRow() >= 0;
    const bool busy = worker_ != nullptr;
    // Nothing may change under a running derotation.
    settingsPanel_->setEnabled(!busy);
    table_->setEnabled(!busy);
    for (QWidget* w : {static_cast<QWidget*>(cxSpin_), static_cast<QWidget*>(cySpin_), static_cast<QWidget*>(radiusSpin_),
                       static_cast<QWidget*>(angleSpin_), static_cast<QWidget*>(mirrorCheck_),
                       static_cast<QWidget*>(measureButton_), static_cast<QWidget*>(applyAllButton_)})
        w->setEnabled(sel && !busy);
    measureAllButton_->setEnabled(any && !busy);
    evenOutButton_->setEnabled(items_.size() > 1 && !busy);
    derotateButton_->setEnabled(any && !busy);
    derotateButton_->setText(busy ? tr("Working…") : tr("Derotate"));
    saveResultButton_->setEnabled(result_ && result_->ok && !busy);
}

void MainWindow::updateSystemCombo() {
    updating_ = true;
    const PlanetInfo& info = planetInfo(planet());
    systemCombo_->clear();
    for (const RotationSystem& s : info.systems)
        systemCombo_->addItem(tr("System %1 - %2").arg(qs(s.name), qs(s.note)), qs(s.name));
    systemCombo_->setCurrentIndex(info.defaultSystem);
    rateSpin_->setValue(info.systems[info.defaultSystem].rate);
    updating_ = false;
}

void MainWindow::onPlanetChanged() {
    const bool wasUpdating = updating_;
    updateSystemCombo();
    updating_ = wasUpdating;
    updateOverlay();
    updateEphemerisLabel();
    if (!updating_) setModified(true);
}

void MainWindow::refreshRow(int row) {
    if (row < 0 || row >= static_cast<int>(items_.size())) return;
    const Item& it = items_[row];
    updating_ = true;
    auto set = [&](int col, const QString& text, bool editable, const QString& tip = QString()) {
        QTableWidgetItem* cell = table_->item(row, col);
        if (!cell) {
            cell = new QTableWidgetItem;
            table_->setItem(row, col, cell);
        }
        cell->setText(text);
        cell->setToolTip(tip);
        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
        if (editable) flags |= Qt::ItemIsEditable;
        cell->setFlags(flags);
    };
    set(ColFile, QFileInfo(it.path).fileName(), false, it.path);
    set(ColTime, it.jdUtc > 0 ? qs(formatTime(it.jdUtc, 0)).chopped(1).replace('T', ' ') : tr("(set time)"), true);
    set(ColFilter, it.channel, true);
    set(ColMeasured, it.measured ? tr("yes") : tr("no"), false);
    updating_ = false;
}

void MainWindow::refreshTable() {
    updating_ = true;
    table_->setRowCount(static_cast<int>(items_.size()));
    updating_ = false;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) refreshRow(i);
}

void MainWindow::showSelected() {
    int row = currentRow();
    updating_ = true;
    if (row < 0) {
        view_->setImage(Image());
        view_->clearOverlay();
    } else {
        const Item& it = items_[row];
        view_->setImage(it.image);
        cxSpin_->setValue(it.geometry.cx);
        cySpin_->setValue(it.geometry.cy);
        radiusSpin_->setValue(it.geometry.radius);
        angleSpin_->setValue(it.geometry.northAngle);
        mirrorCheck_->setChecked(it.geometry.mirror);
    }
    updating_ = false;
    updateOverlay();
    updateEphemerisLabel();
    updateButtons();
}

void MainWindow::updateOverlay() {
    int row = currentRow();
    if (row < 0) return;
    const Item& it = items_[row];
    const PlanetInfo& info = planetInfo(planet());
    // Without a time the tilt is unknown; draw the outline for an equator-on view.
    double de = 0.0;
    Vec3 sun{0, 0, 1};
    if (it.jdUtc > 0) {
        PhysicalEphemeris e = computeEphemeris(planet(), it.jdUtc);
        de = e.de;
        sun = e.sunDirView;
    }
    std::vector<double> marks;
    for (double km : info.ringMarksKm) marks.push_back(km / info.equatorialRadiusKm);
    view_->setFeaturesMoveTowardsPlusX(info.systems[info.iauSystem].rate > 0);
    view_->setOverlay(overlayCurves(bodyShape(planet()), de, sun, true, marks), it.geometry, true);
}

// While the reference time is automatic, show what it will be.
void MainWindow::updateReferenceDisplay() {
    if (!refAutoCheck_->isChecked()) return;
    double sum = 0, sw = 0, base = 0;
    for (const Item& it : items_)
        if (it.jdUtc > 0) {
            if (sw == 0) base = it.jdUtc;
            sum += it.weight * (it.jdUtc - base);
            sw += it.weight;
        }
    refEdit_->setText(sw > 0 ? qs(formatTime(base + sum / sw, 0)).chopped(1) : QString());
}

void MainWindow::updateEphemerisLabel() {
    updateReferenceDisplay();
    int row = currentRow();
    if (row < 0 || !(items_[row].jdUtc > 0)) {
        ephemLabel_->setText(row < 0 ? QString() : tr("Set the capture time of this image to see the planet's orientation."));
        return;
    }
    PhysicalEphemeris e = computeEphemeris(planet(), items_[row].jdUtc);
    const PlanetInfo& info = planetInfo(planet());
    QStringList lines;
    QStringList cms;
    for (size_t s = 0; s < info.systems.size(); ++s) cms << tr("%1: %2°").arg(qs(info.systems[s].name)).arg(e.cm[s], 0, 'f', 1);
    lines << tr("Central meridian  %1").arg(cms.join(QStringLiteral("   ")));
    lines << tr("Tilt towards Earth (D_E)  %1°").arg(e.de, 0, 'f', 2);
    lines << tr("North pole angle on the sky  %1°").arg(e.poleAngle, 0, 'f', 1);
    lines << tr("Diameter  %1″   phase angle  %2°").arg(e.diameterArcsec, 0, 'f', 2).arg(e.phaseAngleDeg, 0, 'f', 1);
    if (items_[row].measured && items_[row].geometry.radius > 0)
        lines << tr("Image scale  %1″/px").arg(0.5 * e.diameterArcsec / items_[row].geometry.radius, 0, 'f', 4);
    ephemLabel_->setText(lines.join('\n'));
}

// ------------------------------------------------------------ image list --

void MainWindow::addImageFiles(const QStringList& paths) {
    QStringList problems;
    int added = 0, noTime = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QString& p : paths) {
        Item it;
        it.path = QFileInfo(p).absoluteFilePath();
        ImageFileInfo info;
        std::string err;
        if (!loadImage(utf8(it.path), it.image, &info, err)) {
            problems << qs(err);
            continue;
        }
        if (!timeFromFileName(utf8(it.path), it.jdUtc)) parseTime(info.dateObs, it.jdUtc);
        if (!(it.jdUtc > 0)) ++noTime;
        it.channel = qs(channelFromFileName(utf8(it.path)));
        // Start with a guess in the middle of the frame, taking over the
        // orientation of the images already there.
        it.geometry.cx = 0.5 * (it.image.width() - 1);
        it.geometry.cy = 0.5 * (it.image.height() - 1);
        it.geometry.radius = 0.3 * std::min(it.image.width(), it.image.height());
        if (!items_.empty()) {
            it.geometry.northAngle = items_.back().geometry.northAngle;
            it.geometry.mirror = items_.back().geometry.mirror;
        }
        items_.push_back(std::move(it));
        ++added;
    }
    QApplication::restoreOverrideCursor();
    if (added > 0) {
        // Keep the list in time order; images without a time go last.
        std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
            if ((a.jdUtc > 0) != (b.jdUtc > 0)) return a.jdUtc > 0;
            return a.jdUtc < b.jdUtc;
        });
        refreshTable();
        if (currentRow() < 0) table_->selectRow(0);
        showSelected();
        setModified(true);
        QString msg = tr("%n image(s) added.", nullptr, added);
        if (noTime > 0) msg += QLatin1Char(' ') + tr("%n of them have no time in the file name: double-click the time column to enter it.", nullptr, noTime);
        statusBar()->showMessage(msg, 15000);
    }
    if (!problems.isEmpty()) QMessageBox::warning(this, tr("Could not read"), problems.join('\n'));
    updateButtons();
}

void MainWindow::onAddImages() {
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Add images"), QString(), tr(kImageFilter));
    if (!files.isEmpty()) addImageFiles(files);
}

void MainWindow::onRemoveImage() {
    int row = currentRow();
    if (row < 0) return;
    items_.erase(items_.begin() + row);
    refreshTable();
    const int last = static_cast<int>(items_.size()) - 1;
    if (last >= 0) table_->selectRow(row < last ? row : last);
    showSelected();
    setModified(true);
}

void MainWindow::onSelectionChanged() {
    if (updating_) return;
    showSelected();
}

void MainWindow::onTableEdited(int row, int column) {
    if (updating_ || row < 0 || row >= static_cast<int>(items_.size())) return;
    Item& it = items_[row];
    const QString text = table_->item(row, column)->text().trimmed();
    if (column == ColTime) {
        double jd = 0;
        if (parseTime(utf8(text), jd)) {
            it.jdUtc = jd;
            setModified(true);
        } else {
            statusBar()->showMessage(tr("Times look like 2026-01-10 22:03:30 (UTC)."), 8000);
        }
    } else if (column == ColFilter) {
        it.channel = text.toUpper();
        setModified(true);
    }
    refreshRow(row);
    if (row == currentRow()) {
        updateOverlay();
        updateEphemerisLabel();
    }
}

// ------------------------------------------------------------ measurement --

void MainWindow::onGeometrySpin() {
    if (updating_) return;
    int row = currentRow();
    if (row < 0) return;
    Item& it = items_[row];
    it.geometry.cx = cxSpin_->value();
    it.geometry.cy = cySpin_->value();
    it.geometry.radius = radiusSpin_->value();
    it.geometry.northAngle = angleSpin_->value();
    it.geometry.mirror = mirrorCheck_->isChecked();
    it.measured = true;
    view_->setGeometry2(it.geometry);
    refreshRow(row);
    updateEphemerisLabel();
    setModified(true);
}

void MainWindow::onViewGeometryEdited(const ImageGeometry& g) {
    int row = currentRow();
    if (row < 0) return;
    Item& it = items_[row];
    it.geometry = g;
    it.measured = true;
    updating_ = true;
    cxSpin_->setValue(g.cx);
    cySpin_->setValue(g.cy);
    radiusSpin_->setValue(g.radius);
    angleSpin_->setValue(g.northAngle);
    updating_ = false;
    refreshRow(row);
    updateEphemerisLabel();
    setModified(true);
}

bool MainWindow::measureItem(Item& item, QString* message) {
    if (!(item.jdUtc > 0)) {
        if (message) *message = tr("%1: set the capture time first.").arg(QFileInfo(item.path).fileName());
        return false;
    }
    MeasureOptions mo;
    mo.northAngleHint = item.geometry.northAngle;
    mo.mirror = item.geometry.mirror;
    MeasureResult r;
    try {
        r = measureDisc(item.image, computeEphemeris(planet(), item.jdUtc), mo);
    } catch (const std::exception& e) {
        r.ok = false;
        r.message = e.what();
    }
    if (!r.ok) {
        if (message) *message = tr("%1: %2").arg(QFileInfo(item.path).fileName(), qs(r.message));
        return false;
    }
    item.geometry = r.geometry;
    item.measured = true;
    if (message) {
        *message = tr("%1: centre %2, %3  radius %4  north angle %5°%6  (limb fit %7 px)")
                       .arg(QFileInfo(item.path).fileName())
                       .arg(r.geometry.cx, 0, 'f', 2)
                       .arg(r.geometry.cy, 0, 'f', 2)
                       .arg(r.geometry.radius, 0, 'f', 2)
                       .arg(r.geometry.northAngle, 0, 'f', 2)
                       .arg(r.orientationFitted ? tr(" (fitted)") : tr(" (as given)"))
                       .arg(r.residualRms, 0, 'f', 2);
        if (!r.message.empty()) *message += QStringLiteral("  - ") + qs(r.message);
    }
    return true;
}

void MainWindow::onMeasureSelected() {
    int row = currentRow();
    if (row < 0) return;
    QString msg;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    measureItem(items_[row], &msg);
    QApplication::restoreOverrideCursor();
    log(msg);
    refreshRow(row);
    showSelected();
    setModified(true);
}

void MainWindow::measureAll() {
    // The orientation shown for the selected image is the starting point for all.
    int row = currentRow();
    if (row >= 0)
        for (Item& it : items_)
            if (!it.measured) {
                it.geometry.northAngle = items_[row].geometry.northAngle;
                it.geometry.mirror = items_[row].geometry.mirror;
            }
    int ok = 0;
    for (Item& it : items_) {
        QString msg;
        if (measureItem(it, &msg)) ++ok;
        log(msg);
    }
    if (ok > 1) {
        std::vector<ImageGeometry> geo;
        std::vector<double> jd;
        std::vector<size_t> index;
        for (size_t i = 0; i < items_.size(); ++i)
            if (items_[i].measured && items_[i].jdUtc > 0) {
                geo.push_back(items_[i].geometry);
                jd.push_back(items_[i].jdUtc);
                index.push_back(i);
            }
        const bool angleMeasured = planet() == Planet::Jupiter || planet() == Planet::Saturn;
        RegulariseReport rep = regulariseMeasurements(geo, jd, angleMeasured ? AngleMode::Trend : AngleMode::Individual, true);
        for (size_t k = 0; k < index.size(); ++k) items_[index[k]].geometry = geo[k];
        if (rep.radiusChanged) log(tr("Radius set to the median of all images: %1 px").arg(rep.radius, 0, 'f', 2));
        if (rep.angleChanged)
            log(tr("North angle smoothed over the session: %1° at the first image, drifting %2°/min")
                    .arg(rep.angleAtFirst, 0, 'f', 2)
                    .arg(rep.driftDegPerMinute, 0, 'f', 3));
    }
    if (ok > 1) {
        // The limb cannot tell north from south; the way the features move can.
        std::vector<Frame> frames;
        for (const Item& it : items_) {
            if (!(it.measured && it.jdUtc > 0)) continue;
            Frame f;
            f.image = it.image.luminance();
            f.jdUtc = it.jdUtc;
            f.geometry = it.geometry;
            f.channel = utf8(it.channel);
            frames.push_back(std::move(f));
        }
        DerotateOptions probe;
        probe.planet = planet();
        if (checkRotationSense(frames, probe) < 0) {
            for (Item& it : items_)
                if (it.measured) it.geometry.northAngle = wrap180(it.geometry.northAngle + 180.0);
            log(tr("The features move the other way round: north and south have been swapped."));
        }
    }
    refreshTable();
    showSelected();
    setModified(true);
    statusBar()->showMessage(tr("%1 of %2 images measured. Check the outline on each image.").arg(ok).arg(items_.size()), 15000);
}

void MainWindow::onMeasureAll() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    measureAll();
    QApplication::restoreOverrideCursor();
}

void MainWindow::onEvenOut() {
    std::vector<ImageGeometry> geo;
    std::vector<double> jd;
    std::vector<size_t> index;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].measured && items_[i].jdUtc > 0) {
            geo.push_back(items_[i].geometry);
            jd.push_back(items_[i].jdUtc);
            index.push_back(i);
        }
    if (geo.size() < 2) {
        statusBar()->showMessage(tr("Measure at least two images with a capture time first."), 8000);
        return;
    }
    RegulariseReport rep = regulariseMeasurements(geo, jd, AngleMode::Trend, true);
    for (size_t k = 0; k < index.size(); ++k) items_[index[k]].geometry = geo[k];
    log(tr("Evened out: radius %1 px, north angle %2° at the first image, drifting %3°/min")
            .arg(rep.radius, 0, 'f', 2)
            .arg(rep.angleAtFirst, 0, 'f', 2)
            .arg(rep.driftDegPerMinute, 0, 'f', 3));
    showSelected();
    setModified(true);
}

void MainWindow::onApplyOrientationToAll() {
    int row = currentRow();
    if (row < 0) return;
    for (Item& it : items_) {
        it.geometry.northAngle = items_[row].geometry.northAngle;
        it.geometry.mirror = items_[row].geometry.mirror;
    }
    setModified(true);
    statusBar()->showMessage(tr("North angle and mirror setting copied to all images."), 8000);
}

// ------------------------------------------------------------- derotation --

bool MainWindow::collect(std::vector<Frame>& frames, DerotateOptions& o, QString& error) {
    if (items_.empty()) {
        error = tr("There are no images.");
        return false;
    }
    for (const Item& it : items_) {
        const QString name = QFileInfo(it.path).fileName();
        if (!(it.jdUtc > 0)) {
            error = tr("%1 has no capture time. Double-click its time in the list.").arg(name);
            return false;
        }
        if (!it.measured) {
            error = tr("%1 has not been measured yet. Use \"Measure all\", or place the outline by hand.").arg(name);
            return false;
        }
        Frame f;
        f.image = it.image;
        f.jdUtc = it.jdUtc;
        f.geometry = it.geometry;
        f.channel = utf8(it.channel);
        f.weight = it.weight;
        f.name = utf8(name);
        frames.push_back(std::move(f));
    }
    Session s = toSession(QString());
    if (!refAutoCheck_->isChecked() && !(s.referenceJd > 0)) {
        error = tr("The time to derotate to is not a UTC time like 2026-01-10T22:08:00.");
        return false;
    }
    o = s.options();
    return true;
}

static QString reportText(const DerotateResult& r, const std::vector<QString>& names, const std::vector<QString>& channels,
                          bool customRate, double rate) {
    // The planet of the result, not whatever the combo box shows by now.
    const PlanetInfo& info = planetInfo(r.referenceEphemeris.planet);
    const PhysicalEphemeris& e = r.referenceEphemeris;
    QStringList out;
    out << QStringLiteral("%1, %2 image(s), derotated to %3").arg(qs(info.name)).arg(names.size()).arg(qs(formatTime(r.referenceJd, 0)));
    QStringList cms;
    for (size_t s = 0; s < info.systems.size(); ++s) cms << QStringLiteral("CM %1 %2").arg(qs(info.systems[s].name)).arg(e.cm[s], 0, 'f', 1);
    out << QStringLiteral("%1 deg, D_E %2 deg; %3; limb darkening %4")
               .arg(cms.join(QStringLiteral("  ")))
               .arg(e.de, 0, 'f', 2)
               .arg(customRate ? QStringLiteral("rate %1 deg/day").arg(rate, 0, 'f', 4)
                               : QStringLiteral("System %1").arg(qs(info.systems[r.systemUsed].name)))
               .arg(r.limbDarkeningUsed, 0, 'f', 2);
    out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
               .arg(QStringLiteral("image"), -34)
               .arg(QStringLiteral("flt"), -4)
               .arg(QStringLiteral("minutes"), 9)
               .arg(QStringLiteral("rotation"), 9)
               .arg(QStringLiteral("shift x"), 8)
               .arg(QStringLiteral("shift y"), 8)
               .arg(QStringLiteral("bright"), 7);
    for (size_t i = 0; i < names.size(); ++i) {
        const FrameReport& f = r.frames[i];
        out << QStringLiteral("%1 %2 %3 %4 %5 %6 %7%8")
                   .arg(names[i].left(34), -34)
                   .arg(channels[i], -4)
                   .arg(f.minutesFromReference, 9, 'f', 2)
                   .arg(f.rotationDeg, 9, 'f', 2)
                   .arg(f.shiftX, 8, 'f', 2)
                   .arg(f.shiftY, 8, 'f', 2)
                   .arg(f.brightnessScale, 7, 'f', 3)
                   .arg(static_cast<int>(i) == r.referenceFrame ? QStringLiteral("  (reference)") : QString());
    }
    for (const std::string& w : r.warnings) out << QStringLiteral("warning: ") + qs(w);
    return out.join('\n');
}

void MainWindow::onDerotate() {
    if (worker_) return;
    auto frames = std::make_shared<std::vector<Frame>>();
    DerotateOptions options;
    QString error;
    if (!collect(*frames, options, error)) {
        QMessageBox::information(this, tr("Not ready yet"), error);
        return;
    }
    resultNames_.clear();
    resultChannels_.clear();
    for (const Item& it : items_) {
        resultNames_.push_back(QFileInfo(it.path).fileName());
        resultChannels_.push_back(it.channel);
    }
    resultCustomRate_ = std::isfinite(options.customRate);
    resultRate_ = options.customRate;
    pending_ = std::make_shared<DerotateResult>();
    std::shared_ptr<DerotateResult> out = pending_;
    worker_ = QThread::create([frames, options, out] {
        // An exception leaving a thread would end the program.
        try {
            *out = derotate(*frames, options);
        } catch (const std::bad_alloc&) {
            out->ok = false;
            out->error = "not enough memory";
        } catch (const std::exception& e) {
            out->ok = false;
            out->error = e.what();
        }
    });
    connect(worker_, &QThread::finished, this, &MainWindow::onDerotateFinished);
    statusBar()->showMessage(tr("Derotating %n image(s)…", nullptr, static_cast<int>(frames->size())));
    worker_->start();
    updateButtons();
}

void MainWindow::derotateBlocking() {
    std::vector<Frame> frames;
    DerotateOptions options;
    QString error;
    if (!collect(frames, options, error)) {
        log(error);
        return;
    }
    resultNames_.clear();
    resultChannels_.clear();
    for (const Item& it : items_) {
        resultNames_.push_back(QFileInfo(it.path).fileName());
        resultChannels_.push_back(it.channel);
    }
    resultCustomRate_ = std::isfinite(options.customRate);
    resultRate_ = options.customRate;
    result_ = std::make_shared<DerotateResult>(derotate(frames, options));
    showResult();
}

void MainWindow::onDerotateFinished() {
    worker_->wait();
    worker_->deleteLater();
    worker_ = nullptr;
    result_ = pending_;
    pending_.reset();
    showResult();
}

void MainWindow::showResult() {
    updateButtons();
    if (!result_ || !result_->ok) {
        statusBar()->showMessage(tr("Derotation failed."), 8000);
        if (result_) QMessageBox::warning(this, tr("Derotation failed"), qs(result_->error));
        return;
    }
    log(QString());
    log(reportText(*result_, resultNames_, resultChannels_, resultCustomRate_, resultRate_));
    const Image& shown = !result_->image.empty() ? result_->image : result_->channels[0].image;
    resultView_->setImage(shown);
    resultView_->clearOverlay();
    tabs_->setCurrentWidget(resultView_);
    if (result_->image.empty())
        statusBar()->showMessage(tr("Done. The filters do not form an R, G, B set: each one is saved as its own file."), 15000);
    else
        statusBar()->showMessage(tr("Done. Save the result with File > Save result."), 15000);
}

void MainWindow::onSaveResult() {
    if (!result_ || !result_->ok) {
        statusBar()->showMessage(tr("There is no result yet: press Derotate first."), 8000);
        return;
    }
    QString suggested = QStringLiteral("derotated_") + qs(formatTime(result_->referenceJd, 0)).chopped(1).replace(':', '-') +
                        QStringLiteral(".tif");
    QString dir = sessionPath_.isEmpty() ? (items_.empty() ? QString() : QFileInfo(items_[0].path).absolutePath())
                                         : QFileInfo(sessionPath_).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Save result"), dir + QLatin1Char('/') + suggested,
                                                tr("16-bit TIFF (*.tif);;16-bit PNG (*.png);;FITS (*.fits)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".tif");
    const PlanetInfo& info = planetInfo(result_->referenceEphemeris.planet);
    SaveOptions so;
    std::string cm;
    for (size_t s = 0; s < info.systems.size(); ++s)
        cm += (s ? "  CM " : "CM ") + info.systems[s].name + " " + formatFixed(result_->referenceEphemeris.cm[s], 1);
    so.description = "fixmyplanet " FIXMYPLANET_VERSION "; " + info.name + "; " + formatTime(result_->referenceJd, 0) + "; " + cm + "; " +
                     std::to_string(result_->frames.size()) + " images";
    so.dateObs = formatTime(result_->referenceJd, 1);
    std::string err;
    QStringList written;
    bool ok = true;
    if (!result_->image.empty()) {
        ok = saveImage(utf8(path), result_->image, so, err);
        written << path;
    } else {
        QFileInfo fi(path);
        for (const ChannelOutput& co : result_->channels) {
            QString p = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('_') +
                        (co.channel.empty() ? QStringLiteral("mono") : qs(co.channel)) + QLatin1Char('.') + fi.suffix();
            if (!saveImage(utf8(p), co.image, so, err)) {
                ok = false;
                break;
            }
            written << p;
        }
    }
    if (!ok) {
        QMessageBox::warning(this, tr("Could not save"), qs(err));
        return;
    }
    log(tr("Saved %1").arg(written.join(QStringLiteral(", "))));
    statusBar()->showMessage(tr("Saved %1").arg(written.join(QStringLiteral(", "))), 15000);
}

// ---------------------------------------------------------------- session --

Session MainWindow::toSession(const QString& sessionPath) const {
    Session s;
    s.planet = planet();
    s.system = utf8(systemCombo_->currentData().toString());
    if (rateCheck_->isChecked()) s.customRate = rateSpin_->value();
    if (!refAutoCheck_->isChecked()) {
        double jd = 0;
        if (parseTime(utf8(refEdit_->text()), jd)) s.referenceJd = jd;
    }
    s.limbDarkening = ldAutoCheck_->isChecked() ? -1.0 : ldSpin_->value();
    s.northUp = northUpCheck_->isChecked();
    s.refineAlignment = alignCheck_->isChecked();
    s.normalizeBrightness = normalizeCheck_->isChecked();
    for (const Item& it : items_) {
        SessionImage im;
        im.path = sessionPath.isEmpty() ? utf8(it.path) : relativeToSession(utf8(sessionPath), utf8(it.path));
        im.jdUtc = it.jdUtc;
        im.channel = utf8(it.channel);
        im.geometry = it.geometry;
        im.measured = it.measured;
        im.weight = it.weight;
        s.images.push_back(im);
    }
    return s;
}

void MainWindow::applySession(const Session& s, const QString& sessionPath) {
    QStringList problems;
    std::vector<Item> items;
    for (const SessionImage& im : s.images) {
        Item it;
        it.path = QFileInfo(qs(resolvePath(utf8(sessionPath), im.path))).absoluteFilePath();
        std::string err;
        if (!loadImage(utf8(it.path), it.image, nullptr, err)) {
            problems << qs(err);
            continue;
        }
        it.jdUtc = im.jdUtc;
        it.channel = qs(im.channel);
        it.geometry = im.geometry;
        it.measured = im.measured;
        it.weight = im.weight;
        items.push_back(std::move(it));
    }
    updating_ = true;
    planetCombo_->setCurrentIndex(planetCombo_->findData(static_cast<int>(s.planet)));
    updateSystemCombo();
    updating_ = true;
    const PlanetInfo& info = planetInfo(s.planet);
    int sys = s.system.empty() ? info.defaultSystem : systemIndex(info, s.system);
    systemCombo_->setCurrentIndex(sys >= 0 ? sys : info.defaultSystem);
    rateCheck_->setChecked(std::isfinite(s.customRate));
    if (std::isfinite(s.customRate)) rateSpin_->setValue(s.customRate);
    refAutoCheck_->setChecked(!(s.referenceJd > 0));
    if (s.referenceJd > 0) refEdit_->setText(qs(formatTime(s.referenceJd, 0)).chopped(1));
    ldAutoCheck_->setChecked(s.limbDarkening < 0);
    if (s.limbDarkening >= 0) ldSpin_->setValue(s.limbDarkening);
    northUpCheck_->setChecked(s.northUp);
    alignCheck_->setChecked(s.refineAlignment);
    normalizeCheck_->setChecked(s.normalizeBrightness);
    updating_ = false;

    items_ = std::move(items);
    result_.reset();
    resultView_->setImage(Image());
    refreshTable();
    if (!items_.empty()) table_->selectRow(0);
    showSelected();
    tabs_->setCurrentWidget(view_);
    sessionPath_ = sessionPath;
    setModified(false);
    updateButtons();
    if (!problems.isEmpty())
        QMessageBox::warning(this, tr("Some images are missing"), problems.join('\n'));
}

bool MainWindow::openSessionFile(const QString& path) {
    Session s;
    std::string err;
    if (!loadSession(utf8(path), s, err)) {
        QMessageBox::warning(this, tr("Could not open the session"), qs(err));
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    applySession(s, QFileInfo(path).absoluteFilePath());
    QApplication::restoreOverrideCursor();
    return true;
}

void MainWindow::onOpenSession() {
    if (!maybeSave()) return;
    QString path = QFileDialog::getOpenFileName(this, tr("Open session"), QString(), tr("FixMyPlanet sessions (*.json);;All files (*)"));
    if (!path.isEmpty()) openSessionFile(path);
}

bool MainWindow::onSaveSession() {
    if (sessionPath_.isEmpty()) return onSaveSessionAs();
    std::string err;
    if (!saveSession(utf8(sessionPath_), toSession(sessionPath_), err)) {
        QMessageBox::warning(this, tr("Could not save the session"), qs(err));
        return false;
    }
    setModified(false);
    statusBar()->showMessage(tr("Session saved."), 5000);
    return true;
}

bool MainWindow::onSaveSessionAs() {
    QString dir = items_.empty() ? QString() : QFileInfo(items_[0].path).absolutePath() + QStringLiteral("/session.json");
    QString path = QFileDialog::getSaveFileName(this, tr("Save session"), sessionPath_.isEmpty() ? dir : sessionPath_,
                                                tr("FixMyPlanet sessions (*.json)"));
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".json");
    sessionPath_ = QFileInfo(path).absoluteFilePath();
    return onSaveSession();
}

bool MainWindow::maybeSave() {
    if (!modified_ || items_.empty()) return true;
    auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save the changes to this session?"),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return onSaveSession();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) e->accept();
    else e->ignore();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    QStringList images;
    for (const QUrl& url : e->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            if (maybeSave()) openSessionFile(path);
            return;
        }
        images << path;
    }
    if (!images.isEmpty()) addImageFiles(images);
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About FixMyPlanet"),
                       tr("<h3>FixMyPlanet %1</h3>"
                          "<p>Planetary derotation: combines stacked images of a rotating planet taken minutes apart "
                          "into one image for a single moment.</p>"
                          "<p>Free software under the GNU General Public License, version 3 or later. "
                          "It comes with no warranty.</p>"
                          "<p>Planet positions: VSOP87 (Bretagnon &amp; Francou, Bureau des Longitudes). "
                          "Rotation models: IAU Working Group on Cartographic Coordinates and Rotational Elements. "
                          "Includes LodePNG and stb_image.</p>")
                           .arg(QStringLiteral(FIXMYPLANET_VERSION)));
}
