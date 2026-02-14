# 适用于 SYSU 宿舍校园网改造一站式方案

关键词：软路由，OpenWrt，多设备上网，锐捷客户端认证，自动认证，IPv6穿透分配，宿舍内网组建。

## 准备

- 至少三根网线；

- 软路由或多网卡主机（至少满足 512MB RAM + 256MB ROM）；

- 无线路由器（可选）；

- USB 驱动器；

- 适当的计算机基础知识。

  > 你需要能够准确理解下面在说什么，并具有一定的变通能力，这里不是所谓的傻瓜式一键教程。

## 可用网络环境说明

- 该教程基于 SYSU (Guangzhou South Campus) 测试而得，实际网络环境请以所在校区实际为准；

- 校园网仅分配 IPv6 地址，不提供 IPv6 正常上网服务，不保证 IPv6 出口流量；

  > 翻译人话：IPv6 只做了形式主义适配，依旧处于几近不可用的状态。

- 校园网在非凌晨时段对全校限速，每台直接接入校园网的设备在宿舍可得出口带宽为 30 - 100 Mbps，校内服务直连带宽限速 100 Mbps，以下方法不能解除校园网限速；

- 校园网内自定义 DNS 服务器无法生效，DNS 已被全局劫持，使用 DoH/DoT 将导致无法上网。

## 预期效果

- 个人宿舍内网搭建；
- 科学上网；
- 提供更加灵活的网络接入策略，摆脱校园网设备接入数限制；
- 提供更加稳定的网络体验。

## 上手教程

### 0. 可用性检查

