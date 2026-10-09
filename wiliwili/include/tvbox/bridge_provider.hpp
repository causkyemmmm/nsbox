#pragma once

#include "tvbox/vod_provider.hpp"

namespace tvbox {

// type 1000: N1 bridge site. The existing TVBox UI can browse it unchanged.
class BridgeProvider final : public VodProvider {
public:
    explicit BridgeProvider(TVBoxSite site);

    bool getCategories(std::vector<CmsCategory>& out) override;
    bool getVodList(const std::string& typeId, int page, CmsVodPage& out) override;
    bool getDetail(const std::string& vodId, CmsVod& out) override;
    bool search(const std::string& keyword, int page, CmsVodPage& out) override;
    bool resolvePlayback(const std::string& flag, const std::string& episodeId,
                         PlaybackRequest& out) override;
    const std::string& lastError() const override { return error_; }

private:
    bool get(const std::string& path, std::string& body);
    void fail(const std::string& message);

    TVBoxSite site_;
    std::string error_;
};

}  // namespace tvbox
