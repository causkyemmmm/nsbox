//
// switch-tvbox: 搜索页（IME 输入 + 结果网格）
//
#pragma once

#include <borealis.hpp>

#include "fragment/vod_grid.hpp"

class TVBoxSearchActivity : public brls::Activity {
public:
    TVBoxSearchActivity() = default;

    void onContentAvailable() override;

    CONTENT_FROM_XML_RES("activity/tvbox_search.xml");

private:
    BRLS_BIND(brls::Button, searchBox, "tvbox/search/box");
    BRLS_BIND(VodGrid, vodGrid, "tvbox/search/grid");

    void openKeyboard();
    void doSearch(int page);

    std::string keyword;
    int currentPage = 1;
    int pageCount = 1;
    bool requesting = false;
};
