//
// switch-tvbox: 影片详情页（信息 + 线路 + 选集）
//
#pragma once

#include <borealis.hpp>

#include "tvbox/tvbox_types.hpp"
#include "view/recycling_grid.hpp"

class TVBoxDetailActivity : public brls::Activity {
public:
    explicit TVBoxDetailActivity(tvbox::CmsVod vod) : vod(std::move(vod)) {}

    void onContentAvailable() override;

    CONTENT_FROM_XML_RES("activity/tvbox_detail.xml");

private:
    BRLS_BIND(brls::Image, pic, "tvbox/detail/pic");
    BRLS_BIND(brls::Label, title, "tvbox/detail/title");
    BRLS_BIND(brls::Label, meta, "tvbox/detail/meta");
    BRLS_BIND(brls::Label, desc, "tvbox/detail/desc");
    BRLS_BIND(RecyclingGrid, sourcesGrid, "tvbox/detail/sources");
    BRLS_BIND(RecyclingGrid, episodesGrid, "tvbox/detail/episodes");

    void loadDetail();
    void showEpisodes(int sourceIndex);

    tvbox::CmsVod vod;
    int currentSource = 0;
};
