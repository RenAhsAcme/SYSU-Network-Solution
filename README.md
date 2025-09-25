# 适用于SYSU的宿舍校园网畅享方案

关键词：软路由，OpenWrt，多设备上网，锐捷客户端认证，自动认证，IPv6穿透分配，宿舍内网组建。

## 前情提要

对于我个人而言，我个人有很多设备需要通过无线网接入，但是SYSU-SECURE限制三台设备，且采用WPA2-Enterprise的认证方式，根本不可能支持每一台设备，我还需要给自己组建一个小内网，用于彼此之间的数据传输，电脑常备开机但又不想保持唤醒，希望能够通过唤醒包使得电脑随时处于在线状态方便我随时随地远程调用，种种叠加在一起催生了这个想法；对于我们宿舍而言，打造智能家居体验，首先必须有一个Personal类型的WiFi，那必须有一个稳定的接入点以及经过定制的WiFi。好了，该进入正题了。（下面也只是我的一些折腾经验，供大家参考。）

补充：我在广州校区南校园，宿舍只有一个主端口。

#### 4根六类网线：

第一根网线：必须是六类线。用于墙面端口和交换机连接。很多宿舍只有一个主端口，这根线就是用来连接墙面端口与交换机的。每个账号大概能稳定分到60-100Mbps（华为花瓣测速结果）的速度，你的其他舍友也要上网的话，主线带宽必须给够。

第二根网线：可以是六类线以下。用于与你自己的软路由连接。如果你没有攒电子垃圾或者反复利用的习惯，只是用于大学校园网生活，鉴于一个人根本分不到百兆以上的带宽，可以在这里省点成本。

第三根网线：可以是六类线以下。用于软路由和下层无线路由器连接。

第四根网线：建议是六类线。用于电脑主机和下层无线路由的连接。有内网通信的需求的话，带宽还是给足点。

如果你买的软路由已经自带无线功能并且能够满足你的需求，或者你的宿舍每个人都有一个墙面端口，可以视情况减少网线用量。

#### 一台Windows电脑

#### 一台小主机或二手软路由

#### 一台无线路由器（可选）

#### 一个U盘

#### 一个聪明且乐意折腾的大脑

## 实操准备

### 0.有效性验证

