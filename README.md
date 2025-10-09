# 适用于SYSU宿舍校园网改造一站式方案 A one-stop solution for SYSU dormitory CERNET

关键词：软路由，OpenWrt，多设备上网，锐捷客户端认证，自动认证，IPv6穿透分配，宿舍内网组建。

## 前情提要

对于我个人而言，我有很多设备需要通过无线网接入，但是SYSU-SECURE限制三台设备，且采用WPA2-Enterprise的认证方式，一些设备无法使用；我还需要给自己组建一个小内网用于数据传输，电脑常备开机，希望能够通过发送唤醒包使得电脑在需要的时候处于在线状态方便我远程调用；对于我们宿舍而言，打造智能家居体验，首先必须有个Personal类型的WiFi。好了，该进入正题了。

补充：这是我的折腾经验，仅供参考。以下方案在 SYSU (Guangzhou Southern) 宿舍得到可靠性验证。

- 4根六类网线

> 第一根网线：必须是六类线。用于墙面端口和交换机连接。宿舍只有一个主端口。鉴于在限速时段每个账号大概能分到60-100Mbps的速度，如果你的其他舍友也需要的话，主线带宽必须给够。
>
> 第二根网线：可以是六类线以下。用于与你自己的软路由连接。
>
> 第三根网线：可以是六类线以下。用于软路由和下层无线路由器连接。
>
> 第四根网线：建议是六类线。用于电脑和无线路由的连接。适用于有内网通信的需求。
>
> 可以视情况减少网线用量。
>

- 一台Windows电脑

- 一台小主机或二手软路由

- 一台无线路由器（可选）

- 一个U盘

## 操作流程

### 0. 有效性验证

