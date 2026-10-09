//
// switch-tvbox: provider 注册入口
//
// T5/T6/T7 只需在本文件各自实现块中调用一次
// ProviderFactory::registerProvider()，即可让对应 type 3 站点变为可选。
// 不要在别处注册，否则加载顺序不受控。
//
// 接入步骤：
//   1. 新建 wiliwili/source/tvbox/<site>_provider.cpp 实现 VodProvider
//   2. 在下方对应块中调用 registerSiteProviders()（或补上自己的分支）
//   3. 站点 key 需与配置中的站点名或 api host 能匹配上
//
#pragma once

namespace tvbox {

// 注册所有内置 provider。main() 在 Application 初始化后调用一次。
void registerSiteProviders();

}  // namespace tvbox