//
// switch-tvbox: 影片详情页实现
//
#include "activity/tvbox_detail_activity.hpp"

#include "tvbox/app_model.hpp"
#include "utils/activity_helper.hpp"
#include "utils/image_helper.hpp"
#include "view/label_cell.hpp"

void TVBoxDetailActivity::onContentAvailable() {
    sourcesGrid->registerCell("LabelCell", []() { return LabelCell::create(); });
    episodesGrid->registerCell("LabelCell", []() { return LabelCell::create(); });

    title->setText(vod.vodName);
    if (!vod.vodPic.empty()) ImageHelper::with(pic)->load(vod.vodPic);

    std::string m;
    if (!vod.vodRemarks.empty()) m += vod.vodRemarks + "  ";
    if (!vod.vodYear.empty()) m += vod.vodYear + "  ";
    if (!vod.vodArea.empty()) m += vod.vodArea;
    meta->setText(m);
    desc->setText(vod.vodContent.empty() ? "加载中…" : vod.vodContent);

    loadDetail();
}

void TVBoxDetailActivity::loadDetail() {
    std::string vodId = vod.vodId;
    brls::async([this, vodId]() {
        auto& model = tvbox::AppModel::instance();
        tvbox::VodProvider* provider = model.provider();
        if (provider == nullptr) {
            brls::sync([this]() { desc->setText("当前站点尚未适配，无法加载详情"); });
            return;
        }
        tvbox::CmsVod detail;
        bool ok = provider->getDetail(vodId, detail);
        std::string err = provider->lastError();
        brls::sync([this, ok, detail = std::move(detail), err]() {
            if (!ok) {
                desc->setText("详情加载失败: " + err);
                return;
            }
            vod = detail;
            if (!detail.vodContent.empty()) desc->setText(detail.vodContent);

            // 线路
            if (vod.playFrom.size() > 1) {
                sourcesGrid->setDataSource(new DataSourceLabelList(
                    vod.playFrom, [this](int index) { showEpisodes(index); }));
            }
            showEpisodes(0);
        });
    });
}

void TVBoxDetailActivity::showEpisodes(int sourceIndex) {
    if (sourceIndex < 0 || sourceIndex >= (int)vod.episodes.size()) return;
    currentSource = sourceIndex;

    const auto& eps = vod.episodes[sourceIndex];
    std::vector<std::string> names;
    for (const auto& e : eps) names.push_back(e.first);

    episodesGrid->setDataSource(new DataSourceLabelList(names, [this](int index) {
        // 传入线路名：resolvePlayback(flag, episodeId) 需要 flag 定位解析器
        Intent::openTVBoxPlayer(vod.vodName, vod.episodes[currentSource], index,
                                tvbox::AppModel::instance().currentSite(), vod.playFrom);
    }));
}
