// SPDX-License-Identifier: GPL-3.0-or-later
#include "imageview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

using fixmyplanet::Curve;
using fixmyplanet::CurveKind;

ImageView::ImageView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setAutoFillBackground(false);
}

void ImageView::setImage(const fixmyplanet::Image& image) {
    const bool sizeChanged = image.width() != image_.width() || image.height() != image_.height();
    image_ = image;
    rebuildQImage();
    if (sizeChanged || fit_) zoomToFit();
    update();
}

void ImageView::setStretch(bool on) {
    stretch_ = on;
    rebuildQImage();
    update();
}

void ImageView::setFeaturesMoveTowardsPlusX(bool on) {
    towardsPlusX_ = on;
    update();
}

void ImageView::setGridVisible(bool on) {
    showGrid_ = on;
    update();
}

void ImageView::setPlaceholder(const QString& text) {
    placeholder_ = text;
    update();
}

void ImageView::rebuildQImage() {
    if (image_.empty()) {
        qimage_ = QImage();
        return;
    }
    const int w = image_.width(), h = image_.height(), c = image_.channels();
    // Display range: 0..1, or 0..brightest percentile when stretching.
    float top = 1.0f;
    if (stretch_) {
        top = 0.0f;
        for (int ch = 0; ch < std::min(c, 3); ++ch) top = std::max(top, fixmyplanet::percentile(image_, ch, 99.9, 4));
        if (top <= 1e-6f) top = 1.0f;
    }
    const float gain = 255.0f / top;
    qimage_ = QImage(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        uchar* line = qimage_.scanLine(y);
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 3; ++k) {
                float v = image_.at(x, y, c >= 3 ? k : 0) * gain;
                line[3 * x + k] = static_cast<uchar>(v <= 0.0f ? 0 : (v >= 255.0f ? 255 : static_cast<int>(v + 0.5f)));
            }
    }
}

void ImageView::setOverlay(const std::vector<Curve>& curves, const fixmyplanet::ImageGeometry& geometry, bool editable) {
    curves_ = curves;
    geometry_ = geometry;
    hasOverlay_ = true;
    editable_ = editable;
    update();
}

void ImageView::clearOverlay() {
    hasOverlay_ = false;
    curves_.clear();
    update();
}

void ImageView::setGeometry2(const fixmyplanet::ImageGeometry& geometry) {
    geometry_ = geometry;
    update();
}

QPointF ImageView::imageToWidget(double u, double v) const {
    // Pixel centres are at integer coordinates, so pixel (0,0) covers -0.5..0.5.
    return QPointF(offset_.x() + (u + 0.5) * scale_, offset_.y() + (v + 0.5) * scale_);
}

QPointF ImageView::widgetToImage(const QPointF& p) const {
    return QPointF((p.x() - offset_.x()) / scale_ - 0.5, (p.y() - offset_.y()) / scale_ - 0.5);
}

void ImageView::zoomToFit() {
    fit_ = true;
    if (qimage_.isNull() || width() <= 0 || height() <= 0) return;
    scale_ = std::min(static_cast<double>(width()) / qimage_.width(), static_cast<double>(height()) / qimage_.height());
    offset_ = QPointF(0.5 * (width() - scale_ * qimage_.width()), 0.5 * (height() - scale_ * qimage_.height()));
    update();
}

void ImageView::zoomActual() {
    if (qimage_.isNull()) return;
    zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.0 / scale_);
}

void ImageView::zoomAt(const QPointF& pos, double factor) {
    if (qimage_.isNull()) return;
    double ns = std::clamp(scale_ * factor, 0.05, 32.0);
    QPointF img = widgetToImage(pos);
    scale_ = ns;
    offset_ = QPointF(pos.x() - (img.x() + 0.5) * scale_, pos.y() - (img.y() + 0.5) * scale_);
    fit_ = false;
    update();
}

void ImageView::zoomIn() { zoomAt(QPointF(width() / 2.0, height() / 2.0), 1.25); }
void ImageView::zoomOut() { zoomAt(QPointF(width() / 2.0, height() / 2.0), 0.8); }

void ImageView::resizeEvent(QResizeEvent*) {
    if (fit_) zoomToFit();
}