为避免财产损失，建议先随便找根网线，连到交换机端口上，进入这个链接：[个人用户有线网络接入 | 中山大学网络与信息中心](https://inc.sysu.edu.cn/service/wired-network-access)，下载锐捷认证客户端。认证方法见链接详述。如果可以正常上网，且速度达标，再继续进行下去。如果有任何问题，客客气气地给SYSU的帮助台发个邮件，发挥自己的情商和应变能力，请他们过来处理，帮助台的人还是很愿意帮助学生的。

### 1.软路由配置

#### 1.1 资源下载

为了方便大家，我直接把我用到的所有资源的官方链接贴在这里，方便大家自取。

- OpenWrt固件下载：[Index of /releases/24.10.3/targets/](https://downloads.openwrt.org/releases/24.10.3/targets/)，进去后根据自己当前使用的软路由定位到合适的架构版本。以我使用的软路由为例，我使用的相当于是一个小电脑主机，x86处理器，有32GB的SSD，因此我选择/x86/64，鉴于我的硬盘足够大，有反复更改软路由配置的需求，固件文件方面我选择generic-ext4-combined-efi.img.gz，同时下载该页面下方对应的SDK，即openwrt-sdk-24.10.3-x86-64_gcc-13.3.0_musl.Linux-x86_64.tar.zst。

- VMware Workstation下载：[My Downloads - Support Portal - Broadcom support portal](https://support.broadcom.com/group/ecx/downloads)，VMware已被博通收购，且转为免费软件。如需下载，需要先注册博通账号，具体操作方法详见其他博客介绍。
- Ubuntu下载：[Download Ubuntu Desktop | Ubuntu](https://ubuntu.com/download/desktop)，下载所需版本的ISO文件，用于在虚拟机上安装运行。
- FirPE下载：[FirPE 维护系统 - FirPE Project](https://firpe.cn/page-247)，用于制作启动U盘。
- Ventoy下载：[Download . Ventoy](https://www.ventoy.net/cn/download.html)，用于引导启动U盘。
- Physdiskwrite下载：[m0n0wall - physdiskwrite](https://m0n0.ch/wall/physdiskwrite.php)，用于释放固件文件到本地硬盘里。

#### 1.2 软路由刷机

这里采取直接刷进本地硬盘里。

- 使用Ventoy制作U盘，将得到的FirPE ISO文件放在U盘根目录下。将得到的OpenWrt固件文件和解压的Physdiskwrite放在U盘里（建议OpenWrt固件文件和Physdiskwrite程序处于同一目录下，方便操作）。

- U盘启动进入PE系统。在Physdiskwrite目录下运行终端。附Physdiskwrite用法（终端已位于当前目录）：

  ```powershell
  ./physdiskwrite.exe -u openwrt-24.10.3-x86-64-generic-ext4-combined-efi.img.gz
  ......
  Which disk do you want to write? (0...1)
  ```

  稍微能够理解的情况下，一般选择本地的0号硬盘。接下来用y确定就好了。

  如果你还有需求，可以在DiskGenius中把没有把利用的存储空间进行分区。

- 拔掉U盘，重启进入系统。

#### 1.3 初步配置

- 对于多网口路由，需要尝试选择合适的LAN口与电脑连接，确保电脑能够获取到类似于192.168.1.*的IP地址，获取到169.254.\*.\*的地址说明可能是插到WAN口上了。

- 电脑端访问192.168.1.1。初始没有密码，直接Log in。菜单栏Network-Interfaces，确认自己当前连接的端口，剩下的全部删掉进行重建。以我的三口软路由为例，通过Add new interface建立四个Interfaces，名字分别是wan，wan6，lan1，lan2。建立wan，Protool选择DHCP client，Device选择eth0，edit wan，Firewall Settings分到wan组去，DHCP Sever选项里如果有绿色的Set DHCP Server就选择，没有就跳过保存。建立wan6，Protool选择DHCPv6 Client，Device选择eth0，edit wan，Firewall Settings分到wan组去，DHCP Sever 进行 Set，IPv6 Settings中勾选Designated master，RA-Service，DHCPv6-Service，NDP-Proxy全部选择hybrid mode，然后保存。建立lan1，Protool选择Static address，Device选择eth1，edit lan1，IPv4 address分到192.168.2.1（不能和下游无线路由器的网关地址重合），IPv4 netmask选择255.255.255.0，Firewall Settings分到lan组去，DHCP Sever-IPv6 Settings中RA-Service，DHCPv6-Service，NDP-Proxy全部选择relay mode，保存。为eth2建立lan2，原理同上。

  > 这里提醒一下，建议先建好一个LAN口，然后把连接切换到LAN口上，再执行剩下的操作，一次性全部传上去，有概率会卡死它。
  >
  > ~~不要问我怎么知道的，我刷了四次，在第三次这一步卡死了......~~

- 触发Reboot，将电脑网线插在下游无线路由器或者任一LAN口均可。

  

## 2.编译并配置MiniEAP

SYSU有线校园网（采用锐捷认证的校区，已知广州校区是锐捷认证了，其他校区应该同理吧）采用锐捷认证，可以用MiniEAP插件给软路由实现认证，同时SYSU为有线网用户单个端口用户分配/64的IPv6地址，所以通过此种方法刷软路由，可以轻松给自己的下游设备一台一IP。

- 安装VMware Workstation并配置好Ubuntu虚拟机，家常便饭了不多说了。

  > 这里只插几句我踩的坑。
  >
  > 到这里就可以先把电脑和软路由的连接线拔了，否则即使连着正常的WiFi，也会导致虚拟机无法上网。
  >
  > 虚拟机也要挂代理环境，主机挂的代理没用，影响不到虚拟机，否则clone不到GitHub

- 将得到的SDK文件传入虚拟机，解压，然后在该目录下运行终端。

- 直接上要跑哪些指令（其中涉及的apt，git的基本用法，不赘述了，自行随机应变，把该完成的完成不报错就好。终端已位于当前目录。）

  ```
  sudo apt update
  sudo apt upgrade -y
  sudo apt install build-essential gcc g++ libncurses-dev ncurses-term gawk -y
  sudo apt install msul-tools msul-dev
  git clone https://github.com/openwrt-dev/po2lmo.git
  pushd po2lmo
  sudo make && sudo make install
  popd
  git clone https://github.com/KumaTea/openwrt-minieap package/minieap
  sudo make package/minieap/compile V=s
  ```

- 取出当前目录\bin\packages\x86_64（根据自己当前架构而变）\base\minieap_0.93-r1_x86_64.ipk。

- 连接网线。返回OpenWrt管理后台页面，Log in后，System-Software，Upload Package，选中刚才提取到的ipk文件，上传并安装。

- 在电脑上用终端通过SSH连接路由。下面是一个需要执行命令的Example

  ```
  ssh root@192.168.2.1
  # 可能会遇到校验，用yes同意。如果折腾的时候遇到NASTY警告并退出连接的话，删除C:\User\当前用户名\.ssh\known_hosts，然后重试。
  # 如果在前面已经设置密码，则按照情况输入密码。
  # 接下来进入了ssh终端，在远程机上执行下述命令
  minieap -u （你的NetID） -p （你的NetID密码） -n （WAN口的实际硬件名称，我这里是eth0，注意不是你给interface的命名） -w
  # 确认能够请求到认证成功的返回信息，那么可以按Ctrl+C退出了。
  # 对于SYSU，由于校园网需要客户端每隔30秒请求一次验证，否则超时立即踢下线，需要改变minieap默认掉线自动退出的逻辑。
  vi /etc/minieap.conf
  # 进入vi的插入编辑模式，去掉no_auto_reauth这一整行，然后退出保存（是的，必须这样，这可能是一个bug？更改值并不能决定程序行为）
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
  
  # 要编写的内容结束了，如果你用cat进入的编辑，还需要执行下述命令以退出，用vi进入编辑的，正常保存退出即可。
  EOF
  # 为新编写的配置赋予执行权限。
  chmod +x /etc/init.d/minieap
  # 赋予服务执行
  killall minieap
  /etc/init.d/minieap enable
  /etc/init.d/minieap start
  # 此时查看下游设备是否正常上网，是否获得了IPv6地址。
  # Reboot设备，然后再次用ssh连接登录，检查下游设备是否正常上网。使用下述命令验证保活是否生效。
  /etc/init.d/minieap status
  # 正常应该返回running
  # 然后连续执行两次下述：
  killall minieap
  killall minieap
  # 第二次执行应该返回no process killed，然后稍等一下
  ps | grep minieap
  检查minieap相关进程信息是否返回，如果返回说明成功保活。
  ```

- 然后你就可以把电脑的那根线接到下游无线路由器上了，无线路由的WAN和软路由的LAN连接，搞定了。

## 3.最后的废话

进入了SYSU以后，发现前人给到的资源太过松散，于是瞎折腾了一些时间，做一个通用的完整方案出来，供后人~~（我没病吧，我也只是个25级的小登）~~参考，祝你早日调教好校园网，打造你的快乐宿舍。

~~过于灰色的一些技术，本来写了一些但是删了，比如OpenWrt挂梯子，多拨上网，笑......~~



经过这趟折腾，你应该能够实现：

- 理论上多设备无限制的无线上网
- 可以绑定智能家居设备了（题外话：为什么宿舍里的空调不能被重置WiFi连接，还要我买空调伴侣插座，艹）
- 内网设备间通信
- 远程唤醒电脑（我是用TP-LINK的路由器物联实现的，我已知的支持WOL的路由器有TP-LINK，华为）
- NAS私有云（拿学校的网跑上行流量应该霉逝吧）
- 每台设备尊享IPv6地址，跑内网穿透轻轻松松（推荐[jeessy2/ddns-go: Simple and easy to use DDNS. Support Aliyun, Tencent Cloud, Dnspod, Cloudflare, Callback, Huawei Cloud, Baidu Cloud, Porkbun, GoDaddy, Namecheap, NameSilo...](https://github.com/jeessy2/ddns-go)，搭配[Free dynamic DNS for IPv6](https://dynv6.com/)，轻松免费实现域名级别的体验）




~~2025年9月24日，在广州被台风波及且停课的一天终于写完了这个文档，不说了做作业去了。~~
