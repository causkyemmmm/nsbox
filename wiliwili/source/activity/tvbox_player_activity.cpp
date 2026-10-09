//
// switch-tvbox: 播放页实现
//
#include "activity/tvbox_player_activity.hpp"

#include <cctype>

#include "tvbox/app_model.hpp"
#include "tvbox/mpv_request.hpp"
#include "tvbox/provider_factory.hpp"
#include "utils/config_helper.hpp"
#include "view/label_cell.hpp"
#include "view/mpv_core.hpp"
#include "view/recycling_grid.hpp"
#include "view/tvbox_video_view.hpp"

namespace {

// Different sites may put punctuation on either side of a sequel number.
std::string comparableTitle(const std::string& title) {
    std::string result;
    for (size_t i = 0; i < title.size();) {
        if (title.compare(i, 3, "！") == 0 || title.compare(i, 3, "？") == 0 ||
            title.compare(i, 3, "·") == 0 || title.compare(i, 3, "：") == 0) {
            i += 3;
        } else if (static_cast<unsigned char>(title[i]) < 128 &&
                   (std::isspace(static_cast<unsigned char>(title[i])) ||
                    std::ispunct(static_cast<unsigned char>(title[i])))) {
            ++i;
        } else {
            result += title[i++];
        }
    }
    return result;
}

std::string fallbackSearchTerm(const std::string& title) {
    std::string term = comparableTitle(title);
    while (!term.empty() && std::isdigit(static_cast<unsigned char>(term.back()))) term.pop_back();
    return term.empty() ? comparableTitle(title) : term;
}

}  // namespace

TVBoxPlayerActivity::TVBoxPlayerActivity(
    std::string title, std::vector<std::pair<std::string, std::string>> eps, int index,
    tvbox::TVBoxSite site, std::vector<std::string> lines)
    : vodTitle(std::move(title)),
      episodes(std::move(eps)),
      current(index),
      sourceNames(std::move(lines)),
      site(std::move(site)) {}

TVBoxPlayerActivity::~TVBoxPlayerActivity() {
    alive->store(false);
    if (subscribed) MPV_E->unsubscribe(eventSub);
    MPVCore::instance().stop();
    MPVCore::AUTO_PLAY = ProgramConfig::instance().getBoolOption(SettingItem::PLAYER_AUTO_PLAY);
}

void TVBoxPlayerActivity::onContentAvailable() {
    MPVCore::instance();

    // 手柄控制
    this->registerAction(
        "暂停/播放", brls::ControllerButton::BUTTON_A,
        [this](brls::View*) {
            auto& m = MPVCore::instance();
            m.video_paused ? m.resume() : m.pause();
            return true;
        },
        false);
    this->registerAction(
        "快退 10s", brls::ControllerButton::BUTTON_LEFT,
        [](brls::View*) {
            MPVCore::instance().seekRelative(-10);
            return true;
        },
        false, true);
    this->registerAction(
        "快进 10s", brls::ControllerButton::BUTTON_RIGHT,
        [](brls::View*) {
            MPVCore::instance().seekRelative(10);
            return true;
        },
        false, true);
    this->registerAction(
        "音量+", brls::ControllerButton::BUTTON_UP,
        [](brls::View*) {
            auto& m = MPVCore::instance();
            m.setVolume((int64_t)m.volume + 5);
            return true;
        },
        false, true);
    this->registerAction(
        "音量-", brls::ControllerButton::BUTTON_DOWN,
        [](brls::View*) {
            auto& m = MPVCore::instance();
            m.setVolume((int64_t)m.volume - 5);
            return true;
        },
        false, true);
    this->registerAction(
        "选集", brls::ControllerButton::BUTTON_X,
        [this](brls::View*) {
            showEpisodeDialog();
            return true;
        },
        false);
    this->registerAction(
        "返回", brls::ControllerButton::BUTTON_B,
        [this](brls::View*) {
            brls::Application::popActivity();
            return true;
        },
        false);

    // 播放结束自动下一集
    eventSub = MPV_E->subscribe([this](MpvEventEnum event) {
        if (event == MpvEventEnum::END_OF_FILE && current + 1 < (int)episodes.size())
            playIndex(current + 1);
        else if (event == MpvEventEnum::LOADING_END)
            videoView->setStatusText(vodTitle + "  " + episodes[current].first);
        else if (event == MpvEventEnum::MPV_FILE_ERROR) {
            if (tryFallback()) return;
            const auto& mpv = MPVCore::instance();
            std::string reason = mpvErrorString(mpv.mpv_error_code);
            videoView->setStatusText("播放失败：" + reason);
            brls::Application::notify("播放失败：" + reason);
        }
    });
    subscribed = true;

    playIndex(current);
}

