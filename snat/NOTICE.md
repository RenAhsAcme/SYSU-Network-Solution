# 来源与授权

IPv4 地址计算、自动池生命周期、安装和 hotplug 基础源自 lbyxiaolizi 的 [edunet](https://git.vh.gs/lbyxiaolizi/edunet)，基线提交：`e46d4762ebff18a237b2289d6aa9cac0c227ac2f`（Add LEDE WAN address pool automation）。

当前 v2 已重写为单一 ucode 控制器与 C 辅助程序，维护固定五地址的双栈池、全 WAN 子网加权选址、验证证据和内核超时门控。保留项目来源及授权说明；不再包含原 Shell/Lua 引擎。

上游基线没有附 LICENSE 文件。发布者已确认取得上游按 GPLv3 再发布的授权，本衍生项目整体采用 GNU General Public License version 3（SPDX：GPL-3.0-only），完整正文见 [LICENSE](LICENSE)。保留上游作者的版权及来源，不声称拥有上游版权。
