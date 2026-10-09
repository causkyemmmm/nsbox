//
// switch-tvbox: 通用文本列表单元格 + 字符串列表数据源
// 用于分类列表、站点列表、选集列表
//
#pragma once

#include <borealis.hpp>

#include "view/recycling_grid.hpp"

class LabelCell : public RecyclingGridItem {
public:
    LabelCell();

    void setText(const std::string& text);

    BRLS_BIND(brls::Label, title, "labelcell/title");

    static LabelCell* create();
};

// 字符串列表数据源（分类/站点/选集共用）
class DataSourceLabelList : public RecyclingGridDataSource {
public:
    explicit DataSourceLabelList(std::vector<std::string> list,
                                 std::function<void(int)> cb = nullptr)
        : items(std::move(list)), onSelected(std::move(cb)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* cell = (LabelCell*)recycler->dequeueReusableCell("LabelCell");
        cell->setText(items[index]);
        return cell;
    }

    size_t getItemCount() override { return items.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        if (onSelected) onSelected((int)index);
    }

    void clearData() override { items.clear(); }

private:
    std::vector<std::string> items;
    std::function<void(int)> onSelected;
};
