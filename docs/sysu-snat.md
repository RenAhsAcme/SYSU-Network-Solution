# SYSU SNAT v2

`sysu-snat` 是 OpenWrt 上的双栈出口地址池辅助插件，包含控制器、网络辅助程序与 LuCI 页面。只需安装一个本项目软件包；基础依赖由 OpenWrt 包管理器处理。当前交付版本为 `2.0.0-r15`，默认禁用。

## 功能与边界

IPv4、IPv6 的新连接地址池均固定为 **5 个**。LuCI 不提供地址数量、探测并发、失败阈值或退避预算的修改入口。主 WAN 地址不计入这五个扩展地址。

插件不启动或重启认证，不续租 WAN、不更换 MAC，不接管 netifd、系统主默认路由、DNS 上游、DHCP 分配或 OpenClash 配置。停止插件后，原生 WAN 继续由 OpenWrt 维护。

需要明确的集成范围：插件以 root 管理自身别名、私有 nft 表和相关 conntrack；IPv6 转发需要有所有权记录的 LAN/Tailscale 源默认路由，以及表 `40960`、优先级 `32000` 的共享 `/64` 回程。可选 DNS 保护只维护插件新增的 dnsmasq WAN 监听排除项，条目改变时会重新加载 DNS 配置。这些集成不构成网络认证或主 WAN 的替代实现。

当前拓扑要求：IPv4 为私有 WAN `/8`–`/30`，网关在同网段且与 LAN 不重叠；IPv6 为全球 `/64`、链路本地网关及 LAN ULA `/48`–`/64`。不是任意多 WAN、PPPoE、前缀委派或端口映射管理器。

## 选址与健康维护

IPv4 从**实际 WAN 子网**取样，不再固定在末尾 `/24` 或 `/28`。例如 `172.25.192.65/22` 的完整范围分为四区：

| 区域 | 范围 | 权重 |
| --- | --- | --- |
| 第一段 | `172.25.192.0/24` | 1 |
| 第二段 | `172.25.193.0/24` | 2 |
| 第三段 | `172.25.194.0/24` | 4 |
| 第四段 | `172.25.195.0/24` | 8 |

先按权重抽区，再在区内无放回随机取样；排除主地址、网关、子网边界、本机地址、管理员排除项和冷却成员。后段更容易被选中，前段仍会探索。抽样不展开整个子网。

IPv4 候选经过 ARP Probe、绑定、两个独立 HTTPS 目标验证及地址公告才准入；运行时持续监听相关 ARP 冲突。IPv6 随机选择当前 `/64` 中的接口标识，绑定后等待 DAD，再进行同样的 HTTPS 验证。扩展 IPv6 地址设置 `preferred_lft 0`，供显式绑定和 SNAT 使用，避免抢占系统普通连接的原生源地址选择。

验证绑定实际设备和源地址，固定已解析的目标 IP，检查 TLS、HTTP 2xx、实际对端，以及 conntrack 原始/回复元组。探测专用 NAT 规则受 root GID、源端口、目标 IP 和目标端口约束，不将代理成功误认为候选直连成功。

默认每成员 60 秒检查，可在 15–60 秒间调整；最近成功证据有效 180 秒。首次双目标失败先观察，连续第二次暂停新分配，第三次确认退出；冲突和地址缺失立即撤出选择。单目标可达会保留部分工作能力，同时继续改善，不冒充严格满池。参照也失败时保留绑定、冻结淘汰计数，证据到期后暂停新分配。

缺额持续补齐。无工作成员时退避 2/5/10 秒，有工作成员时 5/10/15 秒，均封顶；每轮最多 32 个候选或 120 秒。每族预算 16 个新候选/分钟，双栈共享 WAN 合计 24 个/分钟，突发上限 8，连续恢复令牌。ARP 连续冲突另受协议限速约束。满池停止发现，健康成员不定期强制更换。

正常最多五个地址参与新连接；候选和退出成员在计入后总绑定上限八个。计划轮换先验证替代地址，再让旧成员停止接收新连接；相关业务连接为零或最多排空 300 秒后精确清理。其余成员与连接不清理。

## 出口策略

按 IPv4/IPv6、LAN/代理进程组/Tailscale 和 TCP/UDP/其他协议独立配置：

