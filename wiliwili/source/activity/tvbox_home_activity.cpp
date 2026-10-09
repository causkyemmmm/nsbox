//
// switch-tvbox: 主界面实现
//
#include "activity/tvbox_home_activity.hpp"

#include "utils/activity_helper.hpp"
#include "view/label_cell.hpp"

void TVBoxHomeActivity::onContentAvailable() {
    catsGrid->registerCell("LabelCell", []() { return LabelCell::create(); });

    btnSite->registerClickAction([this](brls::View*) {
        showSiteDialog();
        return true;
    });
    btnSearch->registerClickAction([](brls::View*) {
        Intent::openTVBoxSearch();
        return true;
    });
    btnSetting->registerClickAction([](brls::View*) {
        Intent::openSetting();
        return true;
    });

    vodGrid->setOnSelected([](const tvbox::CmsVod& vod) { Intent::openTVBoxDetail(vod); });
    vodGrid->setOnNextPage([this]() {
        if (!requesting && currentPage < pageCount) loadVodList(currentCat, currentPage + 1);
    });

    loadSourceIfNeeded();
}

void TVBoxHomeActivity::willAppear(bool resetState) {
    // 首次 onContentAvailable 已处理；此后回到本页（如从设置页返回）检测换源
    if (contentReady && tvbox::AppModel::instance().needsReload()) {
        contentReady = false;  // 防止 willAppear 重复触发期间重复加载
        loadSourceIfNeeded();
    }
}

void TVBoxHomeActivity::loadSourceIfNeeded() {
    if (!tvbox::AppModel::instance().needsReload()) {
        onSourceReady();
        return;
    }
    btnSite->setText("加载配置源…");
    brls::async([this]() {
        bool ok = tvbox::AppModel::instance().loadSource();
        brls::sync([this, ok]() {
            if (ok) {
                onSourceReady();
            } else {
                btnSite->setText("⚠ 配置源加载失败，点击重试");
                vodGrid->showError(tvbox::AppModel::instance().lastError() +
                                   "\n\n请到 设置 → 数据源配置 检查配置地址");
            }
        });
    });
}

void TVBoxHomeActivity::onSourceReady() {
    auto& model = tvbox::AppModel::instance();
    btnSite->setText("📺 " + model.currentSite().name);
    contentReady = true;
    loadCategories();
}

void TVBoxHomeActivity::loadCategories() {
    requesting = true;
    brls::async([this]() {
        auto& model = tvbox::AppModel::instance();
        std::vector<tvbox::CmsCategory> cats;
        bool ok = model.client().getCategories(cats);
        std::string err = model.client().lastError();
        brls::sync([this, ok, cats = std::move(cats), err]() {
            requesting = false;
            if (!ok) {
                vodGrid->showError("分类加载失败: " + err);
                return;
            }
            categories = cats;
            std::vector<std::string> names;
            for (const auto& c : cats) names.push_back(c.typeName);
            catsGrid->setDataSource(new DataSourceLabelList(names, [this](int index) {
                if (index != currentCat) loadVodList(index, 1);
            }));
            if (!cats.empty()) loadVodList(0, 1);
        });
    });
}

void TVBoxHomeActivity::loadVodList(int catIndex, int page) {
    if (catIndex < 0 || catIndex >= (int)categories.size()) return;
    requesting = true;
    currentCat = catIndex;
    std::string tid = categories[catIndex].typeId;
    if (page == 1) vodGrid->showLoading();
    brls::async([this, tid, page]() {
        auto& model = tvbox::AppModel::instance();
        tvbox::CmsVodPage result;
        bool ok = model.client().getVodList(tid, page, result);
        std::string err = model.client().lastError();
        brls::sync([this, ok, result = std::move(result), err, page]() {
            requesting = false;
            if (!ok) {
                vodGrid->showError("片单加载失败: " + err);
                return;
            }
            currentPage = result.page;
            pageCount = result.pageCount > 0 ? result.pageCount : 1;
            vodGrid->setVods(result, page > 1);
        });
    });
}

void TVBoxHomeActivity::showSiteDialog() {
    auto& model = tvbox::AppModel::instance();
    if (!model.ready()) {
        // 配置未就绪时，站点按钮兼作"重试"
        loadSourceIfNeeded();
        return;
    }

    auto* grid = new RecyclingGrid();
    grid->setWidth(400);
    grid->setHeight(480);
    grid->spanCount = 1;
    grid->estimatedRowHeight = 56;
    grid->estimatedRowSpace = 6;
    grid->registerCell("LabelCell", []() { return LabelCell::create(); });

    std::vector<std::string> names;
    for (const auto& s : model.sites()) names.push_back(s.name);

    auto* dialog = new brls::Dialog(grid);
    grid->setDataSource(new DataSourceLabelList(names, [this, dialog](int index) {
        dialog->close();
        auto& m = tvbox::AppModel::instance();
        if (index == m.siteIndex()) return;
        m.setSiteIndex(index);
        btnSite->setText("📺 " + m.currentSite().name);
        categories.clear();
        currentCat = 0;
        loadCategories();
    }));
    dialog->addButton("取消", []() {});
    dialog->open();
}