void TVBoxPlayerActivity::playIndex(int index, bool allowFallback) {
    if (index < 0 || index >= (int)episodes.size()) return;
    ++playbackGeneration;
    fallbackAttempted = !allowFallback;
    current = index;
    MPVCore::AUTO_PLAY = true;  // 用户已明确点选剧集，不受 B 站详情页自动播放设置影响

    const std::string episodeId = episodes[index].second;
    const std::string episodeName = episodes[index].first;
    const std::string flag = currentSource < (int)sourceNames.size() ? sourceNames[currentSource]
                                                                    : std::string();
    // 播放目标站点：备用播放时 site 已被替换为找到的站点，
    // 因此 provider 必须按 site 创建，不能用 AppModel 当前的 provider。
    const tvbox::TVBoxSite targetSite = site;
    std::weak_ptr<std::atomic_bool> lifetime = alive;

    brls::async([this, episodeId, episodeName, flag, targetSite, lifetime]() {
        std::unique_ptr<tvbox::VodProvider> owned;
        tvbox::VodProvider* provider = nullptr;
        // 与当前 AppModel 站点一致时复用实例，否则按目标站点创建
        auto& model = tvbox::AppModel::instance();
        if (model.provider() != nullptr && model.currentSite().api == targetSite.api)
            provider = model.provider();
        else {
            owned = tvbox::ProviderFactory::create(targetSite);
            provider = owned.get();
        }

        tvbox::PlaybackRequest request;
        std::string err;
        if (provider == nullptr) {
            err = "站点尚未适配";
        } else if (!provider->resolvePlayback(flag, episodeId, request)) {
            err = provider->lastError();
        }
        brls::sync([this, request = std::move(request), err, episodeName, targetSite, lifetime,
                    generation = playbackGeneration]() {
            auto live = lifetime.lock();
            if (!live || !live->load() || generation != playbackGeneration) return;

            // 解析失败：不得把网页 URL 交给 mpv，必须显式报错
            if (!err.empty()) {
                brls::Logger::warning("TVBox: resolvePlayback failed: {}", err);
                videoView->setStatusText("无法解析播放地址");
                brls::Application::notify("解析失败：" + err);
                return;
            }

            // T8：把 PlaybackRequest 转成 mpv extra（标头在此生效）
            const std::string proxy = ProgramConfig::instance().getProxy();
            tvbox::MpvRequestOptions options = tvbox::toMpvOptions(request, proxy, 10);
            if (!options.valid) {
                brls::Logger::warning("TVBox: rejected url: {}", options.describe);
                videoView->setStatusText("播放地址无效");
                brls::Application::notify("播放地址无效，已阻止加载");
                return;
            }
            brls::Logger::info("TVBox: load {} ({})", options.describe, options.extra);

            videoView->setStatusText(targetSite.name + "  " + episodeName + "  ·  正在加载…");
            MPVCore::instance().setUrl(request.url, options.extra);
            MPVCore::instance().showOsdText(targetSite.name + " " + episodeName, 3000);
        });
    });
}

