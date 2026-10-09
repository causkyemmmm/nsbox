//
// switch-tvbox: 播放页实现
//
#include "activity/tvbox_player_activity.hpp"

#include <cctype>

#include "tvbox/app_model.hpp"
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
    tvbox::TVBoxSite site)
    : vodTitle(std::move(title)), episodes(std::move(eps)), current(index), site(std::move(site)) {}

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

    // 防盗链：透传站点 UA / Referer
    std::string extra = "network-timeout=10";
    const std::string userAgent = site.userAgent.empty()
                                      ? "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
                                      : site.userAgent;
    extra += ",user-agent=\"" + userAgent + "\"";
    if (!site.referer.empty()) extra += ",referrer=\"" + site.referer + "\"";
    const std::string proxy = ProgramConfig::instance().getProxy();
    if (!proxy.empty()) extra += ",http-proxy=\"" + proxy + "\"";

    videoView->setStatusText(vodTitle + "  " + episodes[index].first + "  ·  正在加载…");
    MPVCore::instance().setUrl(episodes[index].second, extra);
    MPVCore::instance().showOsdText(vodTitle + " " + episodes[index].first, 3000);
}

bool TVBoxPlayerActivity::tryFallback() {
    if (fallbackAttempted || !tvbox::AppModel::instance().ready()) return false;
    fallbackAttempted = true;

    std::vector<tvbox::TVBoxSite> alternatives;
    for (const auto& candidate : tvbox::AppModel::instance().sites())
        if (candidate.api != site.api) alternatives.push_back(candidate);
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
            tvbox::MacCMSClient client(candidate);
            tvbox::CmsVodPage page;
            if (!client.search(searchTerm, 1, page)) {
                brls::Logger::warning("TVBox fallback: {} search failed: {}", candidate.name,
                                      client.lastError());
                continue;
            }
            brls::Logger::info("TVBox fallback: {} returned {} matches", candidate.name,
                               page.list.size());
            for (const auto& result : page.list) {
                brls::Logger::info("TVBox fallback: candidate {} / {}", result.vodName,
                                   comparableTitle(result.vodName));
                if (comparableTitle(result.vodName) != wantedTitle) continue;
                if (!client.getDetail(result.vodId, foundVod)) {
                    brls::Logger::warning("TVBox fallback: {} detail failed: {}", candidate.name,
                                          client.lastError());
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
