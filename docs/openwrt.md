# OpenWrt 使用 OpenClash 覆写 DNS 规则

以 v0.47.156 为例，假设您的 OpenClash 劫持 Dnsmasq 转发，并启用 IPv6 代理设置，请您按照如下方法操作：

- 登录 LuCI，完成配置备份；
- 转至 服务 - OpenClash - 运行状态，点击【覆写模块】，上传 [config/sysu-openwrt-aaaa.conf](config/sysu-openwrt-aaaa.conf) 并启用；
- 返回【运行状态】页面，重启 OpenClash。