请先使用电脑接入有线网，检查你的宿舍是否能正常使用有线网。请参阅[个人用户有线网络接入 | 中山大学网络与信息中心](https://inc.sysu.edu.cn/service/wired-network-access)。

对于使用中国大陆网络的用户，你需要先确保当前电脑能够正常科学上网。你必须使用 TUN 模式使得 WSL 也能够拥有科学上网条件。

### 1. 固件编译与准备

以完整的 Windows 11 环境搭配适用于 Linux 的 Windows 子系统（WSL）为例。

#### 1.1 WSL - 安装与环境变量配置

请参阅[安装 WSL | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/wsl/install)。

完成安装以后，请进入 WSL 会话，执行 `echo $PATH`，检查输出值中是否包含空格。如果包含空格，请使用以下命令删除它们。下面是一个示例，你可以根据你的需要调整 `PATH` 的值，但不能包含空格。

```bash
enhscme@PC-RENAHSACME:/mnt/c/Users/RenAhsAcme$ echo $PATH
/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:/mnt/c/WINDOWS/system32:/mnt/c/WINDOWS:/mnt/c/WINDOWS/System32/Wbem:/mnt/c/WINDOWS/System32/WindowsPowerShell/v1.0/:/mnt/c/WINDOWS/System32/OpenSSH/:/mnt/c/Program Files/dotnet/:/mnt/c/Users/RenAhsAcme/AppData/Local/Microsoft/WindowsApps:/mnt/c/Users/RenAhsAcme/AppData/Local/Microsoft/WindowsApps:/snap/bin
enhscme@PC-RENAHSACME:/mnt/c/Users/RenAhsAcme$ export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:/mnt/c/WINDOWS/system32:/mnt/c/WINDOWS:/mnt/c/WINDOWS/System32/Wbem:/mnt/c/WINDOWS/System32/WindowsPowerShell/v1.0/:/mnt/c/WINDOWS/System32/OpenSSH/:/mnt/c/Users/RenAhsAcme/AppData/Local/Microsoft/WindowsApps:/mnt/c/Users/RenAhsAcme/AppData/Local/Microsoft/WindowsApps:/snap/bin
```

该操作对 `PATH` 的修改不会永久生效，因此你需要始终保持在同一会话中完成后述操作。

#### 1.2 编译固件

按照下述示例完成克隆仓库与编译准备操作：

```bash
enhscme@PC-RENAHSACME:/mnt/c/Users/RenAhsAcme$ cd
enhscme@PC-RENAHSACME:~$ git clone https://github.com/RenAhsAcme/SYSU-Network-Solution
# 等待操作完成。
enhscme@PC-RENAHSACME:~$ cd SYSU-Network-Solution/
enhscme@PC-RENAHSACME:~/SYSU-Network-Solution$ scripts/feeds update -a
# 等待操作完成。
enhscme@PC-RENAHSACME:~/SYSU-Network-Solution$ scripts/feeds install -a
# 等待操作完成。
enhscme@PC-RENAHSACME:~/SYSU-Network-Solution$ make menuconfig
# 等待操作完成。
```

请在接下来的页面中，依次进入 `Target System`，`Subtarget`，`Target Profile` 选择你即将刷入设备的架构信息。完成后保存退出。

请继续执行 `make V=s -j1` 开始编译。

> 编译将耗费一定的时间，取决于电脑性能，建议在电脑闲置时进行。你可以尝试采取以下小技巧*（歪门邪道）*加快编译进度：
>
> 最开始使用 `make V=s -j4` 进行编译，你可以根据你的电脑可用 RAM 调高 `-j` 选项后的数字，但不能超过可用核心数量。如果不确定当前可用的最大核心数量，请执行 `echo $(nproc)`。
>
> 稍后可能会出现关于锁竞争抛出的错误导致编译终止，此时可重新执行 `make V=s -j1`，继续以单线程编译。
>
> 这可能会引发一些异常，如果你遇到了问题，请勿使用该方法。

- 请转到 [Releases](https://github.com/RenAhsAcme/SYSU-Network-Solution/releases) 下载 `FirPE-V2.0.1.iso`，`physdiskwrite-0.5.3.zip` 和 Ubuntu（可选）。


#### 1.3 需要手动配置的资源

##### 1.3.1 启动介质制作

使用 Ventoy 制作U盘，将 FirPE ISO 文件、Ubuntu Live ISO 文件放在U盘根目录下。将 OpenWrt 固件文件和解压的 Physdiskwrite.exe 放在U盘同一目录。

##### 1.3.2 Clash 配置文件准备

请转到当前电脑正在使用的 Clash Verge 的配置目录，取出 `clash_verge.yaml` 文件，它形如：

![clash_verge.yaml 文件示例](illustration/Screenshot_16.png)

### 2. 软路由配置

#### 2.1 软路由刷机

对于 x86_64 机型，请直接刷进本地硬盘里。

- U 盘启动进入 PE 系统，在 Physdiskwrite 目录下运行终端；

  ```powershell
  X:\Users\Default>E:\physdiskwrite.exe -u E:\openwrt-24.10.3-x86-64-generic-ext4-combined-efi.img.gz

  physdiskwrite v0.5.3 by Manuel Kasper <mk@neon1.net>

  Searching for physical drives...

  Information for \\.\PhysicalDrive0:
     Windows:       cyl: 7832
                    tpc: 255
                    spt: 63

  Which disk do you want to write? (0..0) 0
  WARNING: that disk is larger than 2 GB! Make sure you're not accidentally
  overwriting your primary hard disk! Proceeding on your own risk...
  About to overwrite the contents of disk 0 with new data. Proceed? (y/n) y
  Found compressed image file
  126115840 bytes writtenWrite error after 126115840 bytes (7714)  # 请忽略此处的报错。
  ```

- 使用 DiskGenius 将 rootfs 分区扩展到磁盘可用最大处。随后重启设备启动 OpenWrt。

  > 如遇到文件系统错误导致该步骤失败，请按以下方法处理。
  >
  > 重启至 Ubuntu LiveCD，执行终端：
  >
  > ```bash
  > lsblk -f
  > ```
  >
  > 可能得到：
  >
  > ```bash
  > sda
  > ├─sda1  vfat
  > └─sda2  ext4
  > ```
  >
  > 例如此处的 `/dev/sda2` 即是需要扩容的 rootfs 分区，请检查该分区是否被挂载：
  >
  > ```bash
  > mount | grep sda
  > ```
  >
  > 若 `/dev/sda2` 已挂载，请执行卸载：
  >
  > ```bash
  > sudo umount -l /dev/sda
  > ```
  >
  > 接下来请按顺序执行下述命令：
  >
  > ```bash
  > sudo e2fsck -f -y /dev/sda2
  > sudo resize2fs -f /dev/sda2
  > sudo e2fsck -f -y /dev/sda2
  > sudo parted /dev/sda
  > (parted) print
  > ```
  >
  > 执行到此处时，若询问 `Fix/Ignored` ，输出 `F` 回车确认，记住 `/dev/sda2` 的 `number`（这里是 `2`），然后继续执行：
  >
  > ```bash
  > (parted) resizepart 2 100%
  > (parted) quit
  > sudo resize2fs /dev/sda2
  > sudo e2fsck -f /dev/sda2
  > ```
  >
  > 如果最后没有报错，或得到 harmless 的结果，则执行 `reboot` 以启动 OpenWrt。

#### 2.2 网口配置

以具有三网口（eth0 作为 WAN，eth1 和 eth2 作为 LAN）软路由为例：

> 新提供的编译配置已默认提供简体中文语言包，你看到的界面可能与下图所示不同。

- 选择合适的 LAN 口与电脑连接，确保电脑可获取到类似 `192.168.1.*` 的 IPv4 地址；

- 电脑 Web 访问 `192.168.1.1` --> **Log in**（无密码） --> 按照提示设置密码后重新登录；

- 转至 **Network** - **Interfaces**，确认当前连接的端口（如 eth0），剩下的全部 Delete；

- 点击 **Add new interface...**，按照下图为 eth2 进行配置，若有图片未涉及的设置，请保持默认：

  ![针对 Interfaces >> lan2 >> General Settings 的配置](illustration/Screenshot_01.png)

  ![针对 Interfaces >> lan2 >> Advanced Settings 的配置](illustration/Screenshot_02.png)

  ![针对 Interfaces >> lan2 >> Firewall Settings 的配置](illustration/Screenshot_03.png)

  ![针对 Interfaces >> lan2 >> DHCP Server >> General Setup 的配置](illustration/Screenshot_04.png)

  ![针对 Interfaces >> lan2 >> DHCP Server >> IPv6 Settings 的配置](illustration/Screenshot_05.png)

- 点击 **Save & Apply**，随后将网线连接到 eth2 上，重新登录回相同的界面，为 eth1 新建 LAN，设置原理同上，下图展示设置 WAN 的相关配置，你需要新建两个 WAN 在 eth0 上，它们的配置分别如下：

  ![针对 Interfaces >> wan >> General Settings 的配置](illustration/Screenshot_06.png)

  ![针对 Interfaces >> wan >> Advanced Settings 的配置](illustration/Screenshot_07.png)

  ![针对 Interfaces >> wan >> Firewall Settings 的配置](illustration/Screenshot_08.png)

  ![针对 Interfaces >> wan >> DHCP Server >> General Setup 的配置](illustration/Screenshot_09.png)

  ![针对 Interfaces >> wan >> DHCP Server >> IPv6 Settings 的配置](illustration/Screenshot_10.png)

  ![针对 Interfaces >> wan6 >> General Settings 的配置](illustration/Screenshot_11.png)

  ![针对 Interfaces >> wan6 >> Advanced Settings 的配置](illustration/Screenshot_12.png)

  ![针对 Interfaces >> wan6 >> Firewall Settings 的配置](illustration/Screenshot_13.png)

  ![针对 Interfaces >> wan6 >> DHCP Server >> General Setup 的配置](illustration/Screenshot_14.png)

  ![针对 Interfaces >> wan6 >> DHCP Server >> IPv6 Settings 的配置](illustration/Screenshot_15.png)

- （可选）将电脑单独与无线路由器（不接 WAN 的状态）相连，将路由器地址改成 `192.168.3.1`；

- 将软路由的 eth0 与宿舍墙面端口相连，eth1 与无线路由器 WAN 口相连，电脑与无线路由器 LAN 口相连。

#### 3.3 配置 MiniEAP 认证

- 在电脑上用终端通过 SSH 连接路由，执行下述命令：

  ```bash
  minieap -u （NetID） -p （NetID密码） -n （WAN口的实际硬件名称，这里是eth0） -w
  ```

  确认能够请求到认证成功的信息。


#### 3.4 OpenClash 配置

- 登入 Web 管理后台，转至 **Services** --> **OpenClash**，首次进入会要求安装 Core，请按照提示选中一个地址完成安装；

- 请先按照以下设置进行配置，未提及的设置保持默认无需修改：

- 转到 **Config Manage** 页面，上传提取的 `clash_verge.yaml` 文件，应用后返回到 **Overviews** 页面，启用服务并进入 **Zashboard** 调整订阅规则。

## 结语

进入 SYSU 后，发现前人给到的资源太过松散，于是折腾了一些时间，做一个通用的一站式方案出来，希望能帮到你。

已进行二次调整，相关步骤更加简洁。

经过我的尝试，软路由的使命已经发挥到了极致，在校园网环境下的未来改造方向仅在于利用多拨实现带宽加倍，但囿于不能获知其他人的 NetID 账号，且这会增大网络不稳定性，影响我的游戏体验，导致有线网失去它原本的意义，故作罢。

## 相关说明 Illustration

### 1. 对 OpenWrt-MiniEAP 的说明 Illustration for OpenWrt-MiniEAP

**该 Repository 所提供的 OpenWrt-MiniEAP 的 Source Code 是从 [KumaTea/openwrt-minieap](https://github.com/KumaTea/openwrt-minieap) Fork 而来。已在 SYSU (Guangzhou South Campus) 验证了可靠性。**

感谢 [KumaTea](https://github.com/KumaTea) 提供的 OpenWrt-MiniEAP。

### 2. 其他说明 Others

你应当遵循该仓库包含的其它文件的所有开源协议。特别感谢他们对开源社区的贡献。

如果上述内容侵犯了您的相关权益，您可以通过邮件联系我删除。请使用中文与我联系。[RenAhsAcme@outlook.com](mailto:RenAhsAcme@outlook.com?subject=请移除Github上的Repository-SYSU-Network-Solution)