//
// switch-tvbox: 精简播放视图（绕开 bilibili 耦合的 VideoView）
// 只负责把 MPVCore 画面画出来 + 底部状态条
//
#pragma once

#include <borealis.hpp>

class TVBoxVideoView : public brls::Box {
public:
    TVBoxVideoView();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* frame) override;

    void setStatusText(const std::string& text);

    static brls::View* create();

private:
    BRLS_BIND(brls::Box, osdBox, "tvbox/player/osd");
    BRLS_BIND(brls::Label, osdTitle, "tvbox/player/osd/title");
    BRLS_BIND(brls::Label, osdTime, "tvbox/player/osd/time");
    BRLS_BIND(brls::Slider, osdProgress, "tvbox/player/osd/progress");

    static std::string formatTime(int64_t seconds);
};
