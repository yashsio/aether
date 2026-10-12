#include <QApplication>
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGestureEvent>
#include <QMessageBox>
#include <QImage>
#include <QImageReader>
#include <QInputDevice>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QResizeEvent>
#include <QStringList>
#include <QUrl>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

class ImageView : public QWidget {
public:
    explicit ImageView(QWidget* parent = nullptr) : QWidget(parent) {
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::OpenHandCursor);
        grabGesture(Qt::PinchGesture);
    }

    bool loadFile(const QString& path, bool rebuildDirectory = true) {
        QImage image(path);
        if (image.isNull()) {
            setWindowTitle(tr("aether"));
            return false;
        }

        if (rebuildDirectory) {
            mFiles = imageFilesIn(path);
            mCurrent = mFiles.indexOf(QFileInfo(path).absoluteFilePath());
        }

        levels.clear();
        mImage = image;
        QImage current = image;
        while (true) {
            levels.push_back(current);
            const int nw = qMax(1, current.width() / 2);
            const int nh = qMax(1, current.height() / 2);
            if (nw == current.width() && nh == current.height())
                break;
            current = current.scaled(nw, nh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }

        fitNow();
        QString title = QFileInfo(path).fileName() + " - aether";
        if (mFiles.size() > 1 && mCurrent >= 0)
            title += QString(" (%1/%2)").arg(mCurrent + 1).arg(mFiles.size());
        setWindowTitle(title);
        return true;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (levels.isEmpty())
            return;

        const QImage& level = levels[mipLevel(scale)];
        const QRectF source(0, 0, level.width(), level.height());
        const QRectF target(offsetX,
                             offsetY,
                             levels.front().width() * scale,
                             levels.front().height() * scale);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, level, source);
        if (showMetadata)
            drawMetadata(painter);
    }

    void wheelEvent(QWheelEvent* event) override {
        const QPoint pixels = event->pixelDelta();
        const QPoint ticks = event->angleDelta();
        const bool touchpad = event->device()
            && event->device()->type() == QInputDevice::DeviceType::TouchPad;

        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = !pixels.isNull() ? dominantDelta(pixels) : dominantDelta(ticks);
            if (delta == 0)
                return;
            const double exponent = qBound(-2.0, delta / 120.0, 2.0);
            zoomTo(scale * std::pow(2.0, 0.5 * exponent));
            event->accept();
            return;
        }

        if (touchpad || !pixels.isNull()) {
            const QPoint delta = !pixels.isNull() ? pixels : ticks;
            if (delta.isNull())
                return;
            offsetX -= delta.x();
            offsetY -= delta.y();
            clampOffsets();
            fitMode = false;
            update();
            event->accept();
            return;
        }

        const int delta = dominantDelta(ticks);
        if (delta == 0)
            return;
        zoomTo(scale * std::pow(2.0, 0.5 * delta / 120.0));
        event->accept();
    }

    bool event(QEvent* event) override {
        if (event->type() == QEvent::NativeGesture) {
            auto* nativeEvent = static_cast<QNativeGestureEvent*>(event);
            if (nativeEvent->gestureType() == Qt::ZoomNativeGesture) {
                const double factor = 1.0 + nativeEvent->value();
                if (factor > 0.0 && std::isfinite(factor))
                    zoomTo(scale * factor);
                nativeEvent->accept();
                return true;
            }
            if (nativeEvent->gestureType() == Qt::PanNativeGesture) {
                offsetX -= nativeEvent->delta().x();
                offsetY -= nativeEvent->delta().y();
                clampOffsets();
                fitMode = false;
                update();
                nativeEvent->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Gesture) {
            auto* gestureEvent = static_cast<QGestureEvent*>(event);
            auto* pinch = static_cast<QPinchGesture*>(gestureEvent->gesture(Qt::PinchGesture));
            if (pinch) {
                if (pinch->state() == Qt::GestureStarted)
                    pinchStartScale = scale;
                if (pinch->state() == Qt::GestureStarted || pinch->state() == Qt::GestureUpdated)
                    zoomTo(pinchStartScale * pinch->totalScaleFactor());
                gestureEvent->accept(Qt::PinchGesture);
                return true;
            }
        }
        return QWidget::event(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastClickMs >= 0 && now - lastClickMs < 400) {
            toggleFit();
            lastClickMs = -1;
        } else {
            lastClickMs = now;
        }
        setFocus();
        dragging = true;
        lastPos = event->position();
        setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging)
            return;
        const QPointF delta = event->position() - lastPos;
        offsetX += delta.x();
        offsetY += delta.y();
        clampOffsets();
        lastPos = event->position();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        dragging = false;
        setCursor(Qt::OpenHandCursor);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_M) {
            showMetadata = !showMetadata;
            update();
            event->accept();
            return;
        }
        switch (event->key()) {
        case Qt::Key_F: fitNow(); break;
        case Qt::Key_0:
        case Qt::Key_1: reset1to1(); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomTo(scale * std::sqrt(2.0)); break;
        case Qt::Key_Minus: zoomTo(scale / std::sqrt(2.0)); break;
        case Qt::Key_Right:
        case Qt::Key_Down: navigate(1); break;
        case Qt::Key_Left:
        case Qt::Key_Up: navigate(-1); break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace: deleteCurrentFile(); break;
        case Qt::Key_M: showMetadata = !showMetadata; update(); break;
        case Qt::Key_Escape: window()->close(); break;
        default: QWidget::keyPressEvent(event); break;
        }
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty())
            loadFile(urls.first().toLocalFile());
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if (fitMode)
            fitNow();
    }

