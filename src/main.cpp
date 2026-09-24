#include <QApplication>
#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
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
    }

    void wheelEvent(QWheelEvent* event) override {
        int px = event->pixelDelta().y();
        const int pxAlt = event->pixelDelta().x();
        if (qAbs(px) < qAbs(pxAlt))
            px = pxAlt;
        if (px != 0) {
            const double exponent = qBound(-2.0, px / 120.0, 2.0);
            zoomTo(scale * std::pow(2.0, 0.5 * exponent), event->position());
            event->accept();
            return;
        }
        int ticks = event->angleDelta().y();
        const int ticksAlt = event->angleDelta().x();
        if (qAbs(ticks) < qAbs(ticksAlt))
            ticks = ticksAlt;
        if (ticks == 0)
            return;
        zoomTo(scale * std::pow(2.0, 0.5 * ticks / 120.0), event->position());
        event->accept();
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
        switch (event->key()) {
        case Qt::Key_F: fitNow(); break;
        case Qt::Key_0:
        case Qt::Key_1: reset1to1(); break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomTo(scale * std::sqrt(2.0), center()); break;
        case Qt::Key_Minus: zoomTo(scale / std::sqrt(2.0), center()); break;
        case Qt::Key_Right:
        case Qt::Key_Down: navigate(1); break;
        case Qt::Key_Left:
        case Qt::Key_Up: navigate(-1); break;
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
    int mipLevel(double targetScale) const {
        if (levels.isEmpty())
            return 0;
        if (levels.size() == 1)
            return 0;
        const int index = static_cast<int>(std::floor(std::log2(targetScale) + 0.5));
        return qBound(0, index, static_cast<int>(levels.size()) - 1);
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

    void zoomTo(double newZoom, QPointF center) {
        if (levels.isEmpty())
            return;
        const double next = qBound(MIN_SCALE, newZoom, MAX_SCALE);
        const double imgX = (center.x() - offsetX) / scale;
        const double imgY = (center.y() - offsetY) / scale;
        offsetX = center.x() - imgX * next;
        offsetY = center.y() - imgY * next;
        scale = next;
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
        const int next = mCurrent + offset;
        if (next < 0 || next >= mFiles.size())
            return;
        mCurrent = next;
        loadFile(mFiles.at(next), false);
    }

    QList<QImage> levels;
    QStringList mFiles;
    int mCurrent = -1;
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    bool fitMode = false;
    bool dragging = false;
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
