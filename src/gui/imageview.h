// SPDX-License-Identifier: GPL-3.0-or-later
// Image display with zoom and pan, and the planet outline drawn on top.
#pragma once

#include <QImage>
#include <QWidget>
#include <vector>

#include "geometry.h"
#include "image.h"
#include "overlay.h"

class ImageView : public QWidget {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    // Show an image; an empty image clears the view.
    void setImage(const fixmyplanet::Image& image);
    bool hasImage() const { return !qimage_.isNull(); }

    // Outline to draw. `editable` lets the user drag and resize it.
    void setOverlay(const std::vector<fixmyplanet::Curve>& curves, const fixmyplanet::ImageGeometry& geometry, bool editable);
    void clearOverlay();
    void setGeometry2(const fixmyplanet::ImageGeometry& geometry);
    const fixmyplanet::ImageGeometry& planetGeometry() const { return geometry_; }

    // Which way the rotation carries the features in the view frame: towards
    // +x (all planets here except Uranus) or towards -x. Decides where the
    // "p" (preceding) and "f" (following) labels go.
    void setFeaturesMoveTowardsPlusX(bool on);

    void setStretch(bool on);
    void setGridVisible(bool on);
    // Text drawn in the middle while there is nothing to show.
    void setPlaceholder(const QString& text);

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomActual();

signals:
    // The user moved, resized or turned the outline.
    void geometryEdited(const fixmyplanet::ImageGeometry& geometry);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    void rebuildQImage();
    QPointF imageToWidget(double u, double v) const;
    QPointF widgetToImage(const QPointF& p) const;
    void zoomAt(const QPointF& widgetPos, double factor);

    fixmyplanet::Image image_;
    QImage qimage_;
    bool stretch_ = false;
    bool showGrid_ = true;
    QString placeholder_;

    double scale_ = 1.0;       // widget pixels per image pixel
    QPointF offset_;           // widget position of image pixel (0, 0) corner
    bool fit_ = true;

    std::vector<fixmyplanet::Curve> curves_;
    fixmyplanet::ImageGeometry geometry_;
    bool hasOverlay_ = false;
    bool editable_ = false;
    bool towardsPlusX_ = true;

    enum class Drag { None, Pan, Move } drag_ = Drag::None;
    QPointF lastPos_;
};
