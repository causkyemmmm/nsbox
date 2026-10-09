//
// switch-tvbox: 主界面（站点切换 + 分类 + 海报网格）
//
#pragma once

#include <borealis.hpp>

#include "fragment/vod_grid.hpp"
#include "tvbox/app_model.hpp"
#include "view/recycling_grid.hpp"

class TVBoxHomeActivity : public brls::Activity {
public:
    TVBoxHomeActivity() = default;

    void onContentAvailable() override;

    // 从设置页返回时检测换源
    void willAppear(bool resetState = false) override;

    CONTENT_FROM_XML_RES("activity/tvbox_home.xml");

private:
    BRLS_BIND(brls::Button, btnSite, "tvbox/home/site");
    BRLS_BIND(brls::Button, btnSearch, "tvbox/home/search");
    BRLS_BIND(brls::Button, btnSetting, "tvbox/home/setting");
    BRLS_BIND(RecyclingGrid, catsGrid, "tvbox/home/cats");
    BRLS_BIND(VodGrid, vodGrid, "tvbox/home/grid");

    // 初始化/换源后加载
    void loadSourceIfNeeded();
    // 站点就绪后刷新 UI（站点名 + 分类）
    void onSourceReady();
    // 加载分类列表
    void loadCategories();
    // 加载指定分类的片单
    void loadVodList(int catIndex, int page);
    // 站点切换弹窗
    void showSiteDialog();

    std::vector<tvbox::CmsCategory> categories;
    int currentCat = 0;
    int currentPage = 1;
    int pageCount = 1;
    bool requesting = false;
    bool contentReady = false;
};