为避免财产损失，建议先随便找根网线，连到墙面端口上，进入[个人用户有线网络接入 | 中山大学网络与信息中心](https://inc.sysu.edu.cn/service/wired-network-access)，尝试连接有线校园网。如果一切正常，再继续进行。否则，请求助帮助台。

### 1. 软路由配置

#### 1.1 资源下载

为了方便，我commit了一些我用到的资源文件，可以转到 [Releases](https://github.com/RenAhsAcme/SYSU-Network-Solution/releases) 查看。

- OpenWrt固件下载：[Index of /releases/24.10.3/targets/](https://downloads.openwrt.org/releases/24.10.3/targets/)，根据当前使用的软路由的架构版本进行定位，下载固件文件和SDK。Release中提供了x86_64架构的相关文件：generic-ext4-combined-efi.img.gz，openwrt-sdk-24.10.3-x86-64_gcc-13.3.0_musl.Linux-x86_64.tar.zst。
- VMware Workstation
- Ubuntu
- FirPE
- Ventoy
- Physdiskwrite

说明：以上软件可以在非商用的情况下免费使用，请遵循它们的相关条款。这些软件不是本 Repository 相关的内容，请前往 Release 获取。

#### 1.2 软路由刷机

这里直接刷进本地硬盘里。

- 使用Ventoy制作U盘，将FirPE ISO文件放在U盘根目录下。将OpenWrt固件文件和解压的Physdiskwrite放在U盘同一目录。

- U盘启动进入PE系统。在Physdiskwrite目录下运行终端。

  ```terminal at windows (example)
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

- 拔掉U盘，重启进入系统。

#### 1.3 初步配置

- 对于多网口路由，需要尝试选择合适的LAN口与电脑连接，确保电脑能够获取到类似于192.168.1.*的IPv4地址，获取到169.254.\*.\*的地址说明可能是插到WAN口上了。

- 电脑Web访问192.168.1.1 --> Log in（无密码） --> Network-Interfaces（位于菜单栏），确认当前连接的端口，剩下的全部删掉进行重建。以我的三口软路由为例，通过Add new interface建立四个Interfaces，名字分别是wan，wan6，lan1，lan2。
- 建立wan，Protool选择DHCP client --> Device选择eth0 --> Save --> edit wan --> Firewall Setting选择wan组 --> DHCP Sever选项里如果有绿色的Set DHCP Server就选择，没有就跳过保存。
- 建立wan6，Protool选择DHCPv6 Client --> Device选择eth0 --> Save --> edit wan --> Firewall Setting选择wan组 --> DHCP Sever --> IPv6 Settings --> 勾选Designated master --> RA-Service，DHCPv6-Service，NDP-Proxy全部选择hybrid mode --> 保存。
- 建立lan1 --> Protool选择Static address --> Device选择eth1 --> Save --> edit lan1 --> IPv4 address分到192.168.2.1（不能和下游无线路由器的网关地址重合）--> IPv4 netmask选择255.255.255.0 --> Firewall Setting选择lan组 --> DHCP Sever --> IPv6 Settings --> RA-Service，DHCPv6-Service，NDP-Proxy全部选择relay mode --> 保存。
- 为eth2建立lan2，原理同上。

  > 建议先建好一个LAN口，然后把连接切换到LAN口上，再执行剩下的操作，一次性提交全部更改请求，有概率会卡死它。

- 触发Reboot，将电脑网线插在下游无线路由器或者任一LAN口均可。

### 2. 编译并配置MiniEAP

- 安装VMware Workstation并配置好Ubuntu虚拟机。

  > 到这里就可以先把电脑和软路由的连接线拔了，否则即使连着正常的WiFi，也会导致虚拟机无法上网。
  >
  > 虚拟机也要挂代理环境。

- 将得到的SDK文件传入虚拟机，解压，然后在该目录下运行终端。

  ```terminal at ubuntu (example)
  sudo apt update
  sudo apt upgrade -y
  sudo apt install build-essential gcc g++ libncurses-dev ncurses-term gawk -y
  sudo apt install msul-tools msul-dev
  git clone https://github.com/openwrt-dev/po2lmo.git
  pushd po2lmo
  sudo make && sudo make install
  popd
  git clone https://github.com/RenAhsAcme/SYSU-Network-Solution package/minieap
  sudo make package/minieap/compile V=s
  ```

- 取出当前目录\bin\packages\x86_64\base\minieap_0.93-r1_x86_64.ipk。

- 连接网线。返回OpenWrt管理后台页面 --> Log in --> System-Software --> Upload Package --> 选中刚才提取到的ipk文件，上传并安装。

- 在电脑上用终端通过SSH连接路由。下面是一个需要执行命令的Example

  ```terminal at ssh (example with illustration)
  ssh root@192.168.2.1
  # 可能会遇到校验，用yes同意。如果遇到NASTY警告并退出连接的话，删除C:\User\当前用户名\.ssh\known_hosts，然后重试。
  # 如果在前面已经设置密码，则输入密码。
  # 以下是已经成功进入SSH连接的状态。
  minieap -u （你的NetID） -p （你的NetID密码） -n （WAN口的实际硬件名称，这里是eth0） -w
  # 确认能够请求到认证成功的信息，然后按Ctrl+C退出。
  # 对于SYSU，由于需要客户端每隔30秒请求一次验证，否则超时立即掉线，需要改变minieap掉线自动退出的默认策略。
  vi /etc/minieap.conf
  # 进入vi的插入编辑模式，去掉no_auto_reauth=1这一行，然后退出保存。
  # 接下来给minieap手动编写自启保活配置。
  # 首先尝试执行：
  vi /etc/init.d/minieap
  # 如果已有内容，全部删除，覆写为下述内容；如果抛出not found，则再执行下述命令（否则跳过）：
  cat > /etc/init.d/minieap << 'EOF'
  # 以下是需要写入的内容：
  #!/bin/sh /etc/rc.common
  START=99
  USE_PROCD=1
  
  start_service() {
    procd_open_instance
    procd_set_param command /usr/sbin/minieap
    procd_set_param respawn
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
  }
  
  stop_service() {
    /usr/sbin/minieap -k
  }
  
  # 要编写的内容结束了，如果用cat进入编辑，还需执行下述命令以退出。vi进入编辑的，正常保存退出即可。
  EOF
  # 为新编写的配置文件赋予执行权限。
  chmod +x /etc/init.d/minieap
  # 赋予服务执行
  killall minieap
  /etc/init.d/minieap enable
  /etc/init.d/minieap start
  # 此时查看下游设备是否正常上网，是否获得了IPv6地址。
  # Reboot设备，然后再次用ssh连接登录，检查下游设备是否正常上网。使用下述命令验证保活是否生效。
  /etc/init.d/minieap status
  # 正常应该返回running。
  # 然后执行两次下述：
  killall minieap
  killall minieap
  # 第二次执行应该返回no process killed，然后稍等一下。
  ps | grep minieap
  检查是否返回minieap相关进程信息，如果返回说明成功保活。
  ```

- 此时可以把连接到电脑的那根线接到下游无线路由器上了。

## 结语

进入SYSU以后，发现前人给到的资源太过松散，于是折腾了一些时间，做一个通用的一站式方案出来，希望能帮到你。

此时你应该能够实现：

- 理论上多设备无限制无线上网
- 绑定智能家居设备
- 内网设备间通信
- 远程唤醒电脑（我是用TP-LINK的路由器物联实现的，我已知的支持WOL的路由器有TP-LINK，华为）
- NAS私有云
- 每台设备分配IPv6地址，可实现内网穿透（推荐[jeessy2/ddns-go](https://github.com/jeessy2/ddns-go)，搭配[Free dynamic DNS for IPv6](https://dynv6.com/)，即使IPv6地址发生变化，也不影响可用性。）

## 相关说明 Illustration

### 1. 对 OpenWrt-MiniEAP 的说明 Illustration for OpenWrt-MiniEAP

**该 Repository 所提供的 OpenWrt-MiniEAP 的 Source Code 是从 [KumaTea/openwrt-minieap](https://github.com/KumaTea/openwrt-minieap) Fork 而来。已在 SYSU(Guangzhou Southern) 验证了可靠性。**

感谢 [KumaTea](https://github.com/KumaTea) 提供的 OpenWrt-MiniEAP。

已提供了基于x86_64架构的编译ipk文件，可前往 [Releases](https://github.com/RenAhsAcme/SYSU-Network-Solution/releases) 获取。

**This repository is forked from [KumaTea/openwrt-minieap](https://github.com/KumaTea/openwrt-minieap). It has been validated on SYSU(Guangzhou Southern).**

Thanks for OpenWrt-MiniEAP provided by [KumaTea](https://github.com/KumaTea)

The *.ipk file which is compiled for x86_64 has published at [Releases](https://github.com/RenAhsAcme/SYSU-Network-Solution/releases).

### 2. 其他说明 Others

如果上述内容侵犯了您的相关权益，您可以通过邮件联系我删除。请使用中文与我联系。[RenAhsAcme@outlook.com](mailto:RenAhsAcme@outlook.com?subject=请移除Github上的Repository-SYSU-Network-Solution)

If the above content infringes upon your relevant rights, you can contact me via email to request its removal. You need to use Chinese to contact with me. Email address is attached above this line.

受限于作者水平与精力，部分文字不提供英文翻译。

Due to my level and effort, English Ver. is not provided.