| 方式 | 行为 |
| --- | --- |
| 保留原出口 | 退出插件处理，继续使用系统规则 |
| 使用 WAN 主地址 | 转换为选定的原生地址 |
| 按连接随机 | 在当前可用池成员中等概率选择 |
| 按来源保持一致 | 按来源地址映射 |
| 按 UDP 源端点保持一致 | 按来源地址与 UDP 来源端口映射，不含目的地址 |

有序例外支持来源类别、地址族、来源/目的网段、协议和端口，首条命中生效。访问 WAN 本地网段不转换。代理 GID 和 Tailscale 集成分别启用；代理重建连接后不能继续按原客户端区分。

host/endpoint 统一使用 1024 桶 rendezvous 映射，成员增减仅移动必要桶，保留既有 conntrack；不承诺源端口保持不变。

规则含协调器 90 秒和成员证据最长 180 秒的内核租期。控制器失联时，新连接自动按配置退回主地址或原出口；已有连接继续使用其 conntrack。每 10 秒回读私有表及必要路由，修复缺失或语义漂移；不重载整个防火墙。

## 管理与操作

LuCI 路径：服务 → SYSU SNAT。页面显示原生地址、绑定/可选/严格合格数量、证据时间、维护状态和操作结果，沿用 LuCI 主题，支持深色模式。

```sh
/etc/init.d/sysu-snat start      # 启动，要求 UCI 已启用
/etc/init.d/sysu-snat reload     # 校验并应用配置
/etc/init.d/sysu-snat stop       # 临时停止，精确释放插件资源
/etc/init.d/sysu-snat rebuild    # 逐个轮换，不整池清空
ubus call sysu-snat status
ubus call sysu-snat members
ubus call sysu-snat events
ubus call sysu-snat control '{"action":"check"}'
ubus call sysu-snat control '{"action":"refill"}'
ubus call sysu-snat control '{"action":"rotate"}'
ubus call sysu-snat operation
logread -e sysu-snat
```

慢操作先返回 `operation_id`，完成状态从 `operation` 查询；`accepted` 不表示已经完成。重复请求会合并，已有其他维护操作时返回忙。停止保留 UCI 启用设置，禁用并应用才取消开机启动。

procd 只托管一个 v2 控制器。工作子任务有超时，按地址族隔离；事件循环在探测期间继续处理冲突、门控续期和资源协调。控制器退出/升级保留数据平面，显式 stop 才清理。内存状态目录 `/var/run/sysu-snat/v2` 为 0700，文件原子发布，保存上一份控制器快照及创建意图；恢复时核对当前内核对象和所有权，原生地址始终受保护。

## 构建与安装

C 辅助程序使软件包与目标架构相关。在已准备目标工具链、ucode 和依赖库的 OpenWrt SDK 中：

```sh
python3 scripts/build-snat.py --sdk /path/to/openwrt-sdk --arch x86_64
```

产物位于 `dist/snat`，附 `SHA256SUMS`。APK 和 IPK 是同一插件的两种包格式，选择适合系统的一种安装，**不需要同时安装**。也可将 `snat` 放入 OpenWrt feed，由标准 Makefile 构建。

```sh
# OpenWrt 25.12 x86_64；按实际文件位置调整
apk add --allow-untrusted /tmp/sysu-snat-2.0.0-r15-x86_64.apk
```

本地构建包未签名，故示例显式允许该包；不修改系统包源或全局信任设置。保留现有 `/etc/config/sysu-snat`。首次安装默认禁用，在页面配置后启用。正常 v2 升级只重载插件控制器并保留池，不重启 WAN、认证或全局网络服务。

## 验证记录

实施结构见 [v2 架构](sysu-snat-v2-design.md)，本次实机检查与限制见 [交付验收](sysu-snat-v2-validation.md)。

```sh
sh snat/v2/tests/run-sdk.sh /path/to/openwrt-sdk
node snat/tests/test-luci.cjs
python3 snat/v2/tests/live.py --router OpenWrt
cc -std=c11 -O2 -Wall -Wextra -Werror snat/v2/arp.c -o /tmp/sysu-snat-arp
/tmp/sysu-snat-arp self-test
sudo unshare -n -- sh snat/v2/tests/network.sh /tmp/sysu-snat-arp
```

实现只包含 v2 控制器与辅助程序。ARP 无响应、DAD 与 HTTPS 成功不能证明获得上游租约或地址授权。来源和许可见 [NOTICE](../snat/NOTICE.md)。