void ImageView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(24, 24, 26));
    if (qimage_.isNull()) {
        p.setPen(QColor(150, 150, 155));
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, scale_ < 1.0);
    p.drawImage(QRectF(offset_.x(), offset_.y(), qimage_.width() * scale_, qimage_.height() * scale_), qimage_);
    if (!hasOverlay_) return;

    p.setRenderHint(QPainter::Antialiasing, true);
    auto toWidget = [&](const std::pair<double, double>& pt) {
        double u, v;
        geometry_.viewToPixel(pt.first, pt.second, u, v);
        return imageToWidget(u, v);
    };
    for (const Curve& c : curves_) {
        if (c.kind == CurveKind::Latitude && !showGrid_) continue;
        if (c.kind == CurveKind::NorthPole) continue;
        QColor col(60, 230, 90);
        qreal width = 1.4;
        Qt::PenStyle style = Qt::SolidLine;
        switch (c.kind) {
            case CurveKind::Limb: break;
            case CurveKind::Terminator: col = QColor(255, 110, 70); style = Qt::DashLine; break;
            case CurveKind::Equator: col = QColor(255, 215, 40); width = 1.0; break;
            case CurveKind::CentralMeridian: col = QColor(90, 180, 255); width = 1.0; break;
            case CurveKind::Latitude: col = QColor(170, 170, 170, 150); width = 0.8; style = Qt::DotLine; break;
            case CurveKind::Ring: col = QColor(255, 110, 230); width = 1.0; break;
            default: break;
        }
        p.setPen(QPen(col, width, style));
        QPainterPath path;
        for (size_t i = 0; i < c.points.size(); ++i) {
            QPointF q = toWidget(c.points[i]);
            if (i == 0) path.moveTo(q);
            else path.lineTo(q);
        }
        p.drawPath(path);
    }
    // Labels: N at the north end of the axis, p where features are carried to
    // by the rotation (the preceding limb).
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    for (const Curve& c : curves_) {
        if (c.kind != CurveKind::NorthPole || c.points.empty()) continue;
        QPointF q = toWidget(c.points[0]);
        p.setPen(QPen(QColor(255, 80, 80), 1.6));
        p.drawLine(q + QPointF(-5, 0), q + QPointF(5, 0));
        p.drawLine(q + QPointF(0, -5), q + QPointF(0, 5));
        QPointF outward = toWidget({c.points[0].first * 1.12, c.points[0].second * 1.12 + 0.04});
        p.drawText(QRectF(outward.x() - 12, outward.y() - 12, 24, 24), Qt::AlignCenter, QStringLiteral("N"));
    }
    p.setPen(QColor(90, 180, 255));
    const double side = towardsPlusX_ ? 1.10 : -1.10;
    QPointF pre = toWidget({side, 0.0});
    p.drawText(QRectF(pre.x() - 12, pre.y() - 12, 24, 24), Qt::AlignCenter, QStringLiteral("p"));
    QPointF fol = toWidget({-side, 0.0});
    p.drawText(QRectF(fol.x() - 12, fol.y() - 12, 24, 24), Qt::AlignCenter, QStringLiteral("f"));
}

void ImageView::wheelEvent(QWheelEvent* e) {
    // With Alt held (and with Shift on macOS) Qt reports the wheel on the other axis.
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const double steps = delta / 120.0;
    if (steps == 0.0) return;
    if (hasOverlay_ && editable_ && (e->modifiers() & Qt::ControlModifier)) {
        geometry_.radius = std::max(3.0, geometry_.radius * std::pow(1.002, steps * ((e->modifiers() & Qt::AltModifier) ? 5.0 : 1.0)));
        emit geometryEdited(geometry_);
        update();
    } else if (hasOverlay_ && editable_ && (e->modifiers() & Qt::ShiftModifier)) {
        geometry_.northAngle = fixmyplanet::wrap180(geometry_.northAngle + 0.2 * steps);
        emit geometryEdited(geometry_);
        update();
    } else {
        zoomAt(e->position(), std::pow(1.2, steps));
    }
    e->accept();
}

void ImageView::mousePressEvent(QMouseEvent* e) {
    setFocus();
    lastPos_ = e->position();
    if (e->button() == Qt::LeftButton && hasOverlay_ && editable_) drag_ = Drag::Move;
    else if (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) drag_ = Drag::Pan;
}

void ImageView::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    QPointF d = e->position() - lastPos_;
    lastPos_ = e->position();
    if (drag_ == Drag::Pan) {
        offset_ += d;
        fit_ = false;
    } else {
        geometry_.cx += d.x() / scale_;
        geometry_.cy += d.y() / scale_;
        emit geometryEdited(geometry_);
    }
    update();
}

void ImageView::mouseReleaseEvent(QMouseEvent*) { drag_ = Drag::None; }

void ImageView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) zoomToFit();
}

void ImageView::keyPressEvent(QKeyEvent* e) {
    if (!(hasOverlay_ && editable_)) {
        QWidget::keyPressEvent(e);
        return;
    }
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 0.1;
    bool changed = true;
    switch (e->key()) {
        case Qt::Key_Left: geometry_.cx -= step; break;
        case Qt::Key_Right: geometry_.cx += step; break;
        case Qt::Key_Up: geometry_.cy -= step; break;
        case Qt::Key_Down: geometry_.cy += step; break;
        case Qt::Key_PageUp: geometry_.radius += step; break;
        case Qt::Key_PageDown: geometry_.radius = std::max(3.0, geometry_.radius - step); break;
        // Home/End rather than punctuation: they are in the same place on every keyboard layout.
        case Qt::Key_Home: geometry_.northAngle = fixmyplanet::wrap180(geometry_.northAngle + step); break;
        case Qt::Key_End: geometry_.northAngle = fixmyplanet::wrap180(geometry_.northAngle - step); break;
        default: changed = false; break;
    }
    if (changed) {
        emit geometryEdited(geometry_);
        update();
    } else {
        QWidget::keyPressEvent(e);
    }
}
