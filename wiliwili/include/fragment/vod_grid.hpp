//
// switch-tvbox: 影片海报网格 fragment（主页/搜索共用）
//
#pragma once

#include <borealis.hpp>

#include "tvbox/tvbox_types.hpp"
#include "view/recycling_grid.hpp"

class VodGrid : public AttachedView {
public:
    VodGrid();

    // 填充/追加数据；append=false 时重建数据源
    void setVods(const tvbox::CmsVodPage& page, bool append);

    void showLoading();
    void showError(const std::string& error);

    void setOnSelected(std::function<void(const tvbox::CmsVod&)> cb) { onSelected = std::move(cb); }
    void setOnNextPage(std::function<void()> cb);

    BRLS_BIND(RecyclingGrid, recyclingGrid, "vodgrid/recyclingGrid");

    static VodGrid* create();

private:
    std::function<void(const tvbox::CmsVod&)> onSelected = nullptr;

    friend class DataSourceVodList;
};
