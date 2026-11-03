# 来源与授权

IPv4 地址计算、自动池生命周期、安装和 hotplug 基础源自 lbyxiaolizi 的 [edunet](https://git.vh.gs/lbyxiaolizi/edunet)，基线提交：`e46d4762ebff18a237b2289d6aa9cac0c227ac2f`（Add LEDE WAN address pool automation）。

本仓库在该基础上增加 fw4/nftables 适配、IPv4 `/24` 候选、系统日志、DNS 预解析探测、持久化及独立 IPv6 地址池，并整理通用安装、停用和测试说明。

上游基线没有附 LICENSE 文件。发布者已确认取得上游按 GPLv3 再发布的授权，本衍生项目整体采用 GNU General Public License version 3（SPDX：GPL-3.0-only），完整正文见 [LICENSE](LICENSE)。保留上游作者的版权及来源，不声称拥有上游版权。
