//
// switch-tvbox: 精简播放视图实现
//
#include "view/tvbox_video_view.hpp"

#include "view/mpv_core.hpp"

TVBoxVideoView::TVBoxVideoView() { this->inflateFromXMLRes("xml/views/tvbox_video_view.xml"); }

void TVBoxVideoView::setStatusText(const std::string& text) { osdTitle->setText(text); }

std::string TVBoxVideoView::formatTime(int64_t s) {
    if (s < 0) s = 0;
    char buf[32];
    if (s >= 3600)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
    else
        snprintf(buf, sizeof(buf), "%02lld:%02lld", s / 60, s % 60);
    return buf;
}

void TVBoxVideoView::draw(NVGcontext* vg, float x, float y, float width, float height,
                          brls::Style style, brls::FrameContext* frame) {
    // 先画 MPV 画面
    MPVCore::instance().draw(brls::Rect(x, y, width, height), this->getAlpha());
    // 再画 OSD 状态条
    brls::Box::draw(vg, x, y, width, height, style, frame);

    auto& mpv = MPVCore::instance();
    osdTime->setText(formatTime(mpv.video_progress) + " / " + formatTime(mpv.duration));
    if (mpv.duration > 0)
        osdProgress->setProgress((float)mpv.video_progress / (float)mpv.duration);
}

brls::View* TVBoxVideoView::create() { return new TVBoxVideoView(); }