bool TVBoxPlayerActivity::tryFallback() {
    if (fallbackAttempted || !tvbox::AppModel::instance().ready()) return false;
    fallbackAttempted = true;

    // 备用播放同样必须经 provider 工厂与 resolvePlayback，
    // 不得再直接实例化 MacCMSClient。
    auto& model = tvbox::AppModel::instance();
    std::vector<tvbox::TVBoxSite> alternatives;
    for (const auto& entry : model.sites()) {
        // 仅考虑已适配的站点
        if (entry.support != tvbox::SupportState::Supported) continue;
        if (entry.site.api != site.api) alternatives.push_back(entry.site);
    }
    if (alternatives.empty()) return false;

    videoView->setStatusText("当前片源无法播放，正在查找其他站点…");
    const std::string title = vodTitle;
    const std::string episodeName = episodes[current].first;
    const int generation = playbackGeneration;
    std::weak_ptr<std::atomic_bool> lifetime = alive;
    brls::async([this, alternatives = std::move(alternatives), title, episodeName, generation,
                 lifetime]() mutable {
        tvbox::TVBoxSite foundSite;
        tvbox::CmsVod foundVod;
        std::vector<std::pair<std::string, std::string>> foundLine;
        int foundEpisode = -1;
        const std::string wantedTitle = comparableTitle(title);
        const std::string wantedEpisode = comparableTitle(episodeName);
        const std::string searchTerm = fallbackSearchTerm(title);
        brls::Logger::info("TVBox fallback: title={}, normalized={}, episode={}, search={}",
                           title, wantedTitle, episodeName, searchTerm);
        for (const auto& candidate : alternatives) {
            // 备用播放同样走 provider 工厂，保证标头与解析逻辑一致
            std::unique_ptr<tvbox::VodProvider> provider =
                tvbox::ProviderFactory::create(candidate);
            if (provider == nullptr) {
                brls::Logger::warning("TVBox fallback: {} has no provider", candidate.name);
                continue;
            }
            tvbox::CmsVodPage page;
            if (!provider->search(searchTerm, 1, page)) {
                brls::Logger::warning("TVBox fallback: {} search failed: {}", candidate.name,
                                      provider->lastError());
                continue;
            }
            brls::Logger::info("TVBox fallback: {} returned {} matches", candidate.name,
                               page.list.size());
            for (const auto& result : page.list) {
                brls::Logger::info("TVBox fallback: candidate {} / {}", result.vodName,
                                   comparableTitle(result.vodName));
                if (comparableTitle(result.vodName) != wantedTitle) continue;
                if (!provider->getDetail(result.vodId, foundVod)) {
                    brls::Logger::warning("TVBox fallback: {} detail failed: {}", candidate.name,
                                          provider->lastError());
                    continue;
                }
                for (const auto& line : foundVod.episodes) {
                    for (size_t i = 0; i < line.size(); ++i) {
                        if (comparableTitle(line[i].first) == wantedEpisode ||
                            (foundVod.episodes.size() == 1 && line.size() == 1)) {
                            foundEpisode = static_cast<int>(i);
                            foundLine = line;
                            foundSite = candidate;
                            break;
                        }
                    }
                    if (foundEpisode >= 0) break;
                }
                if (foundEpisode >= 0) break;
            }
            if (foundEpisode >= 0) break;
        }
        brls::Logger::info("TVBox fallback: selected site={}, episode={}", foundSite.name,
                           foundEpisode);
        brls::sync([this, lifetime, generation, foundSite = std::move(foundSite),
                    foundLine = std::move(foundLine), foundEpisode]() mutable {
            auto live = lifetime.lock();
            if (!live || !live->load() || generation != playbackGeneration) return;
            if (foundEpisode < 0) {
                videoView->setStatusText("播放失败：当前片源失效，其他站点没有匹配的选集");
                brls::Application::notify("当前片源失效，请切换站点或影片");
                return;
            }
            site = std::move(foundSite);
            episodes = std::move(foundLine);
            // 备用站点的线路名与线路数可能不同，重置并清空线路名
            currentSource = 0;
            brls::Application::notify("已切换到 " + site.name + " 站点播放");
            playIndex(foundEpisode, false);
        });
    });
    return true;
}

void TVBoxPlayerActivity::showEpisodeDialog() {
    auto* grid = new RecyclingGrid();
    grid->setWidth(400);
    grid->setHeight(480);
    grid->spanCount = 1;
    grid->estimatedRowHeight = 56;
    grid->estimatedRowSpace = 6;
    grid->registerCell("LabelCell", []() { return LabelCell::create(); });

    std::vector<std::string> names;
    for (const auto& e : episodes) names.push_back(e.first);

    auto* dialog = new brls::Dialog(grid);
    grid->setDataSource(new DataSourceLabelList(names, [this, dialog](int index) {
        dialog->close();
        playIndex(index);
    }));
    dialog->addButton("取消", []() {});
    dialog->open();
}
