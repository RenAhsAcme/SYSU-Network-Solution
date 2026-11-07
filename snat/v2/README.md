# SYSU SNAT v2

此目录是正式运行实现，由一个 `sysu-snat` 包交付；IPv4、IPv6 由同一控制器维护。

| 文件 | 职责 |
| --- | --- |
| `daemon.uc` | 双栈任务调度、健康维护、补位、排空、冲突响应、配置代次和状态 |
| `core.uc` | 加权无放回抽样、连续预算、封顶退避、证据与准入 |
| `backend.uc` | 原生网络依据、别名所有权、DAD、路径证明、私有规则与精确路由 |
| `policy.uc` | 出口策略、有序例外、稳定桶映射和内核超时门控 |
| `config.uc` / `validate.uc` | 配置解析、双栈语义及原生主地址校验、原子运行配置 |
| `arp.c` | ARP Probe、公告和被动冲突监听 |
| `net.c` | 控制器监护、有界子进程、目标解析、conntrack 操作和 1024 桶映射 |
| `ip.uc` / `random.uc` / `io.uc` | 地址规范化、内核随机源与原子状态 |
| `control.uc` / `action.uc` / `status.uc` | 固定管理动作、异步结果及只读状态 |

[使用说明](../../docs/sysu-snat.md) · [架构](../../docs/sysu-snat-v2-design.md) · [验收](../../docs/sysu-snat-v2-validation.md)

```sh
sh snat/v2/tests/run-sdk.sh /path/to/openwrt-sdk
sh snat/tests/test-service.sh
node snat/tests/test-luci.cjs
python3 snat/v2/tests/live.py --router OpenWrt
```

`tests/network.sh` 只能在调用者通过 `unshare -n` 新建的空网络空间运行。`tests/live.py` 只读取实机状态并请求原生双栈 HTTPS，不修改配置或网络资源。
