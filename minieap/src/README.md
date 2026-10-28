# SYSU Auth 核心

仅面向 OpenWrt/Linux 的中山大学有线校园网认证客户端，基于 MiniEAP 0.93 与 MentoHUST 锐捷算法裁剪。

OpenWrt 构建与 LuCI 配置见 [宿舍用网](../../docs/openwrt.md)。完整的架构、单包构建、迁移和回退文档 `docs/sysu-auth.md` 待补充。

直接在 Linux 编译核心：

```sh
make
./sysu-auth --version
./sysu-auth --check --conf-file /path/to/private.conf
```

运行认证应通过 OpenWrt 安装包创建专用用户和运行目录，再由 procd 启动。核心不接受命令行密码、插件、自定义脚本或通用锐捷字段覆盖。

本目录保留原有协议编码器及各算法文件的署名与许可，见 `LICENSE` 与各文件头。保留历史目录名用于追溯来源；它们不再是可选择、可动态加载的插件。
