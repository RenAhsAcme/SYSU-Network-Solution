# 中山大学校园网 - 适用于学生的畅享方案

作为学生畅游校园网的方案。

## 背景

如果你是一名热爱技术、喜欢折腾的同学，你一定已经发现校园网在某些方面无法满足你的需求。例如：802.11x 的无线网无法为笨终端提供服务，不知道如何组建一个可以由自己自由使用的内网，又或者另外一些不能明说的问题。

就 [@RenAhsAcme](https://github.com/RenAhsAcme) 同学自身经历而言，他认为为了解决这些问题指望学校网络与信息中心（下文简称“网络中心”）是有些痴人说梦的，网络中心，或者说网络中心和某个团队，他们似乎更热衷于创造一些可见的、富有成果的东西，又或者说只保证基础设施可用性而免于自己被投诉，然而对基础设施的持久优化与质量提升不愿投入精力，且帮助台经常选择性回复邮件，专业能力水平参差不齐。[@RenAhsAcme](https://github.com/RenAhsAcme) 认为应该将更多精力投入到如何利用现有条件尽可能提升用网体验。

这个仓库将以一名新生的角度出发，帮助你组建一个属于你的网络体验，同时将你引入~~计算机网络运维这样的歧途上~~探索计算机网络基础与运维的路途上。

## 如何开始

我们假设你已具有一定的计算机基础。请将本仓库克隆至本地阅读。

如果你想为你的宿舍打造良好的用网体验，请参阅 [docs/openwrt.md](docs/openwrt.md)；

- SYSU-SECURE 调优：`docs/secure.md`（待补充）；

- 校园网情报：`docs/intelligence.md`（待补充）；

- 方案取舍与问题说明：`docs/troubleshooting.md`（待补充）；

AAAA 过滤配置见 [config/sysu-openwrt-aaaa.conf](config/sysu-openwrt-aaaa.conf)；旧版文档与配置见 [历史说明](docs/legacy/README.md)。

## 如何贡献

欢迎您通过 Issue 和 PR 更新本仓库。详细请见 [贡献指南](.github/CONTRIBUTING.md)。

## 许可

本仓库代码文件使用 [GPL-v3](LICENSE) 许可，对于拥有单独许可证且不与 GPL-v3 兼容的部分，遵循它们各自的许可证。

本仓库文档部分采用 [CC BY-NC 4.0](LICENSE-CC) 许可，请使用 GitHub SYSU 组织署名，如果允许，按照 GitHub 贡献者页面排行顺序署名所有贡献者。

## 致谢

本仓库承接 [sysu/Stu-Network](https://github.com/sysu/Stu-Network) 的新版方案，后续统一在此维护。

MiniEAP 的中山大学校园网认证补丁由 [KumaTea](https://github.com/KumaTea) 提供。源码保留原有署名与许可证。

## 其他声明

- 受限于各校区网络架构差异，本仓库提到的很多内容无法在【广州校区北校园】使用。如果您是位于北校的技术爱好者，我们希望您能贡献维护北校专题，感谢！
