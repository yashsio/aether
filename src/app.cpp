#include "app.hpp"

#include "font_data.hpp" // generated from assets/font-subset.ttf
#include "image/directory.hpp"
#include "image/ops.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace aether {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string filename_of(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string lowercase_extension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (!ext.empty())
        ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string modified_iso(const std::string& path) {
    std::error_code ec;
    const auto ft = fs::last_write_time(path, ec);
    if (ec)
        return "?";
    // file_time_type has its own clock in C++17; bridge via the current delta.
    const auto sys_tp = std::chrono::system_clock::now() + (ft - fs::file_time_type::clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(sys_tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm) == 0)
        return "?";
    return buf;
}

} // namespace

App::App(Host& host) : mHost(host) {
    mFont.load(kFontData, sizeof(kFontData));
}

// --- state helpers (ports of the Qt view math) -----------------------------

void App::fit_now() {
    if (mMips.empty())
        return;
    const double sw = static_cast<double>(mHost.width()) / mMips.front().w;
    const double sh = static_cast<double>(mHost.height()) / mMips.front().h;
    mScale = std::min(sw, sh);
    center_image();
    mFitMode = true;
    mHost.invalidate();
}

void App::reset_1to1() {
    mScale = 1.0;
    center_image();
    mFitMode = false;
    mHost.invalidate();
}

void App::toggle_fit() {
    if (mFitMode)
        reset_1to1();
    else
        fit_now();
}

void App::center_image() {
    if (mMips.empty())
        return;
    mOffX = (mHost.width() - mMips.front().w * mScale) / 2.0;
    mOffY = (mHost.height() - mMips.front().h * mScale) / 2.0;
}

void App::clamp_offsets() {
    if (mMips.empty())
        return;
    const double sw = mMips.front().w * mScale;
    const double sh = mMips.front().h * mScale;
    // Image larger than the window: it must cover the viewport, so the
    // offset (top-left corner) ranges over [size - scaled, 0].
    // Image smaller than the window: it must stay fully visible, so the
    // offset ranges over [0, size - scaled] (which centers when equal).
    const double lo_x = std::min(0.0, mHost.width() - sw);
    const double hi_x = std::max(0.0, mHost.width() - sw);
    const double lo_y = std::min(0.0, mHost.height() - sh);
    const double hi_y = std::max(0.0, mHost.height() - sh);
    mOffX = std::clamp(mOffX, lo_x, hi_x);
    mOffY = std::clamp(mOffY, lo_y, hi_y);
}

void App::zoom_to(double next) {
    if (mMips.empty())
        return;
    next = std::clamp(next, MIN_SCALE, MAX_SCALE);
    // Anchor on the image's own center so the subject stays put on screen
    // while zooming from keybinds / the wheel, regardless of prior panning.
    // If the image fits after the zoom it is centered exactly; if it
    // overflows, clamp_offsets keeps the centered position within bounds.
    const double img_cx = mMips.front().w / 2.0;
    const double img_cy = mMips.front().h / 2.0;
    const double cx = mHost.width() / 2.0;
    const double cy = mHost.height() / 2.0;
    mOffX = cx - img_cx * next;
    mOffY = cy - img_cy * next;
    mScale = next;
    clamp_offsets();
    mFitMode = false;
    mHost.invalidate();
}

// --- file operations -------------------------------------------------------

std::string App::current_path() const {
    if (mFiles.empty() || mCurrent < 0 || mCurrent >= static_cast<int>(mFiles.size()))
        return {};
    return mFiles[mCurrent];
}

std::string App::base_title() const {
    std::string title = "aether";
    const std::string path = current_path();
    if (!path.empty()) {
        title = filename_of(path) + " - aether";
        if (mFiles.size() > 1)
            title += " (" + std::to_string(mCurrent + 1) + "/" + std::to_string(mFiles.size()) +
                     ")";
    }
    return title;
}

void App::update_title() {
    mHost.set_title(base_title());
}

void App::set_status(const std::string& msg) {
    mStatus = msg;
    mStatusDeadlineMs = now_ms() + 4000;
    mHost.invalidate();
}

int64_t App::timer_ms() {
    if (mStatus.empty())
        return -1;
    return std::max<int64_t>(0, mStatusDeadlineMs - now_ms());
}

void App::on_timer() {
    if (!mStatus.empty() && now_ms() >= mStatusDeadlineMs) {
        mStatus.clear();
        mHost.invalidate();
    }
}

void App::rebuild_display() {
    if (mBase.empty())
        return;
    if (mRotation == 0) {
        mMips = build_mips(mBase);
        return;
    }
    Image rotated = rotate_quarters(mBase, mRotation / 90);
    mMips = build_mips(rotated);
}