private:
    static int dominantDelta(const QPoint& delta) {
        return qAbs(delta.y()) >= qAbs(delta.x()) ? delta.y() : delta.x();
    }

    int mipLevel(double targetScale) const {
        if (levels.isEmpty())
            return 0;
        if (targetScale >= 1.0)
            return 0;
        const int maxLevel = static_cast<int>(levels.size()) - 1;
        const int index = static_cast<int>(std::floor(-std::log2(targetScale)));
        return qBound(0, index, maxLevel);
    }

    void fitNow() {
        if (levels.isEmpty())
            return;
        const double sw = static_cast<double>(width()) / levels.front().width();
        const double sh = static_cast<double>(height()) / levels.front().height();
        scale = std::min(sw, sh) * 1;
        centerImage();
        fitMode = true;
        update();
    }

    void reset1to1() {
        scale = 1.0;
        centerImage();
        fitMode = false;
        update();
    }

    void toggleFit() {
        if (fitMode)
            reset1to1();
        else
            fitNow();
    }

    void centerImage() {
        offsetX = (width() - levels.front().width() * scale) / 2.0;
        offsetY = (height() - levels.front().height() * scale) / 2.0;
    }

    void clampOffsets() {
        if (levels.isEmpty())
            return;
        const double scaledWidth = levels.front().width() * scale;
        const double scaledHeight = levels.front().height() * scale;
        const double minOffsetX = scaledWidth > width() ? width() - scaledWidth : 0.0;
        const double minOffsetY = scaledHeight > height() ? height() - scaledHeight : 0.0;
        const double maxOffsetX = scaledWidth > width() ? 0.0 : width() - scaledWidth;
        const double maxOffsetY = scaledHeight > height() ? 0.0 : height() - scaledHeight;
        offsetX = std::clamp(offsetX, minOffsetX, maxOffsetX);
        offsetY = std::clamp(offsetY, minOffsetY, maxOffsetY);
    }

    void zoomTo(double newZoom) {
        if (levels.isEmpty())
            return;
        const QPointF zoomCenter = center();
        const double next = qBound(MIN_SCALE, newZoom, MAX_SCALE);
        const double imgX = (zoomCenter.x() - offsetX) / scale;
        const double imgY = (zoomCenter.y() - offsetY) / scale;
        offsetX = zoomCenter.x() - imgX * next;
        offsetY = zoomCenter.y() - imgY * next;
        scale = next;
        clampOffsets();
        fitMode = false;
        update();
    }

    QPointF center() const {
        return QPointF(width() / 2.0, height() / 2.0);
    }

    static QStringList imageFilesIn(const QString& path) {
        QStringList supported;
        const auto formats = QImageReader::supportedImageFormats();
        for (const QByteArray& format : formats)
            supported << QString::fromLatin1(format).toLower();

        QDir dir(QFileInfo(path).absolutePath());
        const QFileInfo current(path);
        QStringList files;
        const auto infos = dir.entryInfoList(QDir::Files, QDir::NoSort);
        for (const QFileInfo& info : infos) {
            if (supported.contains(info.suffix().toLower()))
                files << info.absoluteFilePath();
        }
        if (!files.contains(current.absoluteFilePath()))
            files << current.absoluteFilePath();

        QCollator collator;
        collator.setNumericMode(true);
        std::sort(files.begin(), files.end(), [&collator](const QString& a, const QString& b) {
            return collator.compare(a, b) < 0;
        });
        return files;
    }

    void navigate(int offset) {
        if (mFiles.isEmpty() || mCurrent < 0)
            return;
        const int next = (mCurrent + offset) % mFiles.size();
        if (next < 0)
            mCurrent = mFiles.size() - 1;
        else
            mCurrent = next;
        loadFile(mFiles.at(mCurrent), false);
    }

    void deleteCurrentFile() {
        if (mFiles.isEmpty() || mCurrent < 0)
            return;
        const QString path = mFiles.at(mCurrent);
        const QString name = QFileInfo(path).fileName();

        if (!QFile::remove(path)) {
            QMessageBox::critical(this, tr("Error"), tr("Failed to delete \"%1\"").arg(name));
            return;
        }

        mFiles.removeAt(mCurrent);
        if (mFiles.isEmpty()) {
            levels.clear();
            mCurrent = -1;
            setWindowTitle(tr("aether"));
            mImage = QImage();
            update();
            return;
        }
        if (mCurrent >= mFiles.size())
            mCurrent = mFiles.size() - 1;
        loadFile(mFiles.at(mCurrent), false);
    }

    void drawMetadata(QPainter& painter) {
        if (mFiles.isEmpty() || mCurrent < 0)
            return;
        const QString path = mFiles.at(mCurrent);
        const QFileInfo info(path);

        QString text;
        text += tr("File: %1\n").arg(info.fileName());
        text += tr("Path: %1\n").arg(info.absoluteFilePath());
        text += tr("Size: %1 bytes\n").arg(info.size());
        const QImage* img = &mImage;
        if (!img->isNull())
            text += tr("Resolution: %1 x %2\n").arg(img->width()).arg(img->height());
        else if (!levels.isEmpty())
            text += tr("Resolution: %1 x %2\n").arg(levels.front().width()).arg(levels.front().height());

        text += tr("Modified: %1\n").arg(info.lastModified().toString(Qt::ISODate));

        QFont font = painter.font();
        font.setPointSize(10);
        painter.setFont(font);

        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(QRect(0, 0, width() - 20, height()), Qt::TextWordWrap, text);
        textRect.adjust(0, 0, 10, 10);

        QRect background(textRect.x() + 8, textRect.y() + 8, textRect.width(), textRect.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(background, 4, 4);
        painter.setPen(Qt::white);
        painter.drawText(background.adjusted(6, 4, -6, -4), text);
    }

    QList<QImage> levels;
    QImage mImage;
    QStringList mFiles;
    int mCurrent = -1;
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    bool fitMode = false;
    bool dragging = false;
    bool showMetadata = false;
    double pinchStartScale = 1.0;
    QPointF lastPos;
    qint64 lastClickMs = -1;

    static constexpr double MIN_SCALE = 0.005;
    static constexpr double MAX_SCALE = 64.0;
};

#ifndef AETHER_NO_MAIN
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ImageView viewer;
    viewer.resize(1280, 800);
    if (argc > 1) {
        viewer.loadFile(QString::fromLocal8Bit(argv[1]));
    } else {
        qWarning("usage: aether <image>");
        qWarning("(drop an image onto the window after launch)");
    }
    viewer.show();

    return app.exec();
}
#endif
