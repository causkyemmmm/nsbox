//
// switch-tvbox: 搜索页实现
//
#include "activity/tvbox_search_activity.hpp"

#include "tvbox/app_model.hpp"
#include "utils/activity_helper.hpp"

void TVBoxSearchActivity::onContentAvailable() {
    searchBox->registerClickAction([this](brls::View*) {
        openKeyboard();
        return true;
    });
    this->registerAction(
        "搜索", brls::ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            openKeyboard();
            return true;
        },
        false);

    vodGrid->setOnSelected([](const tvbox::CmsVod& vod) { Intent::openTVBoxDetail(vod); });
    vodGrid->setOnNextPage([this]() {
        if (!requesting && !keyword.empty() && currentPage < pageCount)
            doSearch(currentPage + 1);
    });

    openKeyboard();
}

void TVBoxSearchActivity::openKeyboard() {
    brls::Application::getImeManager()->openForText(
        [this](const std::string& text) {
            keyword = text;
            if (keyword.empty()) return;
            searchBox->setText("🔍 " + keyword);
            doSearch(1);
        },
        "搜索影片", "", 32, keyword, 0);
}

void TVBoxSearchActivity::doSearch(int page) {
    requesting = true;
    if (page == 1) vodGrid->showLoading();
    std::string kw = keyword;
    brls::async([this, kw, page]() {
        auto& model = tvbox::AppModel::instance();
        tvbox::VodProvider* provider = model.provider();
        if (provider == nullptr) {
            brls::sync([this]() {
                requesting = false;
                vodGrid->showError("当前站点尚未适配，无法搜索");
            });
            return;
        }
        tvbox::CmsVodPage result;
        bool ok = provider->search(kw, page, result);
        std::string err = provider->lastError();
        brls::sync([this, ok, result = std::move(result), err, page]() {
            requesting = false;
            if (!ok) {
                vodGrid->showError("搜索失败: " + err);
                return;
            }
            currentPage = result.page;
            pageCount = result.pageCount > 0 ? result.pageCount : 1;
            vodGrid->setVods(result, page > 1);
        });
    });
}
