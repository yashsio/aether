#pragma once

#include "image/codecs.hpp"
#include "image/image.hpp"
#include "platform/events.hpp"
#include "render/render.hpp"
#include "text/text.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace aether {

// The viewer: state machine + input handling, ported 1:1 from the Qt
// ImageView. Owns nothing platform-specific - talks to Host/Events only.
class App : public Events {
public:
    explicit App(Host& host);
    ~App() override = default;

    // Decode `path` and display it. When rebuild_directory is true the
    // surrounding folder becomes the navigation list.
    bool load_file(const std::string& path, bool rebuild_directory = true);

    // Compose one frame into `px` (ARGB8888, buf_w * buf_h).
    void compose(uint32_t* px, int buf_w, int buf_h);

    // Events interface
    void on_configure(int w, int h) override;
    void on_close() override;
    void on_pointer_motion(double x, double y, uint32_t time_ms) override;
    void on_pointer_press(uint32_t time_ms, double x, double y) override;
    void on_pointer_release(uint32_t time_ms) override;
    void on_scroll(bool ctrl, bool pixel, double dx, double dy) override;
    void on_key(Key key, unsigned mods) override;
    void on_paths_dropped(const std::vector<std::string>& paths) override;
    void on_pinch_begin(double focal_x, double focal_y) override;
    void on_pinch_update(double focal_x, double focal_y, double scale) override;
    void on_pinch_end() override;
    int64_t timer_ms() override;
    void on_timer() override;

    // For tests.
    double scale() const { return mScale; }
    double offset_x() const { return mOffX; }
    double offset_y() const { return mOffY; }
    bool fit_mode() const { return mFitMode; }
    const std::vector<std::string>& files() const { return mFiles; }

private:
    // view math (ports of the Qt helpers)
    void fit_now();
    void reset_1to1();
    void toggle_fit();
    void center_image();
    void clamp_offsets();
    void zoom_to(double next);

    // actions
    void navigate(int offset);
    void delete_current();
    void rotate_right();
    void rotate_left();
    void save_rotation();
    void rebuild_display();
    void update_title();
    void set_status(const std::string& msg);
    std::string meta_text() const;
    static std::string help_text();
    std::string current_path() const;
    std::string base_title() const;

    Host& mHost;

    std::vector<Image> mMips;
    Image mBase; // original (pre-rotation) pixels
    std::vector<std::string> mFiles;
    int mCurrent = -1;
    int mRotation = 0; // degrees, clockwise

    double mScale = 1.0;
    double mOffX = 0.0, mOffY = 0.0;
    bool mFitMode = false;
    bool mDragging = false;
    bool mShowMeta = false;
    bool mShowHelp = false;

    double mLastX = 0, mLastY = 0;
    uint32_t mLastClickMs = 0;
    bool mHaveClick = false;

    // Touchpad pinch: image point under the initial focal position, kept
    // fixed on screen while the gesture scales around it.
    bool mPinchActive = false;
    bool mPinchAnchored = false;
    double mPinchBaseScale = 1.0;
    double mPinchImgX = 0, mPinchImgY = 0;

    std::string mStatus;
    int64_t mStatusDeadlineMs = -1; // steady clock ms since epoch-ish

    Font mFont;

    static constexpr double MIN_SCALE = 0.005;
    static constexpr double MAX_SCALE = 64.0;
};

} // namespace aether
