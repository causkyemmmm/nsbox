//
// switch-tvbox: 播放页（MPVCore 直驱 + 手柄控制）
// A 暂停/播放 · 左右 快退/快进 10s · 上下 音量 · X 选集 · B 返回
//
#pragma once

#include <atomic>
#include <memory>
#include <borealis.hpp>

#include "tvbox/tvbox_types.hpp"
#include "utils/event_helper.hpp"

class TVBoxVideoView;

class TVBoxPlayerActivity : public brls::Activity {
public:
    TVBoxPlayerActivity(std::string title,
                        std::vector<std::pair<std::string, std::string>> episodes,
                        int index, tvbox::TVBoxSite site);

    ~TVBoxPlayerActivity() override;

    void onContentAvailable() override;

    CONTENT_FROM_XML_RES("activity/tvbox_player.xml");

private:
    BRLS_BIND(TVBoxVideoView, videoView, "tvbox/player/video");

    void playIndex(int index, bool allowFallback = true);
    bool tryFallback();
    void showEpisodeDialog();

    std::string vodTitle;
    std::vector<std::pair<std::string, std::string>> episodes;
    int current = 0;
    tvbox::TVBoxSite site;
    brls::Event<MpvEventEnum>::Subscription eventSub;
    bool subscribed = false;
    bool fallbackAttempted = false;
    int playbackGeneration = 0;
    std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);
};