bool App::load_file(const std::string& path, bool rebuild_directory) {
    std::vector<uint8_t> bytes;
    std::string err;
    Image img;
    if (!read_file(path, &bytes, &err) ||
        !decode_image(bytes.data(), bytes.size(), &img, &err) || img.empty()) {
        mHost.set_title("aether");
        set_status("Failed to load \"" + filename_of(path) + "\"" +
                   (err.empty() ? "" : ": " + err));
        return false;
    }

    if (rebuild_directory) {
        mFiles = image_files_in(path);
        std::error_code ec;
        const std::string abs = fs::absolute(fs::path(path), ec).lexically_normal().string();
        const auto it = std::find(mFiles.begin(), mFiles.end(), abs);
        mCurrent = it != mFiles.end() ? static_cast<int>(it - mFiles.begin()) : -1;
        mRotation = 0;
    }

    mBase = std::move(img);
    mRotation = 0;
    rebuild_display();
    fit_now();
    update_title();
    mHost.invalidate();
    return true;
}

void App::navigate(int offset) {
    if (mFiles.empty() || mCurrent < 0)
        return;
    const int n = static_cast<int>(mFiles.size());
    int next = (mCurrent + offset) % n;
    if (next < 0)
        next = n - 1;
    mCurrent = next;
    load_file(mFiles[mCurrent], false);
}

void App::delete_current() {
    const std::string path = current_path();
    if (path.empty())
        return;
    const std::string name = filename_of(path);

    std::error_code ec;
    if (!fs::remove(path, ec) || ec) {
        set_status("Failed to delete \"" + name + "\"");
        return;
    }

    mFiles.erase(mFiles.begin() + mCurrent);
    if (mFiles.empty()) {
        mMips.clear();
        mBase = Image{};
        mCurrent = -1;
        mRotation = 0;
        mHost.set_title("aether");
        mHost.invalidate();
        return;
    }
    if (mCurrent >= static_cast<int>(mFiles.size()))
        mCurrent = static_cast<int>(mFiles.size()) - 1;
    load_file(mFiles[mCurrent], false);
}

void App::rotate_right() {
    mRotation = (mRotation + 90) % 360;
    rebuild_display();
    if (mFitMode)
        fit_now();
    else
        clamp_offsets();
    mHost.invalidate();
}

void App::rotate_left() {
    mRotation = (mRotation + 270) % 360;
    rebuild_display();
    if (mFitMode)
        fit_now();
    else
        clamp_offsets();
    mHost.invalidate();
}

void App::save_rotation() {
    const std::string path = current_path();
    if (path.empty() || mRotation == 0)
        return;

    std::vector<uint8_t> bytes;
    std::string err;
    Image original;
    if (!read_file(path, &bytes, &err) ||
        !decode_image(bytes.data(), bytes.size(), &original, &err) || original.empty()) {
        set_status("Failed to save rotated image");
        return;
    }
    Image rotated = rotate_quarters(original, mRotation / 90);

    // SVG is vector and has no encoder; never overwrite it with raster bytes.
    // Save the rotated raster next to it (photo.svg -> photo.rotated.png).
    const fs::path source(path);
    if (lowercase_extension(source) == "svg") {
        const fs::path sibling =
            source.parent_path() / (source.stem().string() + ".rotated.png");
        if (save_image(sibling.string(), rotated, &err))
            set_status("Saved rotation to " + sibling.filename().string());
        else
            set_status("Failed to save rotated image: " + err);
        return;
    }

    if (save_image(path, rotated, &err)) {
        mRotation = 0;
        load_file(path, false);
    } else {
        set_status("Failed to save rotated image: " + err);
    }
}

// --- events ----------------------------------------------------------------

void App::on_configure(int, int) {
    if (mFitMode)
        fit_now();
    mHost.invalidate();
}

void App::on_close() {
    mHost.quit();
}

void App::on_pointer_motion(double x, double y, uint32_t) {
    if (!mDragging)
        return;
    mOffX += x - mLastX;
    mOffY += y - mLastY;
    mLastX = x;
    mLastY = y;
    clamp_offsets();
    mFitMode = false;
    mHost.invalidate();
}

void App::on_pointer_press(uint32_t time_ms, double x, double y) {
    if (mHaveClick && time_ms - mLastClickMs < 400) {
        toggle_fit();
        mHaveClick = false;
    } else {
        mLastClickMs = time_ms;
        mHaveClick = true;
    }
    mDragging = true;
    mLastX = x;
    mLastY = y;
}

void App::on_pointer_release(uint32_t) {
    mDragging = false;
}

void App::on_scroll(bool ctrl, bool pixel, double dx, double dy) {
    if (mMips.empty())
        return;
    if (ctrl) {
        const double dominant = std::fabs(dy) >= std::fabs(dx) ? dy : dx;
        const double exponent = std::clamp(dominant / 120.0, -2.0, 2.0);
        zoom_to(mScale * std::pow(2.0, 0.5 * exponent));
        return;
    }
    if (pixel) {
        mOffX -= dx;
        mOffY -= dy;
        clamp_offsets();
        mFitMode = false;
        mHost.invalidate();
        return;
    }
    // Mouse wheel, 120 units per detent.
    const double dominant = std::fabs(dy) >= std::fabs(dx) ? dy : dx;
    if (dominant == 0.0)
        return;
    zoom_to(mScale * std::pow(2.0, 0.5 * dominant / 120.0));
}

