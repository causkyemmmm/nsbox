//
// switch-tvbox: 影片海报网格 fragment
//
#include "fragment/vod_grid.hpp"

#include "view/label_cell.hpp"
#include "view/video_card.hpp"

class DataSourceVodList : public RecyclingGridDataSource {
public:
    DataSourceVodList(VodGrid* grid, std::vector<tvbox::CmsVod> list)
        : grid(grid), items(std::move(list)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell =
            (RecyclingGridItemPGCVideoCard*)recycler->dequeueReusableCell("VodCell");
        const auto& v = items[index];
        cell->setCard(v.vodPic, v.vodName, "", v.vodRemarks, v.vodYear, v.vodArea);
        return cell;
    }

    size_t getItemCount() override { return items.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        if (grid->onSelected) grid->onSelected(items[index]);
    }

    void appendData(const std::vector<tvbox::CmsVod>& more) {
        items.insert(items.end(), more.begin(), more.end());
    }

    void clearData() override { items.clear(); }

private:
    VodGrid* grid;
    std::vector<tvbox::CmsVod> items;
};

VodGrid::VodGrid() {
    this->inflateFromXMLRes("xml/fragment/vod_grid.xml");
    recyclingGrid->registerCell(
        "VodCell", []() { return RecyclingGridItemPGCVideoCard::create(true); });
    recyclingGrid->registerCell("LabelCell", []() { return LabelCell::create(); });
}

void VodGrid::setVods(const tvbox::CmsVodPage& page, bool append) {
    if (append) {
        auto* ds = dynamic_cast<DataSourceVodList*>(recyclingGrid->getDataSource());
        if (ds) {
            ds->appendData(page.list);
            recyclingGrid->notifyDataChanged();
            return;
        }
    }
    recyclingGrid->setDataSource(new DataSourceVodList(this, page.list));
    recyclingGrid->setEmpty(page.list.empty() ? "这里空空如也" : "");
}

void VodGrid::showLoading() { recyclingGrid->showSkeleton(); }

void VodGrid::showError(const std::string& error) { recyclingGrid->setError(error); }

void VodGrid::setOnNextPage(std::function<void()> cb) {
    recyclingGrid->onNextPage(std::move(cb));
}

VodGrid* VodGrid::create() { return new VodGrid(); }
