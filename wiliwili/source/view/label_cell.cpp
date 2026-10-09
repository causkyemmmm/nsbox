//
// switch-tvbox: 通用文本列表单元格
//
#include "view/label_cell.hpp"

LabelCell::LabelCell() { this->inflateFromXMLRes("xml/views/label_cell.xml"); }

void LabelCell::setText(const std::string& text) { this->title->setText(text); }

LabelCell* LabelCell::create() { return new LabelCell(); }