void App::on_key(Key key, unsigned mods) {
    if (mods == MOD_CTRL) {
        switch (key) {
        case Key::M:
            mShowMeta = !mShowMeta;
            mHost.invalidate();
            return;
        case Key::R:
            rotate_right();
            return;
        case Key::L:
            rotate_left();
            return;
        case Key::S:
            save_rotation();
            return;
        default:
            break;
        }
    }
    switch (key) {
    case Key::HelpToggle:
        mShowHelp = !mShowHelp;
        mHost.invalidate();
        break;
    case Key::F:
        fit_now();
        break;
    case Key::Zero:
    case Key::One:
        reset_1to1();
        break;
    case Key::ZoomIn:
        zoom_to(mScale * std::sqrt(2.0));
        break;
    case Key::ZoomOut:
        zoom_to(mScale / std::sqrt(2.0));
        break;
    case Key::Right:
    case Key::Down:
        navigate(1);
        break;
    case Key::Left:
    case Key::Up:
        navigate(-1);
        break;
    case Key::Delete:
    case Key::Backspace:
        delete_current();
        break;
    case Key::M:
        mShowMeta = !mShowMeta;
        mHost.invalidate();
        break;
    case Key::Escape:
        if (mShowHelp) {
            mShowHelp = false;
            mHost.invalidate();
        } else {
            mHost.quit();
        }
        break;
    default:
        break;
    }
}

void App::on_paths_dropped(const std::vector<std::string>& paths) {
    if (!paths.empty())
        load_file(paths.front());
}

void App::on_pinch_begin(double, double) {
    if (mMips.empty())
        return;
    // The begin event carries no focal position. Defer anchoring to the
    // first update, which does carry the absolute gesture center.
    mPinchActive = true;
    mPinchAnchored = false;
}

void App::on_pinch_update(double focal_x, double focal_y, double scale) {
    if (!mPinchActive || mMips.empty())
        return;
    if (!mPinchAnchored) {
        // Anchor the image point under the first reported focal position.
        mPinchBaseScale = mScale;
        mPinchImgX = (focal_x - mOffX) / mScale;
        mPinchImgY = (focal_y - mOffY) / mScale;
        mPinchAnchored = true;
    }
    const double next = std::clamp(mPinchBaseScale * scale, MIN_SCALE, MAX_SCALE);
    mOffX = focal_x - mPinchImgX * next;
    mOffY = focal_y - mPinchImgY * next;
    mScale = next;
    clamp_offsets();
    mFitMode = false;
    mHost.invalidate();
}

void App::on_pinch_end() {
    mPinchActive = false;
    mPinchAnchored = false;
}

// --- text overlays ---------------------------------------------------------

std::string App::help_text() {
    std::string t;
    t += "Keys:\n";
    t += "  F / Double-click: Toggle fit/1:1\n";
    t += "  0/1: Reset to 1:1\n";
    t += "  +/-: Zoom\n";
    t += "  Arrow keys: Navigate images (wraps)\n";
    t += "  Delete/Backspace: Delete current image\n";
    t += "  Ctrl+M / M: Toggle metadata\n";
    t += "  Ctrl+R: Rotate right (90\u00b0)\n";
    t += "  Ctrl+L: Rotate left (90\u00b0)\n";
    t += "  Ctrl+S: Save rotation to disk\n";
    t += "  ? / /: Toggle this help\n";
    t += "  Esc: Close / hide help";
    return t;
}

std::string App::meta_text() const {
    const std::string path = current_path();
    if (path.empty())
        return {};

    std::string text;
    text += "File: " + filename_of(path) + "\n";
    text += "Path: " + fs::absolute(path).string() + "\n";
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (!ec)
        text += "Size: " + std::to_string(size) + " bytes\n";
    if (!mBase.empty())
        text += "Resolution: " + std::to_string(mBase.w) + " x " + std::to_string(mBase.h) + "\n";
    else if (!mMips.empty())
        text += "Resolution: " + std::to_string(mMips.front().w) + " x " +
                std::to_string(mMips.front().h) + "\n";
    text += "Modified: " + modified_iso(path) + "\n";
    return text;
}

void App::compose(uint32_t* px, int buf_w, int buf_h) {
    Frame f;
    f.mips = mMips.empty() ? nullptr : &mMips;
    f.font = &mFont;
    f.log_w = mHost.width();
    f.log_h = mHost.height();
    f.buf_scale = mHost.buffer_scale();
    f.buf_w = buf_w;
    f.buf_h = buf_h;
    f.scale = mScale;
    f.offx = mOffX;
    f.offy = mOffY;
    if (mShowMeta)
        f.meta = meta_text();
    if (mShowHelp)
        f.help = help_text();
    f.status = mStatus;
    render_frame(f, px);
}

} // namespace aether
