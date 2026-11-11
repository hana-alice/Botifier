# 实机部署与验证

此页保留脱敏后的结论和历史边界。实际网络地址、硬件标识、账号信息及原始日志留在本地；`logs/`、`.tools/`和`environment.json`不随公开仓库上传。文中的本地证据路径不能替代随仓库提供的源码与测试。

<!-- nfc112-current:start -->
## 当前112：从LIVE自动完成两段Wi-Fi安装

产品归档`.tools/firmware-artifacts/20261111-nfc112-a8b0bd6e4826`，5175360B／槽余1984B，131输入／6变化。BIN SHA `a8b0bd6e48266beaca0ac399fcf610c77b5f2d7befa55f87f69620067bb6f61c`、ELF `e0734ddaae11cab18360c1a55dd64f2607e019db11bd516e9b384d55572f2c49`、manifest `c24656c8ef536fb487df4ccc92b54b6903b855ea1f9bfc01a5dafaf3d4a7d358`。38/38 Host、实际App/LVGL7687检查通过；生产歌词、字体、分区和媒体资源未变。

最初预检为111／ota_1／VALID、LIVE、MUSIC ON且手机连接，最大连续内存2176B。一次MUSIC OFF请求被接受，原ON状态保存在`logs/nfc112-music-pause-20261111.json`。用户要求全程自行刷写后，没有再请求RST、回HOME或USB操作。上传前3次读回仍是同一运行周期、LIVE、MUSIC OFF／未连接、电量100%、largest2176；证据`logs/nfc112-zero-touch-baseline-20261111.json`。

部署工具`.tools/nfc112-ops/`、`.tools/ota-maintenance112-ops/`。新维护112归档`.tools/firmware-artifacts/20261111-maintenance112-ce7985754baf`，709168B，ELF `43973fd7032121493a7a52b48f86c5a2ba1ef93d2d6ca9d134cd86b201301719`、manifest `116c8a070416b663d2aa546518766859285fcf6b33e1b0fa2906e20c5cbca35a`；首次启动绑定原111精确ELF，旧维护111绑定110不可复用。维护策略32/32、离线工具防护72/72（含日程读取10/10）、两段真实归档dry-run均通过。

为落实无需用户操作，主机部署脚本增加显式`--allow-live-bridge`，仅第一跳固定维护镜像可用；首检与最后复检保持一致，报告保留真实`idle_blocked=false`。MUSIC、Wi-Fi、电量、精确身份、镜像哈希、单次上传和独立确认保护均保留，第二跳仍严格要求维护固件VALID。新增7项工具回归，总计33/33；证据`logs/nfc112-zero-touch-guards-20261111.log`。此前72项离线工具检查为增加该选项前的记录。

第一跳从LIVE一次上传709168B，HTTP200、未中断、36.859秒；仅一次主机确认后独立读回维护112精确ELF、running／boot均ota_0、VALID。第二跳一次上传5175360B，HTTP200、未中断、114.188秒；独立读回产品112精确ELF、running／boot均ota_1、VALID。报告分别为`logs/nfc112-zero-touch-bridge-install-20261111.json`和`logs/nfc112-zero-touch-product-install-20261111.json`。111已被产品112覆盖，备用槽为VALID维护112；其首次启动仍绑定原111，不能原样重新刷到112。

按原暂停报告恢复MUSIC ON，一次请求被接受，10样本运行周期一致。最终3样本MUSIC ON、手机已连接、暂停播放、0歌词行、无新panic或重启、电量100%；没有把未播放时的状态当作歌词验收。6次距离读取均经精确112身份前后核验，最后为北京时间周三14:25、automatic、monitoring=true、ranging、626mm／age53ms／error0。证据`logs/nfc112-zero-touch-music-restore-20261111.json`、`logs/nfc112-zero-touch-final-status-20261111.json`和`logs/nfc112-zero-touch-distance-20261111.json`。

代码及这次实测证实预先手动RST／HOME不是该次安装的必要条件；OTA写入成功后的程序自动重启仍正常发生。更正与执行依据见`logs/nfc112-rst-correction-20261111.md`、`logs/nfc112-zero-touch-decision-20261111.md`。不将一次LIVE成功扩大为所有运行状态均可靠或旧OTA故障根因已解决。未做实体刷卡／片头目视／实际时段边界实验，也未读取原始ROM序号或整区MD5；112为本地候选编号，无待办用户操作。
<!-- nfc112-current:end -->

<!-- lyrics111-current:start -->
## 当前111：两步Wi-Fi安装完成，MUSIC已恢复

产品归档`.tools/firmware-artifacts/20261110-lyrics111-8a85c35a4c7f`，5174496B／槽余2848B，131输入／13变化。BIN SHA `8a85c35a4c7fd5e1c288fe61adb5291735dab9e454f817f88a9193d638ece10e`、ELF `380b3143d690b8559199fe2a078039ef14fa5e601e3b37154fd2ed3e6e6b71c7`、manifest `7a62467e4055df47e41844c64ef6f817f8a5fc4f1c3a1395c42cca5a595f6162`。38/38 Host、实际LVGL7431项通过，35帧基线一致。

维护111归档`.tools/firmware-artifacts/20261110-maintenance111-68c020b0c21d`，709168B，ELF `4930085aa7690475d56af29478a64a030e69e68c5764bf4b9858f392a4572e11`，manifest `caa9a4e5835365137478eb580f157a0f1088fa6f576a554a18c9b61d0dcff6a7`；首次启动绑定原110的精确ELF，32项维护策略测试通过。分区、bootloader、资源镜像不变。

11月10日准备时，原110连续运行约26小时，最大连续内存3840B；一次MUSIC OFF POST被接受，10样本稳定，原ON状态保存在`logs/lyrics111-music-pause-20261110.json`。关闭后最大连续内存仍3840B，未在该状态上传。11月11日用户完成重启，独立预检确认原110／ota_1／VALID、HOME、MUSIC OFF／generation0、uptime47552ms、free52007／largest40960、电量100%。旧低内存与上传中断存在关联，尚未证明OTA故障根因。

第一跳一次上传709168B、HTTP200／32.109秒；一次鉴权确认后独立GET核对维护111精确ELF、ota_0／VALID。第二跳一次上传5174496B、HTTP200／111.234秒，独立确认产品111精确ELF、running／boot均ota_1、VALID。两次均完整发送、未中断；全程无USB。原110已被产品111覆盖，回退槽为VALID维护111；旧110保留外部归档。维护111首次启动仍绑定110，不能原样再次上传到当前111。

按原暂停报告恢复MUSIC ON，一次POST被接受，10个样本均boot1／reset3且无新增重启。最终独立读回uptime94096ms、MUSIC ON、HOME，free7479／largest7168，无新增panic；手机此时未连接，track_ready=false、0行／无取词请求。因此本次验证了无线安装与启用稳定性，未做手机实际取词、滚动或实体LCD帧率验收，不能把主机回放当成该项实测。

证据：`logs/lyrics111-after-user-rst-20261111.json`、`logs/lyrics111-maintenance-bridge-install-20261111.json`、`logs/lyrics111-maintenance-product-install-20261111.json`、`logs/lyrics111-music-restore-20261111.json`、`logs/lyrics111-final-status-20261111.json`。补充`logs/lyrics111-post-notes-status-20261111.json`在uptime196090ms仍为同一boot1、无panic、MUSIC ON且手机未连接。工具与交接在`.tools/lyrics111-ops/`、`.tools/ota-maintenance111-ops/`，60项离线防护检查通过。未读取原始ROM序号，111为本地候选号；未做整区MD5或真实超时回退实验，旧OTA根因仍未定位。
<!-- lyrics111-current:end -->

<!-- lyrics110-current:start -->
## 当前110：简体版两步无线安装完成

简体显示产品110归档`.tools/firmware-artifacts/20261109-lyrics110-225c95cc86c7`，BIN SHA `225c95cc86c79aef43ebb6a74e6817c8effa79493a093c3d955181a2ba6e00a1`、ELF `4223f809fb2e1a94d270ebf74c5c5374171726404659e5002d55637039ebbaf2`、manifest `9645fa292ce99d0282e2bc9ac156bb5c8615d25f6f70e1e729df220c2edcd686`。5176384B／槽余960B，131项输入、6项变化，Host38/38、实际LVGL5215项通过。

新维护桥归档`.tools/firmware-artifacts/20261109-maintenance110-c26ed758a3a3`，709168B，首次启动绑定原109 ELF `fe5a13e46c72b685de3f8ec38e4d8d62b06dbc95df1740d2146c70e6ab0eced2`；桥ELF `7cd6aea1cf8f3669ded8150695c5d7a37fcfaa67e8b57d2d8c038585b0003726`。32项安全策略及49项工具防护检查通过。分区、bootloader和资源镜像不变。

实机预检经Wi-Fi确认109／ota_1／VALID、正在LIVE，MUSIC ON且已连接，歌词26行／HTTP200／error0。一次MUSIC OFF请求返回202，10样本同一boot且均已关闭；原ON状态保存于`logs/lyrics110-music-pause-20261109.json`。关闭后最大连续内存仍3328B。用户按一次RST后，独立读回HOME、MUSIC OFF／generation0、free53643／largest40960；未在原低余量状态尝试上传。这与此前成功准备状态一致，但没有证明原异常由低内存引起。

第一段709168B、HTTP200／31.578秒，一次主机鉴权确认后独立确认维护桥ota_0／VALID；第二段5176384B、HTTP200／110.781秒，独立确认产品110 ELF、running／boot均ota_1、VALID。两段各一次上传、无中断。随后按暂停报告恢复原MUSIC ON，一次POST／202，10个样本同一boot1；最终uptime93224ms、手机已连、歌词48行／HTTP200／error0、17355ms、无新增panic。手机当时暂停，机器人停HOME；未作实体LCD目视验收，未读取原始ROM序号，110仅为本地候选号。

证据分别为`logs/lyrics110-after-user-rst-20261109.json`、`logs/lyrics110-maintenance-bridge-install-20261109.json`、`logs/lyrics110-maintenance-product-install-20261109.json`、`logs/lyrics110-music-restore-20261109.json`和`logs/lyrics110-final-status-20261109.json`。全程无USB。当前回退槽是VALID维护110；其首次启动基线绑定109，不能原样重新上传到110。未做整区MD5或真实超时回退实验，旧OTA／HTTP根因仍未定位。以下为历史部署记录。
<!-- lyrics110-current:end -->

<!-- lyrics109-current:start -->
## 当前109：两步无线安装完成，MUSIC已恢复

产品归档`.tools/firmware-artifacts/20261109-lyrics109-5821c5d6477b`，BIN SHA `5821c5d6477b05b65f02971dcb23b80911d0a7ca2a1a40a84005261f69b927d6`、ELF `fe5a13e46c72b685de3f8ec38e4d8d62b06dbc95df1740d2146c70e6ab0eced2`、manifest `97bf18ec9ecea7f45459afd22205a4fc93ff34d9f715136bbb10184cf097a3ad`。131输入、9项改动，5176224B／槽余1120B；Host38/38、实际UI4221项、真实源响应10/10和全部BMP映射恒等已验证。

新的维护109已绑定当前108 ELF作首次回退基线，归档`.tools/firmware-artifacts/20261109-maintenance109-c9947b3b1762`，709168B、ELF `74ea489f326b2a6516a44b5dd91a40329ca21417eea199ec5a8a4b6c68e09ff1`。维护安全32、部署保护26、归档13、私有元数据读取6项通过。没有改分区、bootloader或资源镜像。

最初用户回HOME后，两次鉴权状态预检发生InterruptedRequest，普通HTTP GET约20ms内连接重置。热点仍On、唯一机器人MAC对应248，ping2/2；这些只证明网络可达，未证明HTTP可用。随后一次有界热点Stop／Start均Success，机器人未自动重连；用户RST后重新发现同MAC位于`<ROBOT_HOST>`，鉴权确认108／ota_1／VALID、HOME和供电正常。原HTTP重置内部原因未定位。

一次MUSIC OFF POST返回202，保存了原ON状态，10样本稳定。第一轮维护镜像上传只发出69632B便中断，旧108发生abort／reset4，仍为ota_1／VALID；旧boot诊断最后OTA stage9、free7287／largest5120，未保留有效故障PC，不能断言具体机制。自动重启后MUSIC仍OFF，片内free51795／largest40960，在这个已改变的状态下重试同一维护镜像成功：709168B、HTTP200／32.922秒，一次主机鉴权确认和独立VALID。

从已确认维护环境上传完整109：5176224B、HTTP200／112.406秒，独立GET核对ELF、running／boot均ota_1且VALID。原始ROM序号未读取，109是本地候选标号。随后按原记录恢复MUSIC ON，一次POST／202，10个恢复样本同一boot1。实机当前Dear John读回synced46行／HTTP200／error0，取词15573ms；最终uptime280116ms、同一boot、无新panic。Free Loop当时未播放，本轮没有实体TEXT ONLY／翻页目视验收。

失败、重试、成功与恢复证据分别保留：`logs/lyrics109-maintenance-bridge-install-20261109.json`、`logs/lyrics109-first-hop-failure-status.json`、`logs/lyrics109-maintenance-bridge-retry-20261109.json`、`logs/lyrics109-maintenance-product-install-20261109.json`、`logs/lyrics109-music-restore-20261109.json`、`logs/lyrics109-final-status.json`。新地址通过各工具`--host`传入；私有restore工具补上了该参数，先dry-run后执行，未改已冻结固件。全程未使用USB。

当前回退槽为VALID维护109／ota_0，旧108已由109覆盖，仅保留外部冻结归档。新的维护桥首次启动绑定108，不能原样再次刷到当前109。未做新ROM／NVS／资源整区MD5或真实超时回退实验，原完整应用OTA／HTTP异常原因仍未解决；不能把本次绕过并安装成功写成根因修复。
<!-- lyrics109-current:end -->

<!-- lyrics108-current:start -->
## 当前108：两步无线更新成功，实机同步歌词41行

已完成107/ota_1 → 新维护桥/ota_0 →108/ota_1。第一步709168字节、HTTP200／32.234秒，一次鉴权确认后独立VALID；第二步5176336字节、HTTP200／111.516秒，独立核对108 ELF `0c5116b202ab247c1f1e79cc1cc306270d5cba08a15a404382301bc3739397d1`、running／boot均ota_1、OTA状态VALID。108是本地候选标号，未读原始ROM序号。全程无USB。

安装准备时发现电脑热点关闭，已恢复原`<HOTSPOT_SSID>`（Off→Success→On），未修改凭据、频段或网卡。重连后Windows热点客户端将同一MAC对应到新地址`<ROBOT_HOST>`，随后再次通过鉴权ELF／MAC确认设备；旧地址不可达不是本次升级失败。用户回HOME后读回idle_blocked=true、电量100%且正在充电，才暂停MUSIC并安装。

MUSIC已恢复原ON，一次POST／202，10个样本保持同一boot1。手机重连后，同一原始曲目字段读回synced／41行、error0／HTTP200；未见新增panic。证据：`logs/lyrics108-maintenance-bridge-install-20261109.json`、`logs/lyrics108-maintenance-product-install-20261109.json`、`logs/lyrics108-music-restore-20261109.json`、`logs/lyrics108-final-status-20261109.json`，实际曲目信息留在ignored诊断目录。

机上备用为已确认的维护108/ota_0，107仅保留外部归档。维护桥32项安全测试、部署保护26项通过；原完整应用OTA异常根因、真实超时回退和冷启动供电行为仍未验证。工具位于`.tools/lyrics108-ops/`和`.tools/ota-maintenance108-ops/`；新的桥首次基线为107，不可原样重刷到当前108。旧归档／旧工具保留。
<!-- lyrics108-current:end -->

<!-- lyrics107-installed:start -->
## 当前107／ota_1／VALID：两步Wi-Fi恢复已完成

2026-11-09先在106连续运行约62小时的状态下再次验证直传：MUSIC OFF、HOME、额度服务ready，真正POST前距下一次自动查询285412ms，仍在镜像校验返回后最后stage13异常重启。该窗口没有隔离全部任务，也不能观察手动刷新命令队列；它不支持把问题简单归结为周期额度查询。新失败证据：`logs/lyrics107-window-wifi-install-20261109.json`、`logs/lyrics107-window-evidence-20261109.json`、`logs/lyrics107-window-failure-20261109.json`。

随后构建并审查最小维护固件，32项状态策略与25项部署守卫通过。冻结归档`.tools/firmware-artifacts/20261109-maintenance-b3245926f5f0`，镜像709168字节，BIN SHA256 `b3245926f5f0c1a334838244e3471c02a7e44b33ebb495e9c9e876faeaeb2a73`，ELF `0c9f17864f4f8303bcd42e41b556903c27f12f5e531e4448b033654be4005a35`，manifest `ee54cb01801981155a3e3d4b1584df4d1dbd2b8a83963000ed8d4edd0f41326f`，2,430项冻结输入。只读NVS视图、RAM Wi-Fi配置、关闭PHY存储保护现有设置；没有上传bootloader、分区表或资源镜像。没有新的ROM／NVS／资源整区MD5读回，不把代码级保护等同物理校验。

第一步：一次Wi-Fi POST完整接收709168字节、HTTP200，含启动确认35.407秒。维护固件启动于ota_0／PENDING_VERIFY；主机核对MAC／精确ELF／健康状态后，仅一次鉴权POST确认，返回200，再独立GET核对VALID。确认前实测内部free281604、largest237568、HTTP栈余9456字节。此时106／ota_1仍作为回退。证据：`logs/maintenance-bridge-install-20261109.json`。

第二步：在维护固件VALID后，一次POST接收完整5165008字节107镜像、HTTP200、112.359秒；随后独立确认107 ELF `8b765bd53e63fbe711c291101e786d9854f98dec4fe703c4713a54155a079564`、running／boot均ota_1、VALID。107的BIN SHA仍为`4671a8753f64519c261ad511155ca7ca9c8ae70eebd865a6fb646ec5a9392a61`，未重新编译或修改匹配候选。第二步覆盖106所在槽，当前回退镜像是此前确认VALID的维护固件；106保留外部归档。LAN未直接读取原始OTA序号，107为候选标签，不冒称seq107。证据：`logs/maintenance-lyrics107-install-20261109.json`。

原MUSIC ON状态恢复：一次POST202，10个样本boot1／reset3一致，手机已重连；随后uptime183874ms仍无新重启，额度服务ready／HTTP200。RTC诊断布局随维护／产品镜像变化，不能把新镜像boot1当成清除全部历史故障的证明。当前输入为非音乐内容，歌词0行，Spotify歌曲实机取词待确认。证据：`logs/maintenance-lyrics107-music-restore-20261109.json`、`logs/lyrics107-final-status-20261109.json`、`logs/lyrics107-weekly-after-maintenance-20261109.json`。实际媒体字段仅保存在ignored诊断文件，不写入公开记录。

全程未使用USB。这是本次无线恢复成功，不是原应用OTA根因已修复；两步同时改变镜像大小、任务和内存环境，不能据此单独归因。维护固件绑定106作为首次启动基线，不能原样重新刷到107；冷启动供电及实际超时回退尚未做物理测试，已验证的是本轮暖启动、确认与两次上传。回退机制使用[ESP-IDF 5.5.4原生OTA状态](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/system/ota.html#app-rollback)，没有手写OTA序号。

补充此前入口核查边界：ConfigAP源码确有自定义OTA URL网页，但106板类覆盖为BLE配网，相关Start／StartWebServer和网页资源在106 ELF中未定义、map处于丢弃段，不能作为当前无线备用入口。不能把map交叉引用表中的名称误当可执行入口。
<!-- lyrics107-installed:end -->

<!-- lyrics107-current:start -->
## 历史：107直传未完成时的106记录

106诊断版第一次Wi-Fi上传中断，主机发送73728字节后失去连接，设备abort／reset4回到105；最后OTA标记在一次`esp_ota_write`返回前，无具体故障指令。重启后MUSIC保持OFF，同一106镜像完整上传HTTP200／5164688字节／105.75秒，独立确认ELF `55be904df4bf2c9b6c6984580bcf1e67bd2d8027446eb4bf13f9be5177e46e5d`、ota_1、VALID。两次报告均保留，不将第二次成功当作已修复历史OTA崩溃。

Windows在17:29意外重启，System事件41／6008、BugcheckCode=0，无已知具体原因；原热点因此关闭。已恢复`<HOTSPOT_SSID>`（实测信道11），机器人重启后同MAC获得新IP `<ROBOT_HOST>`，106独立身份再次通过。该Windows重启与之前106上传时的设备重启分别记录。

107匹配修复已构建、Host38/38通过并冻结129输入。bin SHA256 `4671a8753f64519c261ad511155ca7ca9c8ae70eebd865a6fb646ec5a9392a61`，ELF `8b765bd53e63fbe711c291101e786d9854f98dec4fe703c4713a54155a079564`，manifest `47773b66b1cfcad0294e2a382abea5da6f34aea93284c8da0030b1fe2823cb60`，目标ota_0。一次202命令关闭MUSIC并保存原ON状态，10个样本确认关闭；用户RST后读回generation0、HOME、MUSIC OFF、有效电量100%，才执行单次Wi-Fi上传。

107上传发送完整5165008字节但无ACK，180秒后仍独立确认106／ota_1／VALID。重启原因4，留存abort=true但无有效PC；OTA最后stage13，记录序列15158、marks7577，stage12的`esp_ota_end`已成功返回。stage13后是`esp_ota_set_boot_partition`，内部包含再次校验镜像及otadata映射／擦写，现有记录不能细分故障指令，也不能完全排除尚未进入诊断初始化的后续启动故障。最近内存采样位于stage4，不能当成stage13的即时内存。失败报告：`logs/lyrics107-wifi-install-20261106.json`、`logs/lyrics107-wifi-failure-state.json`。旧105目标槽已经被尝试覆盖，不再声称旧105整槽保留；106有效回退仍独立确认。

源码及106 ELF核对：set_boot本身无abort调用，常规失败返回错误；底层有多个可达assert，但未定位触发者。HTTP task默认内部RAM、6144字节，当前证据不支持PSRAM栈或巨额栈需求解释。每次flash写同步返回，故最后一块前停顿没有“等待已有写完成”的依据；未执行该低信息量重试。

用户反对反复切换USB后，撤回“USB是必要下一步”的判断，未执行任何USB操作。另作一次同镜像、4KiB后续间隔60→90ms的客户端限速试验，发送4984832／5165008字节时中断，无ACK，仍为106／ota_1／VALID。上一启动OTA最后stage9／marks7209（read_exact之前），abort=true、info/frame均无效。内存仍是stage4的旧采样，非故障时采样。服务端总接收期限120秒，90ms节奏已接近这个期限，因此也不能视为完全隔离时间预算的纯速率实验。证据：`logs/lyrics107-wifi-retry-slow-20261106.json`、`logs/lyrics107-slow-failure-state.json`。不同失败阶段不支持只归因于最终启动切换；目标ota_0现应视为部分写入、未经验证，不能直接激活第一次校验过的镜像。

已停止重复上传，通过一次POST／202恢复原MUSIC ON状态，10个样本保持boot3／reset4，恢复窗口手机曾连接、处于暂停。随后独立读回106／ota_1／VALID、uptime695192ms、同一boot3，MUSIC仍ON但手机已断开；没有新的设备重启。证据：`logs/lyrics107-failure-restore106.json`、`logs/lyrics107-after-restore-status.json`。107尚未成功启动或完成实机歌词验收。

备用无线入口只读核查：SETUP固件升级URL来自既有更新服务，APP.CENTER来自服务器应用列表，两者未找到本地自定义URL入口；AI Agent确有user-only MCP工具`self.upgrade_firmware(url)`，106 map确认已链接，但需要先进入AI Agent并拥有合法MQTT／WebSocket服务端会话，HOME没有直接HTTP调用入口。BLE配网和原厂Avatar WebSocket也没有升级命令。未操作云端、DNS或TLS配置，且pull OTA同样需要写flash／切换分区，不能保证规避本次abort。USB工具仅作离线准备与33项保护检查，没有实际连接、读取或写入设备。

后台隔离核查：HOME不会停止codex-weekly查询／凭据更新或IMU任务；现有HTTP只有音乐启停，不能暂停这些任务。两次失败报告未保留weekly状态／下次查询时间，不能断言与TLS重叠。106 ELF确认IMU使用4KiB PSRAM栈，headtouch和lyrics均为内部栈；RTC fast heap也包含INTERNAL／8BIT能力，而panic记录器仅接受SOC_DRAM地址范围。因此info_valid=0不能直接证明PSRAM线程或IMU是故障来源。目前没有足够新证据支持第三次上传，根因保持未定位。
<!-- lyrics107-current:end -->

<!-- distance105-wifi:start -->
## 当前105候选／ota_0／VALID：Wi-Fi安装已完成

用户追问无线更新后，在未修改候选固件的情况下，沿用4KiB分块／首块停4秒、之后每块60ms的限速流程，单次POST获得HTTP200和完整5,164,288字节接收确认；含重启确认耗时107.39秒。随后独立读取匹配ELF `ddb42b8a7b63a6157ce7ead5b7b0d2db03854d6f048538bc909580f9aa9e730f`、running／boot均ota_0、OTA状态VALID。没有使用USB。报告`logs/distance105-wifi-install-20261106.json`；本次LAN未直接读原始OTA序号或整槽／NVS／资源MD5，不能借用旧USB证据冒充新校验。

`logs/distance105-wifi-schedule.json`实机读回北京时间周五16:08，与电脑时钟一致；monitoring=true、schedule=automatic、有效距离620mm、age59ms、error0。自动工作日检测已在当前状态生效，周末摸头及真实跨09:30／21:30切换仍只有主机测试证据。音乐开关已按原状态恢复ON；一次202请求后的5个样本保持boot3／reset3，最后connected／playing／track_ready均true，歌词行数当时为0，不宣称新歌曲取词已完成。

纠正此前判断：内部largest=168字节是实际读数，但不是“Wi-Fi必定不能安装／必须USB”的充分证据。第一次暂停准备仅在读取阶段失败，没有POST；第二次读到boot2／reset4和既有panic时仍未发暂停命令，之后一次MUSIC DISABLE202成功，最大块3072字节，再完成本次OTA。保留失败与panic记录，不将重启归因为随后发出的暂停／OTA，也不声称本次成功已修复既有内存／重启问题。记录：`logs/distance105-wifi-music-pause.json`、`logs/distance105-wifi-music-pause2.json`、`logs/distance105-wifi-music-restore.json`。
<!-- distance105-wifi:end -->

<!-- distance105-pending:start -->
## 历史：105安装前准备（已由上方Wi-Fi结果接续）

候选已完成ESP-IDF构建与38/38主机测试，尚未上传或写入设备。归档`.tools/firmware-artifacts/20261106-distance105-ac9c71a46761`，bin SHA256 `ac9c71a46761f1b02b6b9205b6fd669375f4d230076882e15034dfc30d6c05c7`，ELF `ddb42b8a7b63a6157ce7ead5b7b0d2db03854d6f048538bc909580f9aa9e730f`，manifest `d6db5c3d47074eebe2a2095e4c90511a6d3b4342a8e562408008fd6ccc9baca7`。镜像5,164,288字节，槽余量13,056字节。

首次LAN读取未成功，随后重试已确认104／ota_1／VALID；证据`logs/distance105-before-status.json`与`logs/distance105-before-retry-status.json`。成功读回时片内free=1,335／largest=168字节，USB没有枚举到此前COM38。尚未发起105 OTA；不能把首个读取失败归因为设备离线，也不能把后续HTTP可达当作足够OTA运行内存。已请求临时连接USB完成安装；105的实机工作日／摸头开窗验收仍未建立。

USB工具`.tools/distance105-ops/install_usb.py`已准备；默认只作本地验证，已实测executed=false。离线保护测试22/22通过（`logs/distance105-usb-guards.log`）。实际安装需显式`--execute`，先核验真实104／E000／VALID和完整回退镜像，作废旧103／D000，再写105至ota_0；保护104完整槽、NVS、启动区和资源。尚未执行该步骤，不将离线dry-run当作部署成功。
<!-- distance105-pending:end -->

<!-- lyrics104-current:start -->
## 当前104／ota_1／VALID：左对齐版已安装

归档 `.tools/firmware-artifacts/20261106-lyrics104-e986d2adb480`，镜像5,162,336字节／槽余量15,008字节；SHA256 `e986d2adb4804abe714831750e921db444ba746dd8d4f1e95e3a0f1f4f18184b`，ELF `cdbab0c46590c1506d54295c6deed93f7d1ef93920d0dbf90c7ed1c51b0f50bf`，manifest SHA256 `31919d45c485e671a0412859b0ce4ac50e9dc9047cc4ffc7b588d39ee032332c`。114项构建输入中，仅view.cpp一处对齐值变化。

USB先验证103／D000／ota_0，再使旧102／E000不可启动，写ota_1并核验。实际ROM读回104／NEW与103／VALID；完整103槽、NVS／启动前缀（除E000）、资源与尾部MD5保持不变，独立LAN确认104／ota_1／VALID。90秒启动采集无panic，包含安装触发的一次USB复位。报告：`.tools/device-backups/20261106-121025-093280-lyrics104-usb-recovery/deployment.json`。旧102保留外部归档。

MUSIC原状态为ON，已恢复并读回为ON；10个恢复后样本保持同一boot4／reset11。最新状态暂无手机歌曲信息，未声称连接或实体LCD验收完成。36/36 Host、3172项生产LVGL检查、15项安装保护检查通过。最新审计：`logs/device-lyrics104-audit.json`；104没有Wi-Fi上传尝试。以下为历史记录。
<!-- lyrics104-current:end -->

<!-- lyrics103-current:start -->
## 当前103／ota_0／VALID：USB安装已完成

103仅改变歌词显示，归档 `.tools/firmware-artifacts/20261106-lyrics103-3a6a9a68731c`。镜像5,162,336字节／槽余量15,008字节；SHA256 `3a6a9a68731c5d7593916c6c356f26029fe1cac01b6d12ebb052a4d9f188e2c1`，ELF `ff1358631ad84d2f5b642ca143c6b87131665210b8003736a1b3fa26aa981a31`，manifest SHA256 `9a2d6612a49097bf15d26bdc7e8872ce1157ae0221dee5360a77d6b5becfa992`。114项构建输入仅view.cpp相对102改变，36/36 Host和新的生产LVGL验证通过。

本轮一次103无线安装失败：用户重启后片内free51,951／largest40,960字节、MUSIC disabled、HOME状态均已核对。上传在主机发出90,112字节后中断，无HTTP接受回执，180秒确认窗口内始终是102／ota_1／VALID。随后读回reset4、previous panic aborted=1但info/frame均无效；OTA最后stage7／marks43只是写入调用区间，不能等同故障PC。实际Flash写入字节数未知，inactive ota_0的旧101不再视为可用回退镜像。

用户接入后已核对COM38／设备MAC。USB安装先验证真实102／VALID、完整102镜像与原厂启动前缀，再使D000旧101条目不可启动，写ota_0并核验，最后写103／NEW。ROM读回seq103／NEW及seq102／VALID；完整102槽、NVS、bootloader／分区表、资源和尾部MD5不变。备份为私有前缀加既有不可变102镜像，不冒称新读出整个备用槽。90秒启动采集无panic，包含一次安装触发的USB复位；独立LAN随后确认103 ELF／ota_0／VALID。部署记录：`.tools/device-backups/20261106-112023-775923-lyrics103-usb-recovery/deployment.json`。

一次MUSIC ENABLE返回202，手机连接且歌词34行／HTTP200；10个样本保持同一boot3／reset11、uptime连续。另60秒UART无panic／reset／诊断错误。reset11属于USB启动，未改称软件reset3。最新审计：`logs/device-lyrics103-audit.json`。实体LCD观感未验收；本次USB成功不证明旧Wi-Fi abort已修复。

准备／失败证据：`logs/lyrics103-after-rst-safe.json`、`logs/lyrics103-wifi-install-20261106.json`、`logs/lyrics103-after-upload-interruption.json`；固件／UI：`logs/lyrics103-idf.log`、`logs/lyrics103-host.log`、`logs/lyrics103-ui-final.json`。下方版本段落为历史快照。
<!-- lyrics103-current:end -->

<!-- lyrics102-current:start -->
## 当前 102／ota_1／VALID：字体内存修复已安装

2026-11-06，归档 `.tools/firmware-artifacts/20261106-lyrics102-9b4a4282fe2d`：镜像5,164,576字节，槽余量12,768字节；SHA256 `9b4a4282fe2d54af0b205809337f68e7d9f3a18fde15f23e485240b147d07d91`，ELF `a13ebba2ac00a7ee119c386cde97ee0b70d35414c6c6f8dedf3d4d511da10d9a`。114项输入冻结，36/36 Host、IDF构建及独立审查通过。

USB先核对实际101／VALID元数据、MAC、原厂前缀及101镜像；备份前缀，将旧100目标条目标为不可启动，再写备用ota_1并核验，最后写102／NEW。真实ROM读回102／NEW及101／VALID；完整101槽、NVS、bootloader／分区表、资源与尾部MD5保持不变。该保护校验不等于新读出整个101槽的副本。独立LAN随后确认102 ELF、运行／启动ota_1、VALID。报告：`.tools/device-backups/20261106-100415-489460-lyrics102-usb-recovery/deployment.json`。101保留于ota_0，旧100仅保留外部归档。

ENABLE对照：旧101实录控制器分配失败与EM映射断言／INT_WDT；新102一次ENABLE返回202、完成至BLE stage21／error0，手机连接成功，10样本同一boot3／reset11且uptime连续，60秒UART无新panic／reset／分配失败。reset11来自本次USB启动，未冒充软件reset3。显示窗口仍是无可用曲目／0行，实体歌词与刷新效果待验证。证据：`logs/lyrics102-enable-once-20261106.json`、`logs/lyrics102-enable-uart-20261106.json`、`logs/device-lyrics102-audit.json`。

此前101“未安装”已是历史：今日一次完整Wi-Fi上传成功（HTTP200、104.609秒），随后才进行上述UART定位和102修复。此前间歇性OTA panic没有因此被确认为已修复。以下版本段落保留原时点记录。
<!-- lyrics102-current:end -->

<!-- diagnostic101-pending:start -->
## 最新状态：100 保留运行，101 两次安装失败

2026-11-05 最新独立 LAN 证据 `logs/diagnostic101-after-recheck-safe.json`：100 ELF `315c34417b3f2afd7814d69d4cf68755fce5cf074e697636ae8df60041b46e18`，运行／启动 `ota_1`、VALID，RTC boot7／reset4，Music disabled。100 的 ENABLE 中断看门狗回归仍未修复。

101 已冻结 110 项输入；固件 5,164,544 字节、槽余量 12,800 字节，SHA256 `51c5794e9db3069b7e9d7fe3bdf3a060121055f225244a3fc35bfb57d04918d4`，ELF `2ab5bc8232a3daef570ba5b925116f0243e2331911c10dc6c5afae51d8afe8e0`。IDF 构建、34/34 Host 测试和最终 panic／BLE 链接审计通过；它仅增加诊断，不能称为修复。

两次完整上传分别保存于 `logs/diagnostic101-wifi-after-small-install.json` 与 `logs/diagnostic101-wifi-end-recheck-install.json`，均无接受回执、未切换到新 ELF。第一次全量主机发送，前一启动 OTA 最后 stage11／marks7589；第二次主机发送 94,208 字节，前一启动 OTA 最后 stage7／marks49。实际 flash 写入量未知，最后阶段不是故障 PC。两次均为 reset4，不能认定是稳定的镜像内容／最终校验错误。

停止盲目重试，等待 USB 串口采集当前 100 的 ENABLE 崩溃；此时电脑没有枚举到机器人。inactive `ota_0` 内容已改变，不是已核验的 99 回退槽；100 的当前 LAN 身份成立，没有新整槽／NVS／资源 MD5 结论。审计入口：`logs/device-diagnostic101-audit.json`。以下记录保留为当时的历史快照。
<!-- diagnostic101-pending:end -->

<!-- diagnostic100-current:start -->
## 当前 diagnostic100 诊断记录

独立LAN确认100候选ELF／ota_1／VALID；100是预期标签，未实读ROM序号。完整上传HTTP200、5,162,368字节；99 ota_0回退仅由inactive更新机制推断，无新整槽／NVS／资源MD5。旧98 ota_1已经覆盖，外部归档与以下99／98历史记录保留。

**仍未修复：**一次ENABLE没有HTTP202确认（TimeoutError），1次断连后8样本均reset5／current boot5；RTC previous是boot4／reset7，不能改写成正常reset3。BLE最后stage34／marks19，表明controller_init已经返回、controller_enable没有记录stage35返回。free38,619／largest27,648／minimum37,875来自更早sample_stage8／mark16／time162250，不能称为enable前即时内存。内部根因仍未确定。

首次100 OTA失败时host sent806,912字节、无ACK；RTC previous OTA stage9／marks1089表明begin已返回并已多次写入，停在后续读取区间，不是初次擦除失败。host送出量不是实际flash写入量。对应失败报告不覆盖。

本版仅诊断，FunctionReady=false；Host33/33、99项冻结输入，视觉仅复用98对应未改源码。101仅准备新增数字panic地址采集，未安装、未声称修复。证据：`logs/device-diagnostic100-audit.json`、`logs/diagnostic100-wifi-after-small-install.json`、`logs/diagnostic100-enable-once.json`、`logs/diagnostic100-wifi-install.json`、`logs/diagnostic100-after-failed-upload-safe.json`。
<!-- diagnostic100-current:end -->

<!-- diagnostic99-current:start -->
## 当前 diagnostic99 诊断记录

保存的独立LAN证据确认ELF／ota_0／VALID；99是预期标签，未实读ROM序号。完整Wi-Fi上传HTTP200、5,162,288字节、104.422秒。98 ota_1回退仅由inactive更新机制推断，未做新整槽／NVS／资源MD5；旧97 ota_0已经覆盖，外部归档与全部失败记录保留。以下98章节是历史记录。

**未修复：**一次远程ENABLE没有收到HTTP202（TimeoutError），随后采集1次断连及8个相同reset_reason=5（ESP_RST_INT_WDT）样本；新99 ELF仍为VALID。RTC previous boot与BLE lane均有效，最后stage=8，internal free=38,615、largest=27,648，源码对应nimble_port_init之前；没有stage9返回标记。这只缩小失败初始化区间，不等于内部根因，也不能证明与OTA重启同因。实际init标记不等于HTTP确认。

本版仅用于诊断，FunctionReady=false，当前音乐disabled／gen0／lines0，未验证歌词或LCD观感。冻结94输入、Host32/32、IDF通过；视觉证据仅复用98对应未改源码，不是新99 UI实测。当前开始diagnostic100准备，未安装或声称修复。

证据：`logs/device-diagnostic99-audit.json`、`logs/diagnostic99-wifi-after-small-install.json`、`logs/diagnostic99-initial-safe.json`、`logs/diagnostic99-enable-once.json`。
<!-- diagnostic99-current:end -->

<!-- lyrics98-current:start -->
## 当前98 Wi-Fi更新记录

本次保存的独立LAN证据确认候选ELF／ota_1／VALID；98是预期升级序号，未通过ROM实读。正常97回退由inactive槽更新机制推断，未做新的整槽、NVS或资源MD5校验；旧96所在ota_1已被本次更新替换，旧镜像与全部历史失败记录保留在外部归档。

冻结63项输入，删除旧1bpp字体并替换为新2bpp灰度字体，新增跟踪pet_avatar源码。镜像5,157,664 bytes、槽内余量19,680 bytes。黑底、当前句位图缓存、隐藏宠物绘制抑制已纳入新生产源码；播放／LRC时轴未改。Host30/30、新生产UI报告通过。主机绘制和flush计数不代表物理LCD FPS。

这里的63项输入及UI证据通过不可变98归档验证；当前工作树已经进入diagnostic99开发，相对已部署98有5项输入差异，详见审计，不把当前源码冒充98构建输入，也不声称99已安装。

本次状态：connected=False，ready=False，HTTP=0，lines=0，fetch_elapsed_ms=0；仅按本次记录描述，不与旧97的42行／5400ms样本推算帧率或同曲加速比。物理观感、声音时序与新的真人交互验收尚未建立。

**当前未解决回归：**完整上传得到HTTP200，耗时104.062秒；安装后3个样本为reset_reason=3。用户随后操作MUSIC ENABLE后，保存的2个样本变为reset_reason=5（ESP_RST_INT_WDT），并显示enabled=false、generation=0、render inactive。新ELF仍为ota_1／VALID；这证明安装已完成，不代表功能可用。重启根因尚未确定，不能由本次状态推断故障栈帧，也不能宣称此前OTA panic已经修复或与它同因。无线调查继续进行，物理歌词显示尚未验收。

证据：`logs/device-lyrics98-wifi-audit.json`、`logs/lyrics98-wifi-steady-install.json`、安装后`logs/lyrics98-after-steady-installed-safe.json`、最新ENABLE后`logs/lyrics98-enable-reboot-safe.json`、`logs/lyrics98-ui-final.json`。
<!-- lyrics98-current:end -->

<!-- lyrics97-current:start -->
## 当前97实测与显示修正（较早96及之前内容按历史记录保留）

USB已验证97 NEW，保存的独立LAN证据确认新ELF／ota_0／VALID；接口不直接暴露序号。镜像5,035,600 bytes，余量141,744 bytes。先作废D000旧93，再覆盖ota_0；正常96／E000及整ota_1、NVS、其余启动区和资源通过保护摘要校验。旧93安装和所有失败记录继续保留。

纠正已确认的显示差距：96真实生产页面是静态的，没有实现预览中的动效；此前静态／源码一致性检查不能证明预览效果已经上机。97有新的真实生产LVGL多帧／动效报告，但它仍不是物理LCD帧率、流畅度或听感时序验收。

当前设备：当前状态未证明同步歌词取回成功，HTTP=0，lyrics=waiting_track，lines=0；状态字段fetch_elapsed_ms=0，同generation启动日志观察到的最大请求数=None。历史96首次为37165ms／6请求；两者不是已控制的同曲A/B，不能据此宣称加速倍数。新冷路径三请求属于源码／主机验证预期，不能替代本次实际日志。

97将精确404且有专辑的compact v2／ID兼容匹配提前，保持三字段一致、唯一最近、min(10秒,3%)及时间戳不越实际时长，最多8请求／45秒软预算。两首LRU按五字段完整签名匹配，仅缓存Synced／Instrumental，96KiB是估算payload总上限而非实测总RAM；缓存恢复不依赖Wi-Fi。完整同值通知不使缓存／请求失效。暂停保留音乐页，不再15秒退出；断连／禁用仍清除。33ms固定对象动画只在播放态计时、暂停静止、clear销毁timer，未实测LCD FPS。未增加音频、依赖或调整优先级。

证据：`logs/device-lyrics97-usb-audit.json`、`logs/lyrics97-ui-final.json`、`logs/device-lyrics97-status.json`。92触摸静置证据保持其原有范围，未新增真人摸头／LIVE验收。
<!-- lyrics97-current:end -->


<!-- lyrics-fixed96-current:start -->
## 本轮96状态（本段优先；下文较早部署记录按当时证据保留）

USB已读回seq96 NEW；本地保存的独立LAN证据确认新ELF／ota_1／VALID。LAN接口本身不暴露原始序号。镜像5,030,704 bytes，余量146,640 bytes；临时94先标ABORTED再被覆盖，正常93／ota_0及NVS、其余启动区和资源通过整槽保护校验。

本次《你要的全拿走》的实际手机时长为293016ms；同歌名／歌手／专辑的公开记录标为290000ms和299000ms，携带完全相同的103行LRC时间轴。只改变请求时长即可复现严格404与200，确认时长门槛足以解释这组请求差异；时长标签为什么不同、歌词与音频是否逐句吻合尚未证明。新生产解析器离线重放40项检查通过，旧LRCLIB39／网易49／酷狗45行响应仍通过；证据见 `.tools/lyrics-actual293/brief-summary.json` 和 `.tools/lyrics-fixed96-probe/summary.json`。

匹配兼容：原严格源链保持±2秒；仅精确LRCLIB为NotFound且有专辑时，在链尾追加v2 metadata search与ID下载。歌名、歌手、专辑须全同，时差不超过min(10秒,实际时长3%)，唯一最近ID且下载后复核；全部LRC时间不得超出实际时长，不平移或缩放。最坏8请求，共享45秒软预算、64KiB响应上限和250ms间隔。

实际状态：已收到同步歌词（103行）；HTTP=200，lyrics=synced，lines=103。这与听感时序／LCD验收分开，物理对齐尚未测量。临时明文tuple日志已移除；原失败、PC探针和92触摸静置证据保留，未新增真人摸头／LIVE验收。旧87 UI报告仅作源码未改证据，没有新渲染验收。

诊断更正：Windows列出WINUSB设备并不代表JTAG可用；实际OpenOCD在连接GDB前即以LIBUSB_ERROR_NOT_FOUND退出，未附加／暂停CPU，也未改Windows驱动。实际匹配参数来自随后临时94的USB诊断，不能把失败JTAG记录当作读取成功。

本地证据：`logs/device-lyrics-fixed96-usb-audit.json`、`.tools/device-backups/20261105-151434-lyrics-fixed96-usb/deployment.json`、`logs/device-lyrics-fixed96-status.json`。
<!-- lyrics-fixed96-current:end -->

日期：2026-10-30。设备为 COM38 的 ESP32-S3 rev 0.2，16 MiB flash；Secure Boot、Flash Encryption 均未启用。用户已确认 ToF4M 接到红色 PORT A，窗口朝前。

## 原设备备份

完整备份保存在本机 `.tools/device-backups/20261030-124710/flash-original.bin`，共 16,777,216 字节。
分段读取通过设备 MD5，拼接后再次与设备整片 MD5 比较一致；分区表与独立 ROM 读取一致。

- SHA256：`9334bf7752c87f2faa0ea29005f067c9197dad25cc55d0ce57463b1d1c9ad3df`
- 设备整片 MD5：`b202384152810e7e18aacc558270eb4a`
- 分析记录：同目录 `inspection.json`、`partition-table.csv`。

备份包含设备配置和凭据，仅保存在 Git 忽略目录；迁移开发机器时应单独妥善保留。

实际分区表与本地构建一致：NVS `0x9000/0x4000`，otadata `0xd000/0x2000`，phy_init `0xf000/0x1000`，ota_0 `0x20000/0x4f0000`，ota_1 `0x510000/0x4f0000`，assets `0xa00000/0x400000`，coredump `0xe00000/0x10000`。

当前备份纠正了旧日志状态：原 ota_1 实际为有效的 **stack-chan 1.6.0 / ESP-IDF v5.5.4**，并非早先启动日志中的 1.5.1。ota_0 为 flappy_bird，启动记录 seq3 / ABORTED；ota_1 为 seq2 / VALID。两个镜像均通过 checksum 和追加 SHA256 校验。

## 资源兼容与部署范围

原资源与构建资源均为 42 项，40 项字节相同。另两项已离线核验：

- `index.json` 只改变数组顺序；表情按名称、额外文件排序后完全相同，字体和模型引用相同。
- `srmodels.bin` 都是 `wn9_histackchan_tts3`，内部三个文件逐字节相同，仅目录排列和偏移变化。加载器 `managed_components/espressif__esp-sr/src/model_path.c` 按文件名称与 offset 加载，不依赖目录序号。

| 模型文件 | 字节数 | 原/新共同 SHA256 |
| --- | ---: | --- |
| `_MODEL_INFO_` | 45 | `96b2615dc51d471796fd5da91330c33733a8ff45ee465692cc630fddbda4572a` |
| `wn9_data` | 289665 | `dbba353b4b5cb923984de753012671aa122cd4373739173956d2071ba9618eb3` |
| `wn9_index` | 1200 | `5810ee84beac8e73b4080a7b765e09900627ff973f2654811dd6a8557f0769b4` |

因此保留原 assets。首次部署镜像为子仓库 `84c6e47ce82469acb3b44b537c64da26a58577b8` 的距离提醒构建，3,741,584 字节，SHA256 `272b23202cc98049d62afe971ba037a67a8bf48422b0f1169b41d1b952bc0505`；本轮调参后的当前版本见下文。

仅写 app 到 ota_0 `0x20000`（擦除范围 `0x20000–0x3b1fff`），校验后再写 `0xd000` 的单个 4096 字节启动扇区，记录 seq5 / NEW / CRC `0xc8210fcd`。保留另一启动扇区、原厂 ota_1、bootloader、分区表、NVS、phy_init、assets。普通 `idf.py flash` 会额外写入这些分区，本次不使用。

部署脚本、原启动扇区及逐步摘要记录保存在上述备份目录及 `.tools/deploy_distance_firmware.py`；不作为可在任意设备上复用的通用刷写入口。

若需要回到保留的原厂槽，本机备份中的 `otadata-slot0-original.bin` 恰为原 `0xd000` 扇区。
核对仍是同一设备后，仅恢复这个扇区即可让另一条 seq2 / VALID 记录重新选择 ota_1，
无需改写应用、资源或 NVS；恢复后的重启与主菜单应再验收。完整恢复整片备份是另一个操作，不与选择原厂槽混用。

## 工具问题留底

原 esptool bulk 读取默认端口超时为 None，出现过无限等待。改用明确超时、有期限的分段读取；512 字节包失败时回退到 256 字节，再严格校验摘要，最终完整备份成功。实际丢包的底层原因尚未证明。

首次部署尝试在第一条写入前因重复 `run_stub()` 被拒绝，没有修改 flash。外部已运行 stub 交给 `esptool.main` 时需正确标记 `sync_stub_detected`，避免再次覆盖 RAM 工具；失败日志 `deployment.log` 保留，修正后的执行另记日志。

## 首次部署的上机结果

两次写入与 MD5 校验成功。启动前再次核对保护区域，与原备份一致：bootloader、分区表、NVS、phy_init、原厂 ota_1、第二 OTA 记录、assets 均未被刷写改变。运行固件可能正常更新设置，不能把这次部署前后的校验延伸为运行时永不写配置。

日志：`.tools/device-backups/20261030-124710/deployment-fixed.log`、同目录 `deployment.json`、`logs/device-distance-first-boot.log`。
HAL 首次报告 `ota confirm check: partition=ota_0, state=1`，随后请求 `marking current app valid`。
之后独立读回整个 otadata 并校验设备 MD5：sector0 为 seq5 / VALID / CRC 正确，sector1 为原 seq2 / VALID，且 sector1 全扇区与备份逐字节相同；记录在 `ota-confirmation.json`、`otadata-after-confirm.bin`。
再经 USB 硬件复位，仍收到有效 `SC_DISTANCE`，20 秒后 HAL 报告 `partition=ota_0, state=2`；日志 `logs/device-distance-reboot.log`。这是 USB 复位重启验证，未实际断开电源测试。

实际接线后 ToF 采样通过。使用现有电脑端 `parse_line` 对真实日志固定窗口离线解析：

| 项目 | 实测 |
| --- | --- |
| 窗口 | seq29–1044，设备 uptime4782–105912 ms |
| 解析与有效性 | 1016/1016 帧成功、有效，raw_status9，error0 |
| 内部缺帧/重复/回退 | 均为 0 |
| 采样间隔 | 中位 100 ms，平均 99.635 ms，范围 80–120 ms |
| 距离 | 中位 800 mm，范围 787–815 mm；未用量尺验证绝对精度 |
| 本机告警 | 此窗口全部 false，尚未覆盖靠近和移远 |

这证明真实 ToF 读取、USB 输出和电脑协议兼容；脸旁符号、跨菜单/AI、传感器拔插和语音唤醒尚未据此验收。

电脑程序也已使用真实 COM38 运行：`python -m distance_reminder --port COM38 --no-notify`。
控制台显示 `state=safe sensor=ranging robot_alert=off` 和实时约 800 mm 距离；按 P 进入 paused 时继续接收距离，再按 P 经 waiting 恢复 safe，按 Q 正常退出码 0 并释放串口。此次关闭桌面通知，不据此声称实际近距通知弹窗通过。

用户随后确认“可以的，有效果”，并反馈感应等待太长、符号不明显。该反馈确认第一版可用，不能代替后续新版视觉验收。

## 第二次调参：更快、更明显

按用户实测反馈，将机器人持续判定从 5 秒缩短到 1 秒，保留三样本中值和 48 cm 恢复迟滞。
在约 10 Hz 采样下，从新出现的近距输入到告警，含滤波预计约 1.1–1.2 秒；精确现场时延仍需日志中有实际近距边沿才能确认。
电脑通知仍使用独立的默认 5 秒，不借本次修改改变其规则。

原 40 px 角落图标改成水平居中、眼睛上方的 80×80 px 红图，并加 8 px 红边框及顶部 “TOO CLOSE”。
提示容器不接收点击和滚动；原 Home 手势、底部语音气泡保持原区域。解除只删除提示图层，不恢复旧 emotion，因此不会覆盖期间收到的新表情。
这是源码坐标核对；任意远程自定义眼嘴位置可能与提示重叠，未做截图对照验收。

固件子仓库提交 `e24db6d`；二进制 3,742,576 字节，SHA256 `f8637727a7990dd1cef68f35bd8a349193a94ea40d8db4dbf23dc880c6dbaeed`。
源码变更为 `distance_sensor/proximity_policy.h`（默认时长）、`stackchan/stackchan.cpp`（独立提示层）、`stackchan/avatar/decorators/angry.cpp` 与同目录 `decorators.h`（复用缩放接口），以及 `tests/proximity_policy_test.cpp`（默认时序与原场景回归）；路径相对 firmware/main，测试相对 firmware。未改变电脑规则或原 AI 表情数据。
最新构建日志 `logs/firmware-20261030-141414.log`；CTest 3/3，规则 17 组，包含默认 999 ms 不触发、1000 ms 触发和移远中值恢复。

上一版可用镜像另存 `20261030-124710/distance-first-working.bin`。本次刷写前再次备份当前前 128 KiB（含运行后的 NVS 和启动记录），保存在 `.tools/device-backups/20261030-141710-faster-warning/`，原完整备份继续保留。
仍只写 ota_0 和启动扇区 0；新记录 seq7 / NEW，保留原厂槽和另一 VALID 记录，逐段校验当前配置及原资源。
部署日志 `logs/device-update-faster-warning.log`；两次写入、当前 NVS/bootloader/分区表/原厂槽/资源的保护摘要均通过。
独立读回新版启动记录为 seq7 / VALID / CRC 正确，sector1 全扇区与原记录相同；同目录 `ota-confirmation.json` 可核对。USB 重启后的日志 `logs/device-faster-warning-reboot.log` 仍有有效测距，HAL 确认 `ota_0, state=2`。

新版采集 `logs/device-faster-warning.log` 共 1203 帧（seq194–1396），全部有效、无序号缺帧，采样间隔中位 100 ms；汇总 `logs/device-faster-warning-summary.json`。
实际记录到 4 次触发和 4 次解除。四次从中值近距成立到 `robot_alert=true` 均为 **1000 ms**，从首次远距读数到两个样本的中值恢复均约 **100 ms**。

| 周期 | 近距判定成立 uptime(ms) | 告警开启 | 告警解除 |
| --- | ---: | ---: | ---: |
| 1 | 38402 | 39402 | 62142 |
| 2 | 96242 | 97242 | 102122 |
| 3 | 103022 | 104022 | 105022 |
| 4 | 106522 | 107522 | 120982 |

这些是传感器与本机规则的真实时延，不包含肉眼观察或 LCD 渲染延迟；新版红框、文字与图标的实际观感尚待用户反馈。

## 第三次部署：DISTANCE 调试入口

用户希望直接查看距离，以排查感觉触发不灵敏的问题。新增内置 Mooncake 页面 **DISTANCE**，紫色图标，追加在既有 App 后以保持旧 appID；右上角固定 **HOME** 按钮返回。源码入口 `apps/app_distance/app_distance.cpp`，只读展示原始距离、中值、近距状态和计时、当前阈值、样本年龄及错误，不改变采样与规则。

采样任务在短临界区发布完整 `Snapshot`，页面每 100 ms 复制一次；UI 不访问 I²C、不推进判定。
超过 1500 ms 没有新发布的数据，快照读取会在副本上清除距离和计时，显示 STALE / --。
原始无效光学读数仍保留用于诊断，并明确标记 INVALID。原有表情告警继续读取既有新鲜度判断。

为让串口日志能和屏幕交叉核对，SC_DISTANCE v1 新增可选 `filtered_mm`、`near_ms`、`proximity_state`，与屏幕来自同一份采样状态；现有 PC 解析器兼容。新增页面、service.h/.cpp、App 注册、主机测试及 CMake 为本次固件范围；电脑端只追加协议兼容测试。

源码提交 `3a64d7c82ed56ae160150a49abd3ee341e3262b5`；固件 3,751,744 字节，SHA256 `19b63305e95315708e83ab29e2ef8369dea49351634f07660f3f1f041deb62e8`。
构建日志 `logs/firmware-distance-debug-20261030-143830.log`；CTest **4/4**，含新增快照 **8 组**（过期边界、副本不变、无效值、自定义超时和时间回绕），原 17 组规则继续通过；PC **57 项**通过。

上一版另存 `20261030-141710-faster-warning/distance-second-working.bin`。当前前 128 KiB 配置与 OTA 备份、刷写和确认记录在 `.tools/device-backups/20261030-144309-distance-debug/`。
仍只写 ota_0 和启动扇区 0；新记录 seq9 / NEW，所有保护区域摘要通过。独立读回 seq9 / VALID / CRC 正确，另一 seq2 / VALID 扇区保持原值。部署日志 `logs/device-update-distance-debug.log`。

刷写后日志 `logs/device-distance-debug.log` 固定采集到 **903 个连续有效帧**（seq317–1219），采样间隔中位 **100 ms**、无缺帧，PC 解析拒绝数 0。
新 `filtered_mm` 与独立三样本中值计算逐帧一致，`proximity_state` 和 `robot_alert` 一致；此窗口原始距离 960–1001 mm，未出现近距边沿，不能当作触发时延测试。
汇总 `logs/device-distance-debug-summary.json`；USB 重启日志 `logs/device-distance-debug-reboot.log` 仍有有效测距，HAL 确认 `ota_0, state=2`。

刷写前旧版 25 秒窗口有 250 个连续有效帧、277–545 mm，未见采样停顿；首行有前次端口关闭留下的旧半帧拼接，已从统计中排除，不据此推断传感器停顿。
用户随后确认“可以的，很灵敏”，说明距离页面可用且读数跟随符合其现场感受；没有单独确认 HOME 触摸返回。用户推测此前表情显示不及时，目前没有 LCD 时延测量来确定该解释。

## 第四次部署：动态表情与实时调参

按用户要求，DISTANCE 默认进入 LIVE：复用 DefaultAvatar，只增加本页面拥有的 Blink/Breath 表情动画；底部固定 HOME/TUNE。切到 TUNE 可直接看 F（中值）/R（原始）cm、状态和有效性，三组 40 px 高加减按钮调整 TRIP cm、CLEAR +cm、WAIT sec，LIVE 返回验证表情。

机器人默认等待从 1000 ms 改为 0 ms，中值低于 400 mm 当帧判定 Alert，恢复仍为 480 mm；保留三样本滤波和 1500 ms 断流清除。这是去掉额外的持续等待，不能据此声称光学采样或 LCD 零延迟。

请求参数和快照在短临界区复制，policy 仍只由独立采样任务操作。每帧先应用参数，再处理新的有效/无效读数；真正修改时清除旧 latch 和计时，但保留仍新鲜的中值窗口，相同参数不重置。
快照记录同一帧实际参数；SC_DISTANCE v1 新增可选 threshold_mm、hysteresis_mm、hold_ms，用于和屏幕交叉核对，原电脑端兼容。

SAVE 请求由 app task 在 LVGL 锁内摘取、锁外写 NVS，锁内显示结果。只有显式 SAVE 才保存 sc_distance/config 单个版本化 blob；开机校验尺寸、版本和范围后恢复，错误或缺键使用默认。保存期间有新编辑会提示 NEWER EDITS，不将后续编辑误标为纯 SAVED。

独立审查发现原 Avatar 基类没有虚析构，而 StackChan 通过 unique_ptr<Avatar> 持有派生脸；反复切页会使派生屏幕面板无法正确释放。因此仅增加 virtual ~Avatar() = default，未扩展其它脸部功能。现有 LVGL Object wrapper 对已由父容器删除的子对象有 isValid/release 防重删路径。
新增实际 Avatar 头的生命周期回归：100 次 reset 与 100 次替换检查派生 RAII、五官、气泡和装饰资源释放；隔离移除虚析构后两组均运行失败，恢复后 2/2。该主机测试不包含真实 LCD 或 LVGL 分配器，不替代现场切页验收。

最终固件 3,755,584 字节，SHA256 `2263d163805563ce04587427b5bcdff970d4c2b17fa2669110c695925bfb4cbe`；构建日志 `logs/firmware-20261030-164246.log`。CTest **5/5**（规则30组、快照9组、生命周期2组，另有原驱动11组与运动），日志 `logs/host-tests-20261030-164441-926-test.log`；电脑端57项与模拟通过，日志 `logs/pc-distance-live-tests.log`。

上一版可用镜像已归档到 `20261030-144309-distance-debug/distance-third-working.bin`。本次配置前缀/启动记录备份和部署记录在 `.tools/device-backups/20261030-164605-distance-live/`；只写 ota_0 与启动扇区0，新记录 seq11 / NEW；原厂槽、当前配置、bootloader、分区表、资源及另一启动扇区保护摘要均通过。部署日志 `logs/device-update-distance-live.log`。

独立读回 OTA 记录为 **seq11 / VALID / CRC 正确**，sector1 原 seq2 / VALID 全扇区不变；记录在同备份目录的 ota-confirmation.json。再次 USB 重启后，日志 `logs/device-distance-live-reboot.log` 返回 **301/301 有效帧**、无序号缺帧，HAL 确认 `ota_0, state=2`。

首轮真实记录 `logs/device-distance-live.log` 为 seq149–1050 共 **902 连续帧**，892 有效、10 无效，PC 拒绝数0，无序号缺帧；采样间隔90–110 ms、平均99.767 ms。每个可独立计算的中值与上报一致，规则边界检查无不一致。
出现两次近距开启：seq279（中值232 mm）和seq669（中值356 mm），均在进入 Alert 当帧 `near_ms=0 / hold_ms=0`，没有额外1秒等待。
seq287 因无效测量清除；seq672 中值483 mm 达到恢复阈值后解除。首行旧半帧与 boot 日志拼接，已排除，未计作协议帧。
汇总 `logs/device-distance-live-summary.json`；这些是采样与判断证据，不包含 LCD 显示时延。此窗口参数始终400/80/0，尚未记录现场加减或 SAVE，因此未声称编辑参数后的保存/重启往返已实测。

固件提交 `2698e9bac05914a22a84185f6b6214d276bf6332`；本次改动文件为 apps/app_distance/app_distance.h/.cpp、distance_sensor/proximity_policy.h 与 service.h/.cpp、stackchan/avatar/avatar/avatar.h，以及 tests/CMakeLists.txt、proximity_policy_test.cpp、distance_snapshot_test.cpp、avatar_lifetime_test.cpp、专用 fakes/avatar_ui/。路径相对 firmware/main，测试路径相对 firmware。
复用原脸、动画、图标、字体和 NVS，无新增依赖；两屏取代单一只读页，继续复用原告警图层。现场 LIVE/TUNE/HOME 触摸、表情观感、编辑后 SAVE/重启恢复及完整 AI 回归仍未独立验收。

## 第五次部署：电子宠物情绪与眼神动作

按用户选择的电子宠物风格，将 DISTANCE LIVE 的单纯眨眼/呼吸替换为 11 状态的宠物脸：平静、好奇、开心、撒娇、害羞、委屈、困倦、惊讶、单眼眨、严肃提醒和放松。开心有笑眼与 8px 弹跳，撒娇有 12px 侧摆和腮红，委屈有垂眼、小弯嘴和泪滴。普通动作以 280ms 平滑转场穿插眼神停留、回看和不等间隔眨眼。

告警信号变化直接抢占动作并绕过 33ms 动画节流；有效距离恢复时短暂放松，无效/过期数据不庆祝。采样、阈值、TUNE/SAVE、NVS 格式和 SC_DISTANCE 均沿用上一版。仅 DISTANCE LIVE 换脸，其它 App 保留原厂脸和既有全局提醒。删除该页面单独持有的 Blink/Breath modifier，合并为一个动作时间表；图元继续复用 LVGL，无新依赖。

实际 LVGL 桌面渲染揭示初版笑眼与嘴部的双轮廓，已改成先压扁再弯曲的笑眼和一条变形嘴线，并增加委屈/严肃的上睑斜切。最终 11 姿态与动画无采样边界裁切，420 帧正常刷新与强制整屏重绘像素一致；100 次创建/销毁后子对象、LVGL 可用内存和分配块数不变，完整性检查通过。图集与限制见 [Face Lab](demos/face-lab/README.md)；这些是实际生产代码的桌面渲染，不是 LCD 实拍，未含完整控件和警告红框。

主机测试 CTest **6/6**，新增动作 **13 组**覆盖 64 组转场、告警即时优先、无效数据恢复、100,000 帧边界/有限值与时间回绕。日志 `logs/host-tests-20261030-182110-194-test.log`。完整 ESP-IDF 构建通过，最终 **3,762,976 字节**，SHA256 `527756678afd18f61865d1f25d87d24643b04e4375999c7d8c839587b42b50c3`，分区余量约 27%；日志 `logs/firmware-20261030-182654.log`。

更新备份目录 `.tools/device-backups/20261030-182853-pet-face/`；前一版镜像已在 `20261030-164605-distance-live/distance-live-working.bin` 校验留存。部署前核对 COM38、MAC、上一版 app SHA 和 seq11/VALID，只写 ota_0 与 `0xd000` 启动扇区0。设备摘要核验当前 NVS、bootloader、分区表、另一启动扇区、原厂 ota_1 和资源不变；部署日志 `logs/device-update-pet-face.log`。最终镜像归档为本次目录内 `distance-pet-working.bin`。

独立读回记录为 **seq13 / VALID / CRC 正确**，原 seq2/VALID 全扇区不变，见同备份目录 `ota-confirmation.json`。正常重启后 HAL 确认 **ota_0 / state=2**，`logs/device-pet-face-reboot.log` 有 **301/301** 连续有效样本（seq178–478），无序号缺帧、协议拒绝、中值或规则不一致。

首轮 `logs/device-pet-face.log` 的 60 秒窗口有 **602/602** 连续有效样本（seq138–739），原始距离 145–591mm，采样间隔 90–110ms、平均 99.734ms；每个可复算中值和判定均一致，协议拒绝与缺帧为 0。7 次进入 Alert，全部 `near_ms=0 / hold_ms=0`；6 次达到恢复距离后解除，窗口结束时处于另一次提醒。首行旧半帧与启动片段拼接已排除。汇总分别为 `logs/device-pet-face-summary.json` 与 `logs/device-pet-face-reboot-summary.json`。

固件本地提交 `e3d1e91ce1258be9ea7d140b0b11e29c5d3f9e3d`。改动为 `apps/app_distance/app_distance.cpp`、新增 `pet_avatar.h/.cpp` 与 `pet_motion.h`，以及 `tests/CMakeLists.txt`、`pet_motion_test.cpp`。本次采样证据不包含 LCD 延迟或用户对表情的最终观感；触摸、编辑后 SAVE/重启往返和完整 AI 回归仍未独立验收。

## 第六次部署：响指候选与摄像头辅助（2026-10-31）

用户连接 USB 后，重新枚举 COM38，核对 MAC、此前电子宠物镜像摘要和 seq13/VALID。初次部署 `e8cc8ec` 的 3,776,496 字节镜像，SHA256 `c9fd38285fd433febdba4fee19f5bd31adab0a8bb7a231a3935ea1a5c2b31cfc`。备份目录 `.tools/device-backups/20261031-132642-snap-sensing/`，部署日志 `logs/device-update-snap.log`；仍仅写 ota_0 与启动扇区0，seq15/NEW，原厂槽、当前配置、bootloader、分区表、另一启动扇区与资源摘要均通过。

**该初版未通过实机验收**：用户报告进入 DISTANCE 就重启，`logs/device-snap.log` 捕获两次相同崩溃。`SC_SNAP` 输出在 `visual_age` 处出现字面 `li`，随后 `LoadProhibited`。旧 ELF 摘要以 `f4010ed2e` 开头，与崩溃记录一致；解码 `logs/device-snap-crash-decoded.log` 指向 `nano-vfprintf_i.c` → `printf` → `snap_service.cpp:134` 的 `emit_telemetry`。

本机 `sdkconfig` 启用 `CONFIG_LIBC_NEWLIB_NANO_FORMAT=y`，ESP-IDF 5.5.4 `components/newlib/Kconfig:110` 明确说明不支持 64 位整数格式。新日志错误使用 `PRIi64`，破坏后续可变参数解析；桌面完整 printf 与固件编译没有暴露这一运行时差异。已将仅需 -1 哨兵与新鲜帧龄的字段改为 `int` / `%d`，保留现有 nano 配置，不扩大为全局 libc 替换。不能将此前 CTest/桌面检查当作实机可用结论。

修复提交 `8b11962020be3c249afaefe360922b90849e0a20`，完整构建日志 `logs/firmware-20261031-133112.log`；最终镜像 **3,776,464 字节**，SHA256 `6e75aae593da35c926d331d5984212852d29117eeaf38ecd7a0b95c59667fbb0`。先核对当前 seq15/VALID 及初版镜像摘要，再写入 ota_0 和启动扇区0，seq17/NEW；部署记录 `.tools/device-backups/20261031-133312-snap-printf-fix/` 与 `logs/device-update-snap-fixed.log`。再次验证当前配置、bootloader、分区表、原厂槽、资源和另一启动扇区不变；最终镜像归档为该目录内 `snap-sensing-deployed.bin`。

先采集 25 秒启动与测距日志，再独立进入 ROM 读回启动记录：**seq17 / VALID / CRC 正确**，原 seq2/VALID 扇区全字节保留。证据为该目录 `ota-confirmation.json`。随后正常重启，`logs/device-snap-fixed.log` 的 HAL 确认 `ota_0 / state=2`。

修复后的 150 秒窗口有 **1506 个连续 SC_DISTANCE 帧**（seq69–1574），776 有效、730 明确无效；没有序号缺帧、协议拒绝、中值或规则不一致，也没有 panic/assert 标记。不能将无效帧算作有效测距。白名单审计汇总为 `logs/device-snap-fixed-audit.json`，审计器的 11 组合成输入检查通过。

**本窗口未观察到修复后 DISTANCE 启动，也没有 SC_SNAP 帧**。因此只确认了新版写入、启动记录、重启与距离通路；修复后的入口回归、MIC/CAM 状态、实际响指响应和误触发率仍等待用户现场操作，未声称感知功能已通过实机验收。初次失败日志与译码记录保留，不用无崩溃的菜单窗口替代进入 App 的验证。

## 第七次部署：响指播放黑底 KURO GAMES 片头

按用户确认的黑底 Logo 版本，从[库洛 PGR 官网](https://pgr.kurogame.net/)公开的 **Aeon Reforged** 宣传片截取原始 0–3.4 秒。保留淡入淡出；该区间静音，没有添加人声或音轨。中心裁切到 4:3 后缩放为 320×240，12 fps、41 张 baseline JPEG；包 176,092 字节，SHA256 `ebb2f97d8e294b1571f711d9b9477269eac9a97e495808915563accf1bc873f3`。来源与转换参数保存在固件 `apps/app_distance/media/source.json`，根目录 `scripts/Prepare-KuroIntro.py` 可用已有 ffmpeg 重建；重复生成逐字节相同，41 帧均独立解码成功。

一次新鲜 Accepted 同步占用播放器，独立任务按时间解码并跳过落后帧，不排队；最多持有两张 RGB565 图。播放期间同步暂停本功能的输入并作废相机旧会话，结束、取消或错误后清空旧候选并恢复预热；累计计数与用户配置保留。完成基线阻断播放前间隙的事件补播。全屏黑底覆盖原控件但保留 HOME，新的距离 Alert 会中断片头。App 关闭先停感知再停播放器，所有阻塞等待在 LVGL 锁外。仅使用现有 JPEG 解码与 app 分区，不修改输出音频路径、不新增依赖。

固件提交 `4ebe86a`；应用 **3,958,176 字节**，SHA256 `2db3b2c8e43727f6c92ed2c935adcd0b0ed3686d346bc5da892eb913056ebd8f`，最小应用分区剩余约 24%。完整构建 `logs/firmware-kuro-intro.log`；主机 CTest **8/8**（新增包解析 6 组），电脑端 **57 项与模拟**通过。静态独立审查未发现锁反转或图像引用失效；底层 checked 输入关闭和 JPEG 解码没有已证明的总时限。

真实生产 View、LVGL 9.4、字体和 `LvglAllocatedImage` 的桌面验证：12 张截图、**36 项行为/生命周期及 304 项边界检查**通过，文字裁切 0，6 次图像释放中仍被 LVGL 借用的次数 0。覆盖 busy 期间连续事件不重启/排队、完成基线、恢复后新事件、空帧快照保持图像、错误/取消、Alert 与 HOME；旧时钟顺序反例按预期失败。证据 `.tools/intro-ui-probe/manifest.json`、同目录 `frames/contact.png`、`logs/intro-render-run.log`。感知和播放器为替身，未纳入全局红色告警装饰；这些结果不代表真实 LCD、触摸、JPEG 解码速度或识别准确率。

重新核对 COM38、MAC、当前 seq17/VALID 与上一版 app SHA 后更新。备份与当前镜像归档 `.tools/device-backups/20261031-141300-kuro-intro/`，文件 `kuro-intro-deployed.bin`；部署日志 `logs/device-update-kuro-intro.log`。只写 ota_0 与 `0xd000` 单个启动扇区，新记录 seq19/NEW；当前 NVS、bootloader、分区表、原厂 ota_1、原资源和另一启动扇区摘要保护校验均通过。

25 秒启动后独立读回 **seq19 / VALID / CRC 正确**，原 seq2/VALID 扇区全字节不变，见同目录 `ota-confirmation.json`；随后 USB 重启再采集 `logs/device-kuro-intro.log`。实际进入 DISTANCE、识别触发与连续响指播放行为的现场结果另据该日志与用户反馈记录，不从菜单窗口推断通过。

用户确认“播一次，结束后恢复；播放中响指无影响”。150 秒窗口记录 5 次 request→playing→finished，均 error0/cancelled0；512 条 SC_SNAP 均可解析，最后恢复 listening/audio_open/ready。SC_DISTANCE 为 seq41–1543 共 1503 条，其中 1501 有效、2 明确无效，无缺帧、中值或规则不一致，没有 panic/assert。原始日志和 `logs/device-kuro-intro-audit.json` 保留。

审计仍为 `attention_required`：每次恢复输入都有一次 SDK 的 `i2s_channel_disable: the channel has not been enabled yet`，共 5 条，不能把本窗口写成“零错误日志”。源码闭环为：checked close 已关闭并核验 RX；恢复时 `esp_codec_dev_open()` 先调用厂商 `audio_codec_data_i2s.c` 的 `set_fmt`，其再次无条件 disable RX 且忽略该返回值，随后重新配置并 enable；checked open 再核验 RX 已启用。实际随后恢复采音，未因这条日志改动 codec、掩盖日志或提前启 RX。这一解释仅限该条重复关闭记录。

### 后续灵敏度反馈与证据

用户随后反馈第一次灵敏，之后往往没反应、间隔一段时间又能触发；宠物表情变化让其难以区分识别和播放。新的 180 秒 `logs/device-kuro-intro-responsiveness.log` 有 876 条 SC_SNAP，候选与接受仅各增加 1、融合拒绝计数不变；该一次接受有完整播放周期，摄像头处理样本增加 1325。同期 1805/1805 个距离样本有效、连续，规则与中值复算一致。这支持当前问题主要出在融合前的声音过滤，而非已接受事件被播放器漏掉。

按 `uptime_ms-event_age_ms` 去重并排除采集前旧记录，得到 34 条以 tonal 结束的短瞬态：edge0.0461–0.773 全部低于固定 0.8，crest2.514–11.332 均通过原 2.5；duration10–60ms。因此这些记录由 edge 条件过滤。没有逐次声源标注或原始 PCM，不能将 34 条全部认定为响指，也不能以重复上报的 event_reason 次数计算识别率。另 6 条 long_sound 是保留观测，不等于 6 次独立声音。SENS 只调幅度门槛，不改变原固定 edge 门槛。

### seq21：短衰减瞬态与明确就绪提示

源码提交 `217f37a` 保留原 broadband 分支，另允许 duration≤30ms / edge≥0.45 / crest≥3.0 的短高峰瞬态，原幅度、保护与摄像头规则保持。8 个连续衰减合成夹具（含跨块）在旧规则全拒、新规则全过，4 个边界负控仍拒；原 12 组保留，检测器 14 组、CTest8/8，日志 `logs/host-tests-20261031-142121-166-test.log`。短敲击和衰减铃声也可能通过，合成测试不代表现场识别率。

LIVE 顶部增加 READY、WARMING、SOUND FILTERED 和画面/距离状态，日常宠物动画不作为识别反馈。首次桌面验证抓到 Alert 首帧标签未隐藏，已改为每帧跟随告警显隐、文字仍100ms刷新。真实 View 最终 57 行为与761边界检查通过、0裁切，6次释放无借用违规；证据 `.tools/intro-ui-probe/responsive-manifest.json` 与 `responsive-frames/contact.png`，原证据存入 `baseline-intro/`。

完整构建 `logs/firmware-kuro-snap-response.log`；镜像 **3,958,976 字节**，SHA256 `e22df798f9002fab7fcbf8277fbaf8c750a2e69d3a59408a55944a84bd0a29ac`。核对上一版 app 和 seq19/VALID 后，仅写 ota_0 与启动扇区0，部署日志 `logs/device-update-snap-response.log`；备份与归档 `.tools/device-backups/20261031-142411-snap-response/snap-response-deployed.bin`。保护区域摘要校验通过，独立读回 **seq21/VALID、CRC正确**，原 seq2/VALID 全扇区保留。重启后以 `logs/device-snap-response.log` 采集现场复测，未用上一版单次成功代替修正版灵敏度验收。

**seq21 仍未通过识别验收。** 用户按 READY 测试三次仅成功 1–2 次，之后明确报告敲键盘持续触发。`logs/device-keyboard-false-positive.log` 中 accepted 从15增至25，camera_samples 从926增至1997；配置始终 SENS3、CAM ON。原采集文件名 `device-snap-sens5.log` 不代表实际调到5，不能写成 SENS5 的测试结果。`logs/device-snap-response.log` 中一次播放取消与有效 ToF Alert 同时发生，属于距离提醒抢占，不能据此解释所有漏识别。

现有摄像头门槛仅判断亮度局部运动，没有手检测；此前声音规则放宽并不能解决键盘误触发。已复位回主菜单停止反复播放，记录 `logs/device-snap-reset-menu.log`。用户随后明确授权新增乐鑫官方 ESP-DL 与 hand_detect，继续实现“镜头前举手＋短声音”；模型只输出手框，不识别响指姿态，现场准确率须重新验收。

### seq23：官方手检测与一次举手授权

固件提交 `374a7a4e6e27893b195a8518e9d60ff02ddbc8d4`，新增精确锁定的 hand_detect0.2.0 和 ESP-DL3.3.0，其它依赖版本保持。生产相机路径改为真实 RGB888 手框检测，区域内的大手稳定后才授权一次声音互动；持续有手不能重复授权，接受或1.8秒到期后要求移开再举手。取帧前丢弃排队旧帧，取帧加推理统一受500ms新鲜度预算约束。模型输出手框，不识别响指姿态；几何区域也不等于键盘分类器。

默认 O3 构建超槽935,328字节，未刷入；仅将新增 ESP-DL 组件改为大小优化，并关闭其不用的像素转换，未改 managed 源码或分区布局。最终构建 `logs/hand-model-build-size2.log` 通过，应用 **5,032,096 字节**、SHA256 `23926da2e211574131b0e4422da21944df221659e2c7f2b2a62a575ad53acec9`，应用槽余145,248字节（约3%）。主机CTest10/10、实际View86行为/1367边界检查通过；测试输入和硬件识别分开记录，见 `DEVELOPMENT.md`。

更新前核对 COM38、MAC7c:4f:ad:af:61:f0、seq21/VALID和旧镜像摘要；只写 ota_0 和启动扇区0。`logs/device-update-hand-detection.log` 记录当前NVS、bootloader、分区表、原厂槽、资源与另一启动扇区的保护摘要通过。备份目录 `.tools/device-backups/20261031-154932-hand-detection/`，归档 `hand-detection-deployed.bin`、匹配 ELF 和 map。

启动25秒采集 `logs/device-hand-boot.log` 后，独立 ROM 读回 **seq23/VALID、CRC正确**，原seq2/VALID扇区逐字节保留，证据同目录 `ota-confirmation.json` 及 `logs/device-hand-ota-verify.log`。随后复位回菜单，开始 `logs/device-hand-detection.log` 现场采集；模型加载、推理耗时、举手响应和正常键盘负样本仍须结合该窗口与用户操作确认，不能从菜单启动或主机合成测试推断通过。

该180秒窗口实际记录 **1804/1804 有效 SC_DISTANCE**（seq670–2473），没有序号缺帧、中值或规则复算不一致，未见 panic/assert 或 SDK 错误标记。**SC_SNAP 为0，未观察到模型激活**；`logs/device-hand-detection-audit.json` 正确标记 `not_observed`。这只能补充测距与启动证据，不能据此宣称手检测、推理耗时、响指成功率或键盘负样本已通过。现场测试问题已发出，等待用户实际打开 DISTANCE 并操作。

### seq23 实机故障：SNAP CAMERA ERROR

用户进入后报告 CAMERA ERROR。20秒采集 `logs/device-hand-camera-error.log` 包含98条手检测快照，均 `camera_error=263`（`ESP_ERR_TIMEOUT`）、audio listening/error0、camera_samples0。它们是轮询快照，不能当作98次独立推理。当前源码中该超时出现在取帧和推理返回后的统一500ms预算检查；具体哪段耗时仍未知。旧实现仅在成功发布时更新 `hand_inference_ms`，因此失败帧留下的0ms不能解释为真实推理速度。

诊断提交 `1250d3a` 保留时效门槛，改为在拒绝帧中保留取帧和推理耗时，每5秒最多打印一次包含任务栈余量的32位整数诊断。构建 `logs/hand-timing-build.log` 通过；镜像5,032,480字节，SHA256 `37317e86b0adff3477f3a664a9694571166efca169b43d659739b7a6f1d1e206`。这是定位更新，尚未解决超时，不将编译通过写为故障修复。

本次归档 `.tools/device-backups/20261031-160657-hand-timing/hand-timing-deployed.bin` 与匹配ELF；部署日志 `logs/device-update-hand-timing.log` 验证当前设置和原厂保护区域不变。25秒启动采集后独立读回 **seq25/VALID、CRC正确**，原seq2/VALID全扇区保留，见同目录 `ota-confirmation.json`。首次150秒 `logs/device-hand-timing.log` 仍无 SC_SNAP，审计 `not_observed`；没有获得分段耗时。现有远程接口没有打开内置App的命令，warm reboot索引仅选中菜单图标，仍需要用户点击DISTANCE才能继续实测。

### seq25 实际激活后的超时基线

用户进入 DISTANCE 后仍显示 CAMERA ERROR。35秒 `logs/device-hand-timing-active.log` 中172个SC_SNAP轮询全部为camera_error263，camera_samples保持0；39组连续变化的耗时值中，取帧87–286ms、中位143ms，推理577–1277ms、中位670ms，合计715–1462ms。轮询会重复同一结果，39组变化值也不等于39次独立推理。6条限频拒帧日志的实测总耗时789–1200ms，底层error全部为0，确认推理成功后被500ms新鲜度门槛拒绝。分析见 `logs/hand-timing-baseline.json`，不能据此计算识别准确率。

### seq27：手检测 CPU 调度单变量对照

提交 `82185aea25c3671fbd56e7798abf0bcafacde0eb` 只将 priority1 的 snap-camera 从CPU1移到CPU0。源码 `hal.cpp:200` 与 `stackchan_display.cc:123` 表明CPU1上已有priority3的表情和LVGL任务；模型默认SINGLE_CORE，摄像头接口没有CPU1专属约束。CPU0上的priority3音频仍可抢占推理。调度竞争是待实测的性能假设，模型参数、内存分配、识别门槛与500ms有效期均保持，不把编译通过写成修复成功。

`logs/hand-camera-core0-build.log` 编译链接与槽检查通过，主机CTest10/10通过（`logs/hand-camera-core0-host.log`）。镜像5,032,480字节，SHA256 `8a998727043ca7c3dc493d32e986d34e3eb17b6656f838bff4823734b05be268`，应用槽余144,864字节。归档 `.tools/device-backups/20261031-162745-hand-camera-core0/` 含镜像、匹配ELF和map。

部署 `logs/device-update-hand-camera-core0.log` 验证旧镜像身份并只更新ota_0与启动扇区0，当前NVS、原厂槽、资源、bootloader、分区表和原启动扇区1的保护摘要全部通过。25秒启动采集后独立读回 **seq27/VALID、CRC正确**，原seq2/VALID扇区逐字节保留，见归档 `ota-confirmation.json`。已开始 `logs/device-hand-camera-core0.log`，等待进入DISTANCE后的实际耗时与手检测状态；超时改善、键盘反例与举手正例尚未确认。

用户随后报告空镜头仍为ERROR。完成的seq27采集共482个SC_SNAP轮询，其中10个是初始化零耗时快照，其余472个全部camera_error263，没有Ready帧。排除初始化后，97组连续变化的耗时值中，检测流程672–1046ms、中位806ms，合计848–1212ms、中位968ms；17条限频拒帧日志全部底层error0。见 `logs/hand-core0-timing-summary.json`。换核没有解决超时；没有CPU占用对照证据，不能将先前关于CPU1表情竞争的假设写成根因。

### seq29：局部优化仍超时，随后切换 NFC

`ac43a385fd7663813d805e965b3f004df2c06dfb` 将两个卷积实现文件恢复为O3，其余72个ESP-DL C++单元仍为Os，另增加32位格式的pre/model/post分段日志。镜像5,039,248字节，SHA256 `b17cd70a1038497e1d1784ec571098f2722c7c9219dd3741677e6fda0b56af4d`；归档 `.tools/device-backups/20261031-163805-hand-hot-conv/`，独立读回seq29/VALID、CRC和原扇区保护通过。

用户实测空镜头仍为ERROR。`logs/device-hand-hot-conv.log` 首批分段样本为pre25–27ms/model825–842ms/post1–2ms；完成采集中的28条限频拒帧日志均底层error0，合计884–1136ms，见 `logs/hand-hot-conv-timing-summary.json`。不能将局部优化写为有效提速。

后续完整O3配合7算子注册裁剪在本地构建为4,856,224字节、SHA256 `d1b8566df6bc5bd2d31217073269ee14192d051a2575e89e752316f5a42bc63b`，静态检查见 `logs/hand-graph-o3-review.json`，**未刷入或实测**。用户改用随附NFC卡并明确允许删除handdetect；该实验的镜像、ELF和构建源片段归档 `.tools/hand-graph-o3-research/`，没有继续部署。

### NFC 替代实现与验证范围

用户明确授权引入四个M5Stack官方库并移除手检测。四库固定Git提交，项目侧处理IDF5.5.4组件名称大小写映射；60个保留组件的版本/hash/依赖无漂移，managed源码与官方提交一致。见 `logs/nfc-dependency-verification.json`。删除生产手模型、响指服务、hand_detect和ESP-DL依赖，片头、距离提醒与宠物表情保留。

读卡使用机身ST25R3916、共享I2C总线0x50，仅执行NFC-A UID选择（4/7/10字节）和易失RF状态切换，不写卡片存储。任一传输/选择错误不算移卡；候选无卡须RF关场/开场各实际等待至少10ms后再次确认无响应。服务在播放/告警/无效距离时关闭触发许可，结束后须至少两次有效无卡且跨度达到300ms才能重新触发。错误后约1秒重新初始化，不把错误当作有效移卡。

主机CTest10/10通过，NFC门控11组包含1000次生命周期；实际生产View的LVGL探针875断言（88行为/787几何）、0裁切、6次图像释放/0借用中释放、heap完整性通过。证据 `logs/nfc-host.log`、`.tools/intro-ui-probe/nfc-manifest.json`，视觉判定96/pass已归档 `.omx/state/nfc-ui/ralph-progress.json`。硬件读卡、持卡去重和移卡重触发仍待实机；主机输入为模拟数据。

### NFC 构建就绪，USB 中断导致尚未部署

源码提交 `d3014aab3c78646aeb95e5db550a31343a10262e`。完整固件构建2473/2473步骤通过，日志 `logs/nfc-firmware-build.log`；镜像 **4,025,936字节**、SHA256 `f337d8c73581b0272f9f9649cd133b8d1070fcb48069a4fada954b9cabe87e30`，应用槽保持5,177,344字节，剩余1,151,408字节（约22%）。候选镜像、匹配ELF/map及身份记录归档 `.tools/firmware-artifacts/20261031-nfc-trigger/`，文件名 `nfc-trigger-candidate.bin`；明确标记 `Flashed=false`。

17:16尝试更新时COM38已不在Windows设备枚举中；`logs/device-update-nfc-trigger.log` 显示工具在 `port.open()` 即报找不到端口，尚未读取或写入设备。空的备份目标 `.tools/device-backups/20261031-171610-nfc-trigger/` 不是成功部署记录。最后已核验设备状态仍为 **seq29/VALID**，没有seq31更新或NFC硬件验收结果。

接回USB数据线后，重新核验设备身份与旧镜像，使用新的备份目录完成受保护更新，再读回OTA状态。现场验收包括进入DISTANCE后的初始化、随附卡片读取、持卡不重复、移卡重触发、播放期间刷卡无影响及ToF告警抢占。日志审计工具 `.tools/audit_nfc_log.py` 的12组合成自检通过，但这不替代物理刷卡验证。

### 2026-11-02：NFC 固件部署为 seq31/VALID

用户接回USB后，Windows重新枚举COM38、VID303A/PID1001、序列号<ROBOT_MAC>；更新器再通过ROM核验MAC、旧seq29/VALID、旧app摘要和原厂保护区域。继续使用此前已构建的 `d3014aa` 镜像，4,025,936字节、SHA256 `f337d8c73581b0272f9f9649cd133b8d1070fcb48069a4fada954b9cabe87e30`，未重新构建或修改固件代码。

备份与部署归档 `.tools/device-backups/20261102-093531-nfc-trigger/` 包含更新前启动记录及配置、`nfc-trigger-deployed.bin`、匹配ELF和map。`logs/device-update-nfc-trigger-20261102.log`（另存为 `logs/device-update-nfc.log`）记录三阶段保护校验通过：只写ota_0应用和0xd000启动扇区0，当前NVS、bootloader、分区表、原厂ota_1、资源和启动扇区1保留。

更新后采集25秒启动日志 `logs/device-nfc-boot.log`，随后独立进入ROM读回两启动扇区：**seq31/VALID、CRC正确**，原seq2/VALID扇区逐字节不变，见归档内 `ota-confirmation.json` 和 `logs/device-nfc-ota.log`。验证完成后复位并开始180秒 `logs/device-nfc.log` 采集；启动日志已有持续有效距离读数。NFC初始化、随附卡片兼容性、持卡去重、移卡重触发和播放期间忽略仍须以实际进入DISTANCE后的现场结果为准。

用户进入DISTANCE后确认“播一次，持卡不重复，移开可再播”。完成的180秒采集有97个SC_NFC轮询快照，实际观测到1种UID，accepted从0增至3；3组accepted→playing→finished均error0/cancelled0。NFC初始化与本次随附卡片读出已有实机证据，持卡去重和移卡重触发另有用户现场确认；轮询快照数不等于独立刷卡次数，不据此计算识别率。

同窗口SC_DISTANCE为seq178–1985，**1808/1808有效**，无缺帧、中值或规则复算不一致，未见panic/assert/watchdog。审计 `logs/device-nfc-audit.json` 仍标记 **attention_required**：读卡errors累计增加9次，其中3个轮询保留error=-1（ESP_FAIL），另有一次无法确认RF关场的警告，最终读卡状态恢复ready/error0。当前返回码不能区分具体失败步骤，错误根因未定位；不能将这次基本功能验收写成“全程零错误”或长期稳定性通过。播放中刷卡忽略、ToF/HOME抢占的独立现场试验与精确响应延迟仍未单独标注验收。

### seq33：按用户要求取消 NFC 与测距的联动

用户指出刷卡无需距离限制。提交 `e3f8cdd613a1dca15d88a1a642c6fd4bbb5327ba` 删除NFC服务对测距状态的依赖、App触发入口的有效距离/安全距离门槛，以及距离Alert自动停止片头的逻辑；移除错误的NFC NEEDS DISTANCE提示。正常距离采样、宠物提醒、参数保存、持卡去重、播放忙时不补播和HOME退出保留。未改读卡底层、依赖、分区或测距阈值；此前seq31的短暂读卡错误不据此视为已修复。

完整目标增量构建 `logs/nfc-no-distance-build.log` 与主机CTest10/10 `logs/nfc-no-distance-host.log` 通过。镜像 **4,025,600字节**，SHA256 `ca75828fbe9505db0d546d4511a0ab9faf0d0bfd8e6deff2f369a5465626923e`；应用槽仍为5,177,344字节，余1,151,744字节。

实际生产View/LVGL回归 **1,275断言**（146行为/1,129几何）、0裁切、12次图像释放/0借用中释放、heap PASS；invalid、stale、Near、Alert、filtered=-1均可新刷卡启动，播放中切换距离状态不取消，持卡/忙时不补播与移卡重触发、HOME退出通过。旧seq31距离取消逻辑作为负例按预期失败，旧时钟顺序负例exit42。证据 `.tools/intro-ui-probe/nfc-unrestricted-manifest.json`、44场景联系表与97/pass视觉记录，父级归档 `.omx/state/nfc-unrestricted-ui/ralph-progress.json`。这些是合成服务输入下的实际界面验证，未运行RF硬件或播放器解码线程。

核对COM38、设备MAC、seq31/VALID和旧镜像摘要后，仅写ota_0及启动扇区0。备份与镜像/ELF/map归档 `.tools/device-backups/20261102-095058-nfc-unrestricted/`；三阶段保护校验见 `logs/device-update-nfc-unrestricted.log`。启动25秒后独立读回 **seq33/VALID、CRC正确**，原seq2/VALID扇区逐字节不变，见归档 `ota-confirmation.json` 与 `logs/device-nfc-unrestricted-ota.log`。开始120秒 `logs/device-nfc-unrestricted.log` 采集，等待用户现场近距离刷卡结果；不复用seq31的3次播放作为新版验收。

用户随后确认“过近也能触发，距离变化不打断”。本版120秒窗口有 **2组完整播放，均error0/cancelled0**：第一组接受日志前最近测距为124mm/Alert，播放中34个测距样本全为Alert；第二组接受前为72mm/Alert，播放中12个Alert样本及23个Safe样本，仍正常结束。关联逐行证据见 `logs/device-nfc-unrestricted-distance-playback.json`，未对采样点之间距离插值。近距离启动和播放中距离变化已有实机与用户确认，invalid/stale/未接ToF的情形仍仅有合成界面回归与源码证据。

`logs/device-nfc-unrestricted-audit.json` 记录98个SC_NFC轮询、accepted增2、读卡errors增8（3个快照保留ESP_FAIL）、最终恢复ready/error0。审计仍为attention_required，未隐去或宣称修复底层读卡错误。SC_DISTANCE seq414–1617，**1204/1204有效**，无缺帧、中值或规则不一致，未见panic/assert/watchdog。保留seq31所有历史采集与验收记录，不将不同版本轮询快照合并为刷卡成功率。

## 空闲轮播、MM:SS 与 Codex 周额度（2026-11-02，seq41/VALID）

该次源码 `StackChan` 提交 **372eba8dae2328494d745538b3057b1c4c051d0c**（前置功能d86d53d、USB RX纠正4d7b736）。镜像 **4,036,480字节**，SHA256 **c269f17f17d05069c16ccbdcebafcd1ad81ad40436ac98c44d56ee0a74b41f2c**，原0x4f0000槽剩余1,140,864字节（约22%）。归档`.tools/device-backups/20261102-idle-countdown41/idle-program-deployed.{bin,elf,map}`；构建`logs/idle-countdown-build-final.log`。

HAL后启动独立空闲服务：默认每20分钟到期，自动计满20秒后短暂开心；NFC/操作菜单/AI非Idle/距离Alert及原有需确认ReminderView优先。DISTANCE LIVE顶部显示EYE MM:SS；设置入口TUNE→NFC→IDLE，ON/20min/20sec，显式SAVE。空闲普通周卡6秒/每60秒，普通节目间至少30秒宠物间隔，前台结束后空闲10秒才恢复。

Codex周额度由PC只读`account/rateLimits/read`，按codex桶10080分钟窗口筛选，无模型推理。当前实测75%（查询epoch1793589807），设备已通过真实USB包变为Fresh/75并轮播。完整API响应/凭据不发送到设备；失败与过期均不显示旧百分比。PC默认300秒查询、15秒重发缩短TTL，当前需要USB与电脑助手运行，尚无Wi-Fi投递/系统自启。

保护更新`logs/device-update-idle-countdown41.log`已校验新app与原厂槽、资源、bootloader、分区表、配置保护区。启动40秒全程同端口采集`logs/device-idle-countdown-early-boot.log`，HAL于20秒后标记有效；独立只读`ota-confirmation.json`确认seq41/state2、CRC正确、原始sector1 seq2/state2逐字节保留。读回后再次重启，后续遥测持续正常。

验证证据分开记录：

- CTest **12/12**，包含空闲策略13组、严格wire7组；PC **77/77**。日志`logs/idle-host-tests-final.log`、`logs/idle-pc-tests-final.log`。
- 生产AppDistance/View/Pet与真实LVGL9.4/字体的桌面渲染 **3,775项断言**（1,219行为/2,556几何），0裁切/缺字；900帧各情绪没有侵入新计时行；100轮创建销毁LVGL可用内存前后相同，12 image frees/0借用中释放。桌面有限实验不等同实机LCD/无限运行保证。manifest`.tools/idle-program-ui-probe/manifest-live-eye.json`，actual截图`frames-live-eye-final/live-eye-contact.png`，视觉97/pass。旧69帧/2,534项证据完整保留。
- 最终设备180秒短周期采集`logs/device-idle-countdown-ready-probe-report.json`：184个idle快照，两个完整20秒RestDone，真实75%持续Fresh，12次quota包写入成功；距离1,853/1,859有效（6次无效读数亦保留），0畸形允许帧、0采集串口错误、0观察到的panic/assert/watchdog标记。本段未产生NFC轮询/播放，不能以此宣称现场刷卡抢占通过；NFC/Alert抢占另有实际LVGL回归，seq33历史物理验收保留。
- 前一seq39的80秒实机段收到真实75%，825/825距离有效，Alert确实中断EyeRest，completed保持0；见`logs/device-idle-rx39-probe-report.json`。最终测试只发save=false，未写入短周期NVS；默认恢复与后续只读观察另见`logs/device-idle-default-verify.log`。

保留失败与纠正记录：首次seq35在确认前回退，读取为ABORTED（state4）；未采得第一次启动错误，原因待验证，不能归因于后续发现的USB RX缺陷。同一镜像在同端口采集下seq37/VALID，见`.tools/device-backups/20261102-idle-retry37/ota-confirmation.json`。另一个确定缺陷为IDF5.5.4/no-driver模式的O_NONBLOCK无法读FIFO，独立提取真实SDK函数实验与实机seq39投递均闭环；报告`logs/usb-secondary-vfs-source-probe.json`，修复与被证伪的旧审查结论见DEVELOPMENT.md。最终seq41后第一次采集助手在HAL初始化前写入、超时退出，失败报告`logs/device-idle-countdown-probe-report.json`保留；助手先排空启动日志后重试成功，不将失败记录合并进成功段。

本轮没有修改NFC RF底层，seq33的8次瞬态读卡错误/现场验收记录继续有效；不据新增节目宣称改善读卡稳定性。20秒完成只表示计时未被前台打断，不验证视线或实际休息。跨AI所有真实交互组合与长期运行稳定性仍未全量实测。

## 独立倒计时栏目与信息优先轮播（2026-11-02，seq43/VALID）

用户要求倒计时独立成为栏目，并缩短纯表情占用时间；同时免除重复现场限时确认，有问题再反馈。最终`StackChan`提交 **7bc54b943df7289dabd6fb15d2eb7b3df4bc4b9c**，新增大号 **NEXT EYE BREAK / MM:SS** 栏目。普通轮播改为倒计时12秒→纯表情3秒→周额度12秒→纯表情3秒；周额度尚未到达时跳过，周卡暂未满足冷却时用倒计时补位。20分钟护眼时钟独立运行，换栏不重置；到期20秒休息优先于普通栏目，NFC和前台操作继续优先。已保存的v1设置仅迁移护眼开关/间隔/休息时长，栏目节奏采用新默认值；显式SAVE持久化v2。

完整IDF5.5.4构建`logs/idle-carousel-build-final.log`通过。镜像 **4,037,136字节**，SHA256 **e92b90ab428b5e5ca9d60e54d58714f6f9626cb547f9d61b4894a06f9d7188f7**，原应用槽余 **1,140,208字节**。归档`.tools/device-backups/20261102-idle-carousel43/idle-program-deployed.{bin,elf,map}`，保护更新日志`logs/device-update-idle-carousel43.log`；同端口40秒启动采集`logs/device-idle-carousel-early-boot.log`后，独立只读OTA确认`ota-confirmation.json`为 **seq43/state2 VALID、CRC正确、原sector1逐字节保留**。原厂固件、配置、bootloader、分区与资源保护校验保留。

- CTest **12/12**（空闲策略17组、wire7组），PC **77/77**；证据`logs/idle-carousel-host-tests.log`、`logs/idle-pc-tests-final.log`。
- 实际生产View/Pet/AppDistance与真实LVGL桌面渲染 **4,171项断言**（1,232行为/2,939几何），0裁切/缺字；100次倒计时/周卡创建销毁LVGL可用内存前后均8,374,448字节，12次图像释放/0借用中释放。新增20:00、19:59、00:01、120:00与栏目互转截图；manifest`.tools/idle-program-ui-probe/manifest-card-countdown.json`，联系表`frames-card-countdown-v1/card-countdown-contact.png`，视觉97/pass。源码摘要均与最终工作树匹配。属于桌面实际渲染，非LCD实拍。
- 最终默认20分钟周期实机窗口 **104.203秒**，104个idle快照、真实74%额度Fresh快照101个、7次额度包写入；距离 **1,046/1,048有效**，2个启动无效样本保留。0畸形允许帧、0串口采集错误、0观察到的panic/assert/watchdog标记。报告`logs/device-idle-carousel-probe-report.json`。
- 前两个未受打断的完整轮播周期，在约1Hz遥测相邻边界上分别为 **12.050/3.000/12.120/3.000秒**、**12.000/3.000/12.120/3.000秒**；对应信息约 **80.1%**。该比例排除开机空闲等待、后续距离干扰与末尾配置恢复，不能将全窗口快照占比当作连续正常轮播比例。全部104个快照的`uptime_ms + eye_remaining_ms`均为1,201,928，未观察到栏目切换重置护眼时钟。证据`logs/device-idle-carousel-cadence.json`。

本次未产生NFC刷卡/播放样本；此前seq33用户确认与物理日志、实际LVGL抢占回归继续分别留存，不冒充seq43现场验收。默认参数以save=false恢复，未写入测试周期。PC额度助手以隐藏进程启动，USB连接成功，真实查询摘要为74%（epoch1793591318）；日志仅含安全摘要/固定状态，`logs/codex-weekly-helper.log`及`.err.log`，PID记录`.tools/codex-weekly-helper.pid`。每5分钟刷新，无系统自启动；关闭助手或断开USB后额度会过期，机器人护眼计时独立运行。长期稳定性与所有AI交互组合仍未全量实测。

## 按用户纠正收回 BOTIFIER 应用（2026-11-02，seq45/VALID）

上一版把“空闲轮播”扩展到启动菜单和AI待机，偏离用户意图。用户明确要求保留在原DISTANCE入口，并允许改名；现入口为 **BOTIFIER**，仅其LIVE页渲染栏目。删除main每轮全局render、launcher/AI的可展示上下文及render，另删随AI轮播失去调用方的ReminderView活动计数。退出应用清层，其他页面不显示栏目；没有自动打开或替换原厂主菜单。距离采样仍由`app_main`独立启动，原有全局脸部过近提醒保留。

显示优先级为 **NFC > TOO CLOSE > 护眼及普通信息**。App在同一个LVGL锁内处理NFC/距离抢占并渲染栏目。另修复经源码审查确认的采样窗口：取到Safe快照后，后台采样可能已转Alert，原全局warning已创建但旧Safe仍允许top-layer栏目；在StackChan更新后以实时`alert_active()`和有效快照Alert取并集，统一阻止栏目并隐藏顶部状态。此举不修改阈值、采样、中值滤波、NFC RF或播放逻辑。

源码提交 **e642522cc794dbc4e92f933d02e8917891553505**，5个生产文件增加15行、删除37行。完整IDF5.5.4构建`logs/botifier-app-only-build-final.log`通过；最终镜像 **4,037,008字节**，SHA256 **066a4163f881b0c56fba4dc893cd3e259411c8ccce2a5694920875fe86356d57**，应用槽余 **1,140,336字节**。候选与匹配ELF/map在`.tools/firmware-artifacts/20261102-botifier-app-final/`，受保护部署归档`.tools/device-backups/20261102-botifier45/botifier-deployed.{bin,elf,map}`。

`logs/device-update-botifier45.log`记录原app/配置/原厂槽/资源等保护校验；同端口40秒启动捕获`logs/device-botifier-early-boot.log`后，独立只读`ota-confirmation.json`已确认 **seq45/state2 VALID、CRC正确、原sector1逐字节不变**。没有以启动日志替代OTA元数据读回。

- 主机CTest **12/12**，日志`logs/botifier-app-only-host.log`；PC代码未改动，先前77/77证据保留。
- 实际完整AppDistance/节目View/Pet、真实LVGL9.4、从生产源码原文提取的DistanceWarningDecorator及真实AngryDecorator/图片/24px字体，共 **4,724断言**（1,276行为/3,448几何）、0裁切/缺字、13图像释放/0借用中释放。100次面板循环LVGL free 8,374,448→8,374,448。服务数据和硬件StackChan外壳为替身，不冒充硬件测试。
- 三种信息节目被Alert首帧清除；逐像素检查NFC覆盖真实过近图层，结束仍近恢复TOO CLOSE；三种非LIVE页/退出无残留，BOTIFIER入口名正确。Safe→Alert注入覆盖20ms非刷新帧和100ms刷新帧；只移除新实时读取的负控制准确失败，最终生产源码通过。底部距离/SAFE文字仍按既有100ms刷新，红色大提醒首帧已显示，不将旧底部文字当成提醒未生效。
- 证据`.tools/idle-program-ui-probe/manifest-botifier.json`，104张独立图（15张新增优先级场景）、`frames-botifier-v1/botifier-contact.png`，视觉 **96/pass**。19份生产源摘要与最终工作树一致，旧89张/4,171断言证据逐一校验未覆盖。复现脚本`run-botifier.ps1`，全局渲染范围另见`logs/botifier-display-scope-audit.json`。

OTA读回并重启后，默认20分钟周期启动待机采集49.266秒：50个idle快照全部blocked/Pet，未激活栏目；真实73%额度Fresh快照47个、3次安全额度包写入。距离496/498有效（2个启动无效样本保留），有5个Alert快照，说明栏目不展示期间测距与阈值判断仍运行；0畸形允许帧、0采集串口错误、0观察到的运行故障标记。证据`logs/device-botifier45-probe-report.json`；这是设备遥测，不是主菜单LCD录像。默认参数RAM恢复save=false，未改用户NVS。隐藏额度助手随后恢复，日志`logs/codex-weekly-botifier-helper.log`及`.err.log`。

本版未重新进行物理刷卡；上述抢占为实际生产界面配合合成服务输入的回归，seq33物理验收保留。长期运行与所有AI交互组合仍未全量覆盖。

## 红底距离提醒、守岸人休息动画与互斥播放（2026-11-02）

用户要求距离提示更显眼、护眼到点改成鸣潮角色二创动画，并选定守岸人；随后明确“动画这种优先级并列最高、互不打断”。该最新要求覆盖seq45的NFC→Alert→EyeRest顺序。现在NFC与EyeRest同属最高自动优先级，先开始的完整播放；EyeRest期间的新卡消费丢弃，不排队、不补播、不延长休息。NFC期间护眼到点只保留一个due，前台结束并空闲后开始。距离Alert排在两种动画之后，但持续独立采样；普通Countdown/Weekly/Pet仍立即让位给Alert。显式HOME/TUNE/OFF可以退出。

距离警告改为全屏不透明 **#ED1724** 鲜红背景、80px白图形与约36px白字，无频闪。只改自有decorator，不重设共享screen/头像颜色；原有底部控制与NFC片头仍位于其上。EyeRest改为蓝紫渐变、金色大号秒数、守岸人六帧挥手/眨眼、轻微浮动和星点；显示仅在BOTIFIER LIVE，原20分钟/20秒和12/3/12/3栏目节奏保留。EyeRest时控制层也使用不透明蓝紫背景，避免低优先级红底从底部透出，结束恢复透明。

纯策略区分前台阻挡与距离阻挡，持续Alert不再刷新idle活动时间；已播EyeRest忽略Alert和普通触摸。`try_begin_foreground_animation()`与worker tick同用state_mutex，原子决定先后，避免查询后同时启动的竞态；App先消费刷卡事件，再尝试claim。退出与关闭仍由context/set_config明确取消。

素材使用内置image_gen新生成、未提供图片输入。透明PNG及完整提示词/设计依据在`apps/common/idle_program/media/`，包含原图`shorekeeper-sheet.png`、`source.json`和可复现转换记录。实际pack **614,400字节、六160×160 BGRA帧**，SHA256 **2f5ce8498008a319933016834a631a60fcfa3852fad7f9007197a10f371484bf**；六帧SHA均不同，alpha数据验证和160px预览通过。`scripts/Prepare-Shorekeeper.py`只用现有FFmpeg和标准库裁格、缩放、打包，无新增依赖；原画生成非确定性，原图到pack转换可复现。官方开发笔记/角色展示仅为设计参考，没有将官方图或他人二创帧打包；此前误取到的幕后乐手封面未用于生成或固件。

源码 **0a5c81849c73005a9ab7407630d208bba5c7b878**。完整IDF5.5.4构建`logs/shorekeeper-build-first.log`通过，镜像 **4,653,296字节**、SHA256 **3c9a74fefe2da7bdf1b79de5716014ef2db21c978276b9758ad201dd7697c848**，原应用槽剩 **524,048字节**。匹配bin/ELF/map归档`.tools/firmware-artifacts/20261102-shorekeeper/`。像素与descriptor均静态flash生命周期，不新增解码器、后台任务或逐帧RGB分配；源码检查不等于实机帧率测量。

- CTest **12/12**（Policy **21组**、wire7组），MSVC C++17 `/W4 /WX`通过；日志`logs/shorekeeper-host-tests.log`。PC助手未改，原77/77证据保留。
- 实际完整AppDistance/View/Pet、真实LVGL/红底decorator与真实Policy共 **6,517断言**（2,409行为/4,108几何），0裁切/缺字；123独立帧中19个新场景。真实614,400字节资源通过host链接适配使用，实际frame_index执行，FreeRTOS/NVS锁层和硬件为替身。
- 红主导像素 **66,677/76,800 = 86.82%**，白标题437px/白图形1,014px，连续50帧稳定；NFC全帧像素与无警告时一致，解除恢复。EyeRest显示六帧及20/19/1/0秒，普通栏目样式恢复；真实Policy执行NFC播中到期→结束后护眼、护眼期间Alert/刷卡不中断/不延长，20,000ms结束同帧恢复红底。显式退出清层。
- 100轮含EyeRest/Countdown/Weekly/六帧切换，LVGL free **8,374,448→8,374,448**，15次图像释放/0借用中释放。manifest`.tools/idle-program-ui-probe/manifest-shorekeeper.json`、`frames-shorekeeper-v1/shorekeeper-contact.png`，视觉 **98/pass**；27份生产源/资源摘要与最终工作树匹配，旧seq45的104帧保留。旧Alert热读竞态负控制仍准确失败，生产源码通过。初次测试错误要求主体alpha必须255，实际抗锯齿主体为253/254；已按实测改为≥250并保留失败日志，不将测试阈值错误归为生产故障。

受保护更新已完成：`logs/device-update-shorekeeper47.log`校验新app及原厂固件、配置、bootloader、分区和资源保护区；匹配bin/ELF/map归档`.tools/device-backups/20261102-shorekeeper47/shorekeeper-deployed.*`。同端口40秒早期启动采集后，独立只读`ota-confirmation.json`确认 **seq47/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**。

本轮启动窗口仅证明启动与遥测：`logs/device-shorekeeper47-boot-report.json`记录375个距离快照，373有效、2个启动无效样本保留；38个idle快照全部blocked，未进入BOTIFIER播放；0观察到的panic/assert/watchdog标记。未发送短周期配置，仍为默认20分钟/20秒。PC额度助手已隐藏启动并恢复COM38连接，启动摘要为真实72%；记录`logs/codex-weekly-shorekeeper-helper.log`及`.err.log`，不将PC查询结果当作本轮设备显示验收。这些检查不替代新LCD动画帧率实测、物理NFC验证或长期运行测试；按用户此前要求不再安排重复限时确认。

## 三阶段护眼、实体摇头与不同栏目视觉（2026-11-02）

源码提交 **d927abe93a464229a82625512af2fd6948796c96**。20分钟到期后依次播放 **3.8秒守岸人3D短片 → 4.5秒半秒一拍的左右/上下动作与表情 → 完整REST20秒**。只有真实播放器完成或启动失败后，generation匹配确认才进入动作；REST由动作结束时的实际tick重新开始计时，不扣除前两段或调度延迟。整段EyeRest与NFC并列最高，互不打断，刷卡不排队；距离采样独立运行，红底显示排在两者之后。仅BOTIFIER LIVE渲染，显式HOME/TUNE/OFF取消自有流程。

3D素材取自[官方守岸人角色介绍](https://wutheringwaves.kurogames.com/en/main/news/detail/1419)中的技能GIF，裁出实际游戏3D画面，非静态图平移或设备端3D引擎。38帧、10fps、320×240（视频320×184，底部留黑），**324,546字节**，SHA256 **fa420d4e4620552d5a2fc3ee9306164fba24dc96adf698b3a24424a50ef6a7af**。媒体来源/原GIF摘要/裁剪和所有权在`media/shorekeeper-3d-source.json`；`scripts/Prepare-Shorekeeper3D.py`复用已有打包器，实测重建逐字节一致，默认拒绝覆盖/错误输入SHA亦通过。使用原JPEG单worker/两个weak RGB槽；停止链接旧614,400字节BGRA和删除其无调用方运行时代码，历史PNG/提示/pack保留。无新依赖。

实体动作以进入Motion时实际姿态为中心，左右±8°、俯仰±4°、speed300，并与实时动态硬限取交集；最后回到起始姿态。Intro只预占协作锁，让已发生idle移动自行完成；其它持锁或未完成动作不被抢占，改为跳过本轮实体动作。丢锁不夺；取消只stop自身已发起的运动。原bool协作锁缺少owner token的限制保留，不扩展全局运动架构。View独立pet按同一九拍变表情，不伪造ToF状态。Countdown采用暖琥珀钟环与居中MM:SS；Weekly采用冷蓝仪表盘、>_标识、百分比与十格额度条。

- CTest **13/13**，含Policy **25组**与Motion **14组**；两份真实内嵌pack也经生产C++解析器检查。日志`logs/eye-sequence-host-final.log`。PC未改，保留历史77/77，不冒充本轮重跑。
- 实际生产App/View/Pet/Policy与LVGL **6,950项断言**（1,975行为/4,975几何），138独立帧，0裁切/缺字；100轮生命周期覆盖Intro/Motion/Rest/Countdown/Weekly，可用内存8,374,448→8,374,448，16次图片释放/0借用中释放。保留旧红底、NFC、Safe→Alert、HOME/TUNE回归。新增旧完成计数误ack新busy片段、立即decoder失败、stop期间新卡时间晚于帧入口、旧Motion/Rest替换为新Intro的真实App回归，以及三阶段各HOME/TUNE/OFF共九个显式退出用例。四个隔离负控制精确失败：ack baseline后置、漏stop后时钟刷新、漏head代际重置、漏Alert热读。JPEG第一帧fixture来自真实pack；codec worker/FreeRTOS/舵机硬件仍为替身，不等同实机帧率/角度测试。视觉最终96/pass，第一轮88/revise保留。
- UI清单`.tools/idle-program-ui-probe/manifest-sequence.json`、最终视觉`visual-verdict-sequence-final.json`、31个新场景联系表`frames-sequence-final/sequence-contact.png`；22份生产源/资源摘要已与最终工作树逐一核对。复现`.tools/idle-program-ui-probe/run-sequence.ps1`；旧seq47证据不覆盖。
- 完整ESP-IDF5.5.4构建`logs/eye-sequence-build-final.log`通过。镜像 **4,366,576字节**，SHA256 **c2f17a86a168f76e191ef9de0e2e6654b71fd10dcce41bf16115371d29d75dbc**，应用槽余 **810,768字节（约16%）**。匹配bin/ELF/map归档`.tools/firmware-artifacts/20261102-eye-sequence/`。

受保护更新完成：`logs/device-update-eye-sequence49.log`校验仅app与OTA sector0更新，原厂固件槽、原sector1、配置、bootloader、分区表和资源均保留。归档`.tools/device-backups/20261102-eye-sequence49/eye-sequence-deployed.{bin,elf,map}`；40秒同端口启动采集后，独立`ota-confirmation.json`确认 **seq49/state2 VALID、CRC正确、sector1 seq2/state2逐字节保留**。读回日志`logs/device-eye-sequence49-ota-confirm.log`。

启动报告`logs/device-eye-sequence49-boot-report.json`：376个距离快照，374有效、2个启动无效样本保留；37/37栏目blocked，未发生EyeRest播放；0观察到的崩溃/断言/看门狗标记。未发短周期配置，初始eye_remaining_ms=1,200,000，默认20分钟保留。隐藏额度助手PID90060已恢复COM38连接，安全摘要71%（epoch1793599642），日志`logs/codex-weekly-eye-sequence-helper.log`及`.err.log`，无OS自启动；PC查询不替代设备显示验收。按用户此前要求不再安排重复限时确认。新LCD播放帧率、机械角度/节奏、物理NFC与长期运行未以主机结果替代。

## 全屏菜单与四组慢拍：seq51实机失败记录（2026-11-02）

用户要求播放与节目去掉常驻HOME、顶部下滑或底部上滑拉出菜单；指出未见机头动作，并将节奏明确为四组“两拍、回中”。源码`70c51f98134d18b62e2b513a4ce7b8b027d25823`改为12×1000ms、完整REST20仍独立计时；每个新拍开启扭矩。独立sys抽屉不改共享层属性，单纯开关不打断动画；HOME/设置导航在LVGL锁外停止worker。原底部控件删除，守岸人38帧均为320×240满幅，无56px留黑。

seq51镜像 **4,467,840字节**、SHA256 **9022711f46f4fb93de7d2fca4e6e9eb1466c100250e5dc00350894ef2ed36302**，应用槽余 **709,504字节**。IDF日志`logs/fullscreen-build-final.log`；受保护更新与独立读回均完成，归档`.tools/device-backups/20261102-botifier-fullscreen51/`，元数据seq51/state2 VALID、CRC正确、原sector1逐字节保留。早期启动376距离/374有效/2启动无效，37个idle全部blocked、0运行故障标记；不能用该窗口声称运动通过。

CTest **14/14**，实际LVGL **176帧、8,105断言**（5,638几何、2,467行为）、0裁切/缺字、两组各100轮生命周期LVGL增量0。五个隔离负控均准确失败；新增SAVE底边手势的PRESS_LOST负控真实复现按钮按下状态残留。清单`.tools/idle-program-ui-probe/manifest-fullscreen-presslost.json`、视觉95/pass；实际硬件和codec worker不在这些桌面证据范围内。

用户进入LIVE后采集`logs/device-fullscreen51-motion.ndjson`及原始report：同代12拍、flags5（已开始且持锁）、扭矩寄存器明确0→1、位置与扭矩写ACK1/设备状态0，但新鲜位置 **yaw486–487、pitch601**，即左右仅1raw抖动、上下无位移。实机运动判定为 **失败**，不是“代码目标已发即摇头成功”。原report的`attention_required=false`漏计跨度不足，后续修正探针并保留原报告；`physical_motion_verified=false`原判定正确。

同一日志11.294秒两轴各只增加30次位置写；这不直接等同App帧率，也不能确定阻塞来源。独立实验编译原UITK Animate/Spring源码：speed300的8°目标，2/3/4次固定20ms更新仅推进0.3485°/0.7055°/1.1301°；4°目标仅其一半，50次才接近完整目标。生产Servo每次固定推进20ms、每个新拍又从真实姿态重置起点，解释了低写入频率下运动不足。证据`.tools/servo-cadence-probe/probe.cpp`及`results.txt`；是否还有硬件死区未读寄存器，不作定论。此结论覆盖此前仅补扭矩就可能解决的推测。

探针自身另有恢复竞态：1秒测试周期在主机看到completed前再次到期；生产Policy按原设计保留pending due，恢复20分钟包写成功但观察未恢复。没有为测试修改Policy。后续探针改10秒，及时恢复且分别记录写成功与实际正常计时。seq51已用save=false暂停RAM节目，未写NVS；暂停观察completed_rests2且eye_remaining1,055,669ms，额外pending轮完成后已按正常间隔走时，见`logs/device-fullscreen51-probe-pause.json`。

## seq53：全屏、四组慢拍及实体位移通过（2026-11-02）

最终源码 **735a2583725e78c67967f318f49b3c1555e3f7a5**，镜像 **4,468,608字节**，SHA256 **9b79758d78e29a060adfde874d160d4ce9acf35ec12398ee580f9eae132d0c9b**，原应用槽余 **708,736字节**。`logs/fullscreen-timed-build-final.log`完整构建通过，`logs/fullscreen-timed-host-final.log`记录 **15/15**；控制器20组、真实Servo/Motion/UITK11组。新增测试只替换硬件和墙钟，直接编译生产运动代码；验证低频更新、旧spring不覆盖、取消立即写当前角度、限幅/堵转hook及回绕。实际LVGL仍为 **8,105断言/176帧/五项负控**，176帧与全屏前版逐字节一致；清单`.tools/idle-program-ui-probe/manifest-fullscreen-timed-head.json`，视觉95/pass。

护眼专用`moveWithDuration(...,350)`立即向SCS发完整目标和时长，不更改全局spring步长。timed期间继续原堵转检查，正常move/stop立即发当前角度20ms覆盖旧硬件目标；不依赖退出后的下一次App更新。初始姿态贴限位时在原实际姿态±8°/±4°与实时硬限的交集内选中点、缩小幅度，保留两方向活动空间，不扩大原外包络。菜单、播放器和普通栏目沿用seq51已验证全屏版本。

受保护更新日志`logs/device-update-fullscreen53.log`，同端口40秒启动捕获后，独立只读`ota-confirmation.json`确认 **seq53/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**。原厂槽、配置、bootloader、分区与资源保护区校验通过。归档`.tools/device-backups/20261102-botifier-fullscreen53/botifier-fullscreen-deployed.{bin,elf,map}`。新启动376距离/374有效/2启动无效、37个idle全部blocked、0运行故障标记；初始护眼1,200,000ms。

用户再次进入LIVE后，`logs/device-fullscreen53-motion.ndjson`实际采到同一generation的 **0–11全部12拍**，flags始终5；两轴每拍均为硬件350ms，ACK1、设备状态/独立错误均0。新鲜位置读取计数各为3→14：**yaw464–505，跨度41raw≈12.8125°；pitch599–610，跨度11raw≈3.4375°**。这证明实际编码器位置发生明显变化，不用目标或写入次数冒充实体运动，也不把编码器换算当作外部姿态标定。

从设备快照反推Motion开始至Rest开始 **12,032ms**；Rest入口至退出日志 **20,016ms**（含退出处理延迟），completed_rests从0→1。全窗口847/847距离有效、84个idle快照，0畸形允许帧、0故障标记。一次性探针恢复包写成功，随后实际读回 **eye_remaining_ms=1,199,440**，正常20分钟计时恢复，未SAVE。自动报告`logs/device-fullscreen53-motion-report.json`与根代理独立原始帧核对`logs/device-fullscreen53-independent-audit.json`均通过，物理位移判定true。

额度助手已恢复为隐藏进程 **PID146112**，COM38连接标记和真实PC摘要 **67%** 已核对；安全日志`logs/codex-weekly-fullscreen-helper.log`及`.err.log`，运行记录`.tools/fullscreen-helper-runtime.json`，无系统自启动。PC摘要不替代本轮机器人额度画面验收。未重测物理NFC、LCD实际帧率/物理手势灵敏度、外部姿态精度或长期运行；此前RF瞬态错误与协作bool锁无owner token的限制保留。

## seq57：护眼动作连续过渡，实机流畅度待验证（2026-11-02）

用户明确问题是护眼摇头时表情与动作不连贯。seq53的位移和时长通过记录保留为历史证据，不代表流畅度通过。先部署仅增加被动计时的seq55（4,470,464字节，SHA256`11eca027c9e664c185e5258c8448f7c5d8ebe6922be5396e31dc3d557f80a912`），独立读回VALID；两次175秒采集均未进入LIVE、未发送短周期配置、没有SC_FRAME，报告`logs/device-motion-baseline55-report.json`及`device-motion-baseline55-retry-report.json`。没有形成有效实机性能基线。

最终源码 **fdf44df81bfd0990ef96f3c91492c0e76a74b112**：仅Motion脸部位置由每秒直接跳位改为350ms连续过渡后保持650ms，沿用原情绪混合与12秒四组动作；两种SCS读取函数移除缓冲字节间多余调度，保留无数据让出CPU与超时/错误处理。`SC_FRAME`输出在显示锁外，统计内容提交而非LCD帧率。没有新增依赖或修改普通宠物/栏目节奏。

`logs/motion-flow-host-final.log`记录CTest **17/17**，正式串口回归旧实现先失败、修改后通过；源码提取协议/调度模型63组比较通过。实际生产App/Pet/LVGL **8,105断言、五项负控**通过、0裁切/缺字，两组各100轮生命周期内存增量0；同样91帧/33ms采样最大相邻容器位移 **8→1px**，视觉95/pass。证据`.tools/motion-flow-probe/motion56-result.json`、`after/visual-verdict.json`和`logs/scserial-read-source-probe.json`。motion56仅是桌面迭代名称；没有OTA56。桌面时钟和硬件替身不能证明实机帧率或机械流畅度。

完整IDF5.5.4构建`logs/motion-flow-build-final.log`通过。镜像 **4,470,704字节**、SHA256 **916e1c39b5469f15b11c979b79304d59242d91fce67950aeb1b96b19dd85364b**，应用槽余 **706,640字节**。匹配bin/ELF/map归档`.tools/device-backups/20261102-motion-flow57/motion-flow-deployed.*`。受保护更新`logs/device-update-motion-flow57.log`和独立`ota-confirmation.json`确认 **seq57/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**；NVS、bootloader、分区表、原厂槽和资源保护区校验通过。

40秒启动报告`logs/device-motion-flow57-boot-report.json`：37个idle全部blocked、0运行故障标记，初始eye_remaining_ms=1,199,998，正常20分钟计时。376个距离样本均无效；seq55采集也有无效读数，本次没有检查传感器摆放或原因，不据此认定固件回归，也不宣称当前测距通过。随后175秒窗口`logs/device-motion-flow57-report.json`仍未进入LIVE，173个idle全部blocked，没有SC_HEAD/SC_FRAME、未发送测试配置、0故障标记。当前仅确认启动与软件渲染路径，实机流畅度没有测得；无需将“未发送配置”的restore=false解读为恢复失败。

默认20分钟/REST20保留，未SAVE。隐藏额度助手已恢复 **PID1528**，COM38连接标记与新鲜安全PC摘要 **66%** 已核验，日志`logs/codex-weekly-motion-flow-helper.log`及`.err.log`，记录`.tools/motion-flow-helper-runtime.json`；PC摘要不是机器人显示验收，无系统自启动。本轮未重测物理NFC、LCD帧率、外部姿态或长期稳定性。

## seq59：两次往返、预备眼神与暂停（2026-11-02）

用户确认“一回中一，短暂停；二回中二……”指左/右/上/下每方向重复两次，中间回中，并要求动画曲线。源码 **803f622a195111442e6b53f2fb40bc8a773a3f80** 将等长12拍替换为16个明确目标：每组三秒内在120/720/1400/2100ms发方向/中/方向/中，时长360/300/420/340ms，结尾留560ms。眼神提前120ms，眼神与脸部位移用smoothstep；每组暂停眨眼并保持该组情绪。原限幅/堵转/取消与动画互斥保留，总Motion12秒后仍开始完整REST20。具体时间是设计选择，研究来源及实现说明见`DEVELOPMENT.md`，未宣称是本设备最优值或物理舵机cubic曲线。

`logs/expressive-motion-host-final3.log`：CTest **17/17**，宠物18组、Policy27组、机头21组、生产Servo/Motion/UITK12组；额外四分之一/中点回归能区分线性位移与缓入缓出。真实App/Pet/LVGL **8,441断言**（5,926几何、2,515行为）、180帧、五项隔离负控通过，0裁切/缺字，19次图片释放/0借用错误，两组各100次生命周期堆增量0。156/160非Motion帧逐像素同seq57；4张紧跟Motion的Weekly图仅100×75私有宠物区域有差异，文字/额度条/时间/布局相同。状态历史是可能原因，未独立隔离，不冒称160张全同。

完整12秒影片为300×40ms帧，另有60个精确采样点。第一轮因Surprise在第二摆前到期、末尾随机Sad，视觉88/revise；已通过仅Motion的`hold_mood`保持当前组情绪，原280ms混合与细小动作继续。第二轮实际Controller逐帧情绪300/300匹配四组，视觉94/pass，Happy弧眼的预备位移比睁眼组含蓄。失败v1/旧seq57完整保留，最终`.tools/expressive-motion-probe/manifest-final.json`及`movie-v2/visual-verdict.json`；16份源码摘要与当前树逐项核验，GIF为`movie-v2/expressive-12s.gif`。这是桌面渲染，不是LCD实拍或机械曲线测量。

完整IDF5.5.4构建`logs/expressive-motion-build-final2.log`通过。镜像 **4,471,984字节**、SHA256 **b6194d2de226099987d8acc2063b49bc7a6dd04e2051c71c6c0ae0205a90f55a**，应用槽余 **705,360字节**。匹配bin/ELF/map归档`.tools/device-backups/20261102-expressive-motion59/expressive-motion-deployed.*`。`logs/device-update-expressive59.log`确认保护区校验；独立`ota-confirmation.json`确认 **seq59/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**。NVS、bootloader、分区表、原厂槽与资源均保留，未引入依赖。

启动报告`logs/device-expressive59-boot-report.json`：37个idle全blocked，375个距离样本无效，0运行故障标记；初始eye_remaining_ms=1,199,998。测距无效在seq55/57已观察到，本次没有检查摆放或原因，不据此认定新编排回归，也不宣称当前测距验收通过。实机新动作另使用独立探针与seq59合同，禁止套用旧12拍通过记录；探针10项合成校验和脚本范围见`.tools/expressive-motion-probe-tests/delivery.json`。探针元数据`expected_sequence59`只是合同标签，OTA身份以上述独立flash读回为准。

## seq59实体方向失败与seq63修正（2026-11-02）

用户明确反馈“实体机头都朝同一侧”。seq59原探针以跨度/目标包判通过，未核对校准正面的两侧，判定范围过宽，已撤回其方向通过含义并保留原报告。实际校准yaw raw458、起点raw499，旧逻辑以起点为中心，使全部左右目标仍为正角度+4.8°到+20.8°；后续新鲜位置raw477–518也均大于458。失败独立审计`logs/device-expressive59-direction-failure-audit.json`。该轮45次内容提交/11,928ms≈3.77次每秒，最大间隔354ms，显示锁等待9,733ms/最大272ms；这不是LCD帧率。

最终源码 **04d7d53dc903c14cc297907cb757256558fe4566**（含方向修正`1398ba0`）：yaw改以校准正面0为中心，先700ms归正，再做四组各两次往返，总12.7秒/17目标。初始偏转>30°、限位不能跨零、他人占用或首tick晚于120ms时跳过实体动作；不抢锁、不省略/截断归正。俯仰仍受原实际姿态±4°可行窗口限制，不调用双轴goHome。Motion脸改native256，删除每帧普通208→Motion256的重复布局；原厂主页面、AI、全局运动步长、SPI、LVGL缓冲和NFC优先级不改。涉及文件为`eye_sequence.h`、`rest_head_motion.h`、`view.cpp`及对应三份主机测试；此前seq59的EyeCue/组情绪实现保留。

- `logs/centered-motion63-host-final.log`：CTest **17/17**，Policy28组、机头24组、真实Servo/Motion/UITK13组。首次120/121ms、稀疏更新、动态限位、取消与他人运动均覆盖。
- 实际App/Pet/LVGL **8,548断言、五项隔离负控**；新增 **30条真实App/Policy迟到保护断言**与旧控制器负控，181帧同seq61逐像素一致，两组100次生命周期堆增量0、19次图片释放/0借用中释放。最终overlay`.tools/centered-motion-guard63-probe/guard-verification-overlay63.json`记录16个最终生产输入摘要；基准清单`.tools/centered-motion-probe/manifest-final.json`。完整318帧/12.7秒影片、64精确点、视觉94/pass保留在`movie-v2/`。背景层有意小幅平移并裁切，五官/文字不越界，不能泛称所有层均无裁切。
- 完整ESP-IDF5.5.4构建`logs/centered-motion63-build-final.log`通过。镜像 **4,472,128字节**，SHA256 **88a97c8a47789249ea19fc78a1e7782862ebd5a95d36dd8c4bac9c16a781db4e**，应用槽余 **705,216字节**。匹配bin/ELF/map归档`.tools/device-backups/20261102-centered-motion63/centered-motion-deployed.*`；不可变构建清单位于`.tools/firmware-artifacts/20261102-centered-motion63/manifest.json`。

中间版seq61已独立读回VALID，未进行实体动作验收；review发现迟到首次tick可能省略/截断归正，修复后最终部署63。`logs/device-update-centered63.log`与独立`ota-confirmation.json`确认 **seq63/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**。仅更新app与metadata sector0；NVS、bootloader、分区表、原厂槽与资源保护校验通过。第一次63预检因引用不存在的旧bin路径失败于打开串口之前，保留`device-update-centered63-preflight.log`；随后改用已核验不可变61构建镜像，未绕过任何保护。

新启动报告`logs/device-centered63-boot-report.json`：376个距离样本中374有效、2个启动无效；37个idle均blocked、0故障标记，初始正常20分钟。校准仍为yaw458/pitch591，没有改动标定值。

用户进入LIVE后，`logs/device-centered63.ndjson`采到同代 **0–16全部17目标**，硬件时长700与360/300/420/340ms正确，flags5、两轴ACK1/状态0、位置读取计数递增。目标后的新鲜反馈为：左两次raw **445、437**，右两次 **472、478**，相对校准458确实位于两侧。用户再次明确确认 **“左右都正常了”**，与根代理独立原始帧审计`logs/device-centered63-independent-audit.json`一致。

**严格归中精度仍未通过。** 回中反馈raw453/462，相对458残余−5/+4raw，最大约1.5625°；原先±3raw验收阈值没有放宽。原严格探针的`physical_direction_verified/physical_motion_verified=false`完整保留，不改写为全通过；独立审计明确分开“两侧各两次=true”和“严格归中=false”。此项不是四次同侧问题复现，没有为通过测试擅自改舵机标定、PID或扩大运动幅度。

本轮内容提交 **118次/12,646ms≈9.33次每秒**，最大间隔 **222ms**；显示锁等待最大 **140ms**。相对seq59的3.77次/秒、354ms/272ms有改善，但显示锁总等待仍9,553ms；这是一组组合修改比较，不能把收益全部归因于某一处，也不能当LCD实际FPS或主观流畅度已通过。实际Motion约 **12,755ms**；Rest入口至退出日志 **20,033ms**，completed_rests0→1。测试恢复包写成功，实际观察到 **eye_remaining_ms=1,199,323**，20分钟恢复；全程save=false，没有写保存参数。严格63探针本身11/11合成用例通过，能拒绝旧16目标、单侧位移、缓存位置、错时长与晚启动skip冒充正常动作，证据`.tools/centered-motion63-probe-tests/delivery.json`。

隐藏额度助手已恢复 **PID28412**，COM38连接标记及新鲜安全PC摘要 **64%** 已核验；日志`logs/codex-weekly-centered-helper.log`与`.err.log`，运行记录`.tools/centered-helper-runtime.json`，无系统自启动。该摘要不替代设备额度页面验收。剩余限制为上述归中误差、LCD实际帧率/舵机物理缓动曲线及长期运行；物理NFC/触摸未在本次重复验收。无新增依赖、无远程推送。

## 机器人独立联网额度：seq65故障与seq67修复（2026-11-03）

用户明确要求额度查询脱离电脑运行；此前USB同步是实现选择，不是硬件限制。本轮加入设备自己发起的设备码登录、原子保存刷新凭据、直接HTTPS读取额度和5分钟轮询，不读取/复制电脑登录文件。两个旧助手PID28412、53440均已停止，记录`.tools/standalone-weekly-stopped-helpers.json`；不能恢复它们来替代独立查询验收。源码主要为`apps/common/codex_weekly/`、AppDistance的CODEX入口、idle策略发布接口和启动接入，未增加第三方依赖。

- 最终主机CTest **19/19**，协议 **17组合成回归**，包含刷新响应缺access、旧access过期与重启access为空时仍保存新refresh。日志`logs/standalone-weekly-host-dns67.log`。真实App/LVGL新页 **748断言**、既有回归 **8598断言/五项负控**、207截图、三组100次生命周期无堆增量；`.tools/standalone-weekly-ui-probe/manifest-final.json`，视觉95/pass。桌面服务替身不构成真实TLS、登录或凭据恢复证据。
- seq65镜像 **4,517,696字节**、SHA256 **7cffc4b69e4f506c6c491bc8e60d97ce2e99efaeefd94f3d15a11426e9afe581**；受保护更新与独立读回均确认 **65/state2 VALID**，原sector1完整保留。归档`.tools/firmware-artifacts/20261103-native-weekly65/`，部署与读回`.tools/device-backups/20261103-native-weekly65/`。设备启动后Wi-Fi已连接、signed_out、无Storage错误。
- seq65实机登录失败，用户重复确认Network error。`logs/device-native-weekly65-sdk01.ndjson`捕获DNS `getaddrinfo`错误 **202/EAI_FAIL**，随后连接失败、HTTP状态0；不能宣称已取得设备端额度。电脑UDP交叉测试网关<ROBOT_HOST>:53立即拒绝，1.1.1.1/8.8.8.8/223.5.5.5返回有效回答。设备实际DNS错误RCODE未观测，不将该电脑实验冒充设备DNS报文。
- seq65局域网诊断实际验证缺KEY/错误KEY均 **401**，带`no-store/nosniff`，POST为 **405**，未泄漏状态；`logs/device-native-weekly65-http-auth-negative.json`。使用正确KEY的GET与拔除USB后的查询仍待真实验收。
- seq67修复仅补空BACKUP=1.1.1.1、空FALLBACK=8.8.8.8，保留MAIN及已有服务器。只填FALLBACK的初始候选因lwIP负响应分支不跳空槽被源码复核否决，记录`logs/codex-service-dns-http67-review.json`。DNS槽在当前SDK全局共享；NOERROR无可用记录不会切DNS，此限制保留。响应回调预算从连接成功后重置10秒，不计入DNS/TLS已耗时间，仍非整笔硬超时。
- seq67最终ESP-IDF5.5.4构建通过，镜像 **4,519,360字节**、SHA256 **9d27edeb198f5e1182ea4355fa208cc88d4f76394af469e7fefb5620b201860b**，应用槽余 **657,984字节**。归档`.tools/firmware-artifacts/20261103-native-weekly67/`含匹配bin/ELF/map和逐文件源摘要，构建`logs/standalone-weekly-build-dns67-final.log`。
- seq67受保护安装完成，独立读回 **67/state2 VALID、CRC正确、sector1 seq2/state2逐字节保留**；`.tools/device-backups/20261103-native-weekly67/ota-confirmation.json`。启动报告`logs/device-native-weekly67-early-report.json`含37个额度状态（联网后signed_out/error-none）、38个距离状态、0观察到的panic/assert/watchdog标记。实际DNS读回为MAIN0.0.0.0/BACKUP1.1.1.1/FALLBACK8.8.8.8。当前SDK启用ESP_DNS，遍历所有槽并跳过空MAIN；不能因MAIN为空再推断解析必定失败或擅自覆盖主DNS。
- 安装后用户确认已经出现 **Login Code**，安全遥测也进入`awaiting_login/login_pending=true/error=none`，说明有效设备码申请已通过。初始申请响应发生在两段采集间隙，未虚报捕获到具体HTTP200；源码只有合法2xx响应且解析成功才进入该状态，后续可见官方约定的待授权轮询403。已指导用户在手机打开`auth.openai.com/codex/device`登录自己的ChatGPT账号、输入机器人码并确认。真实额度GET、递增observed_at的刷新、重启恢复凭据、拔除USB后刷新均 **待手机授权后验证**，不能将设备码成功当作额度查询已通过。

新增`SC_CODEX_HTTP`只记录固定端点枚举/数字错误码，`SC_CODEX_DNS`只记录三个服务器地址；登录码、KEY、账号标识及令牌均不进入诊断日志。只读探针 **25/25合成测试**，含严格schema、敏感额外字段拒绝、DNS文本丢弃及FakeSerial零写入；`.tools/native-weekly-probe-tests/`。首次启动当前测距状态为`sensor_missing`，不将历史ToF验收或主机测试当作本轮有效采样；没有为本次额度问题改测距阈值、硬件接线或动作控制。

## seq69：授权后额度超时修复与实际独立查询通过（2026-11-03）

用户完成设备授权后仍报Network error。首先更正诊断误判：初版探针只接受0–599，把SDK合法的无响应状态`-1`丢成malformed，曾据此判断额度任务没有输出；该判断不成立。`logs/device-native-weekly67-authorized01-report.json`保留60个malformed记录，后续鉴权Wi-Fi GET确认实际`signed_in=true/error=network/http_status=-1`。探针仅放宽这一合法值，仍拒`-2`，成功仍要求HTTP200；增加精确SDK头缓冲错误字面量白名单后，共 **30项合成测试通过**。纠错记录`.tools/native-weekly-probe-tests/status-minus-one-correction.json`。

seq67重启实测`refresh`返回 **HTTP200/3588ms**，随后`usage`返回 **ESP_ERR_HTTP_EAGAIN 28679、约12999ms、无响应头**，证据`logs/device-native-weekly67-authorized-reboot01.ndjson`。因此授权及保存后的刷新已成功，不能再次让用户重做登录或把问题一概归为Wi-Fi未连。SDK默认TX512、单头不能拆分；真实源码生成器实验用假access1800/account36复现旧512只发送 **83/1967字节且无最终CRLF**，生产2348发送完整；假8192/256时旧路径83/8579，生产8960完整。四项失败负控、当前与更宽历史account边界、无Bearer GET/POST，共 **10/10通过**；SDK源未修改，生产赋值从实际`service.cpp`提取编译，发送循环为捕获模型。证据`.tools/http-header-budget-probe/{manifest,results}.json`、`logs/http-header-budget-probe-20261103-115416-run.log`。

生产修复仅三行：有Bearer的usage按`access.size()+account.size()+512`设置TX，当前最大8960字节，已有账户512宽界最多9216；保留所有OAuth POST的默认TX、RX、TLS验证、超时和32KiB响应上限。完整主机 **CTest19/19**，`logs/standalone-weekly-host-http69.log`；完整IDF5.5.4构建`logs/standalone-weekly-build-http69.log`通过。镜像 **4,519,360字节**、SHA256 **41caa0925c841324b312bb09422fcdca8021bb71f3482c53f56de7e4910a98a8**，槽余 **657,984字节**，匹配bin/ELF/map和全部源摘要归档`.tools/firmware-artifacts/20261103-native-weekly69/`。

受保护更新`logs/device-update-native-weekly69.log`保留已有登录及设置、bootloader、分区表、原厂槽、资源和sector1，仅写app及sector0。独立读回确认 **seq69/state2 VALID、CRC正确、原sector1 seq2/state2逐字节保留**，`.tools/device-backups/20261103-native-weekly69/ota-confirmation.json`。NVS保存区在应用启动前核对；运行中的正常凭据轮换会写NVS，不能宣称重启后NVS仍逐字节不变。

新固件首次启动实际`READY/HTTP200`、真实周余量 **62%**，observed_at **1793678208**。独立读回并重启后，不重新登录即恢复：`refresh` **200/3704ms**、`usage` **200/4436ms**，再次 **62%**、observed_at **1793678263**。`logs/device-native-weekly69-reboot01-report.json`记录52个READY快照、0malformed/故障标记；idle额度 **51/51一致**。`.tools/read_native_weekly_status.py`通过实际Wi-Fi鉴权GET也取得相同结果，KEY仅从既有设备备份定向提取并校验CRC、不输出或落入日志；未提取账号凭据，未调用PC额度查询。

USB额度助手始终停止，新固件忽略其旧额度帧。当前已关闭所有串口采集，仅用只读Wi-Fi观察下一次默认5分钟轮询，定时刷新验收另记；USB线仍用于供电，未将关闭串口混称物理拔线测试。原UI真实LVGL验证仍适用，此次三行传输修复未改界面；实际LCD操作、长期网络故障恢复、NVS突然断电窗口等未拿主机或一次成功代替。

**默认定时刷新也已通过。** 关闭所有USB采集/助手后，仅用鉴权只读LAN GET观察；`logs/device-native-weekly69-lan-poll03.json`记录`source=device_wifi`、READY/HTTP200/62%，observed_at **1793678263→1793678567（递增304秒）**，last_success_ms **15102→318258**，下一轮约270秒。没有串口读写、没有手机重新登录、没有调用或推送PC额度；只读状态GET不会触发刷新，因而证明后台自行完成了新一轮查询，而非重复展示旧62%。前三段启动/重启/轮询通过，物理拔线及长期运行仍未声称覆盖。最终环境记录与原始数据一致，源摘要再次匹配归档镜像。


## Wi-Fi OTA：双向无线升级与seq75最终读回通过（2026-11-03）

用户要求通过Wi-Fi更新功能。新增本地鉴权 `GET/POST /api/firmware`，复用原服务器/设备KEY/IDF OTA与SHA，无新增依赖。PowerShell/Python工具 `scripts/Update-FirmwareWifi.*` 校验本地完整ESP32-S3 app，再核对MAC/工程/当前ELF/VALID状态；上传完成不等于成功，必须目标slot、目标ELF及VALID全部成立。固件更新会短暂重启，不是C++运行时热替换。

先受保护USB安装seq71无线入口，独立读回71/VALID、原sector1逐字节保留。实机6项preflight通过：无KEY的GET/POST401、错误MAC/ELF409、超大长度413、错误镜像prefix400；检查前后slot/ELF/VALID一致。见 `logs/device-wifi-ota71-preflight.json`。初次探针用完整无效body时遇到拒绝后的RST；改成header-only又触发SDK解析408；最终根据真实on_body分派行为使用最小首字节，两次错误假设和原始失败日志均保留，未计为通过。

初版两次Wi-Fi传输未确认成功，客户端明确失败，日志 `logs/device-wifi-ota72-wireless-report.json`、`logs/device-wifi-ota-retry02-report.json`。第二次只发送131072字节时，抓到仍在旧ota_0运行的headtouch栈哨兵失败并主动abort；先前“回到原槽”不能证明新槽启动回滚，现更正并保留 `logs/device-wifi-ota-retry02-boot.log`。只改头触任务同一6144字节栈的分配位置SPIRAM→INTERNAL|8BIT，优先级、周期和手势完全不变。SDK正常guard会停两核调度，因此具体哨兵损坏机制仍待验证，不把外置栈与cache的相关性写成已证根因。

完整IDF5.5.4构建 `logs/wifi-ota-build73-internal-touch.log` 通过；最终镜像 **4,527,152字节**，SHA256 **c5c1aa7a3b69bf8fb514fafd0a2d528d944d5bb48d9b7007d15afc903870a587**，ELF SHA **e818f7ae4f8fb282c0c16b16b5b08ec756661855162a74b21f6b4d6b312dd7a2**，原槽剩 **650,192字节**。bin/ELF/map/逐源摘要在 `.tools/firmware-artifacts/20261103-wifi-ota73/`；71→73摘要对比仅 `hal_head_touch.cpp` 不同，见 `logs/wifi-ota-stack-experiment.json`。受保护修复安装73，保留当时NVS、bootloader、分区、非目标应用及资源；原厂完整镜像仍在磁盘，已不将被前次OTA擦写的备用槽视为原厂。73启动日志明确出现headtouch任务启动，排除了静默创建失败。

关闭所有串口后，两次完整镜像均纯Wi-Fi上传并通过LAN确认：

- **ota_0→ota_1：47.500秒**，4527152字节，POST200且回执摘要匹配；观察到PENDING_VERIFY→VALID。`logs/device-wifi-ota74-wireless-report.json`。
- **ota_1→ota_0：52.641秒**，同样完整传输、POST200、目标slot/ELF/VALID一致。`logs/device-wifi-ota75-wireless-report.json`。

两次都保留本机授权、自动refresh/usage成功，Wi-Fi只读查询为 **61%**；没有读取或推送电脑Codex额度，没有重启旧USB助手。`logs/device-wifi-ota74-status.json`、`logs/device-wifi-ota75-status-before-readback.json`。这两次是同镜像重复部署，验证双槽传输/启动和同ELF不假成功；不是两份不同业务版本的兼容测试。

无线升级和LAN验收完成后，另用USB做独立只读交叉校验：**sector0 seq75/state2、sector1 seq74/state2，两者CRC正确**；两个应用槽各4527152字节均与归档镜像MD5匹配。bootloader/分区表与0xA00000之后6MiB资源/尾部摘要均与原备份相同。没有flash写入或擦除，也没有读取NVS凭据；记录 `.tools/device-backups/20261103-wifi-ota75/ota-confirmation.json` 和 `logs/device-wifi-ota75-verify.log`。随后正常重启的观察另记日志，不将这一步USB交叉校验混称全程无USB。

Host **CTest20/20**（真实handler11组，SHA/flash平台fake），PC **95/95**（含18项上传工具测试），独立same-image/丢回执模型4项通过。日志 `logs/wifi-ota-host71.log`、`logs/wifi-ota-python-final.log`；实际加密/flash路径由上述两次硬件结果补证。界面未改，不重复以桌面渲染冒充LCD实拍。新增代码未commit/push。

当前两槽用于开发版A/B，原厂应用不再常驻ota_1，完整原厂备份保留 `.tools/device-backups/20261030-124710/flash-original.bin` 并重新核对SHA256。未重分区、未刷资源、未清空用户设置。实机断电回滚、长期持续多轮升级、不同业务版本间NVS迁移及手动触摸/NFC/实体动作回归尚未覆盖；不能以正常双向更新成功替代这些项目。

最终独立读回后的正常重启也已通过：`logs/device-wifi-ota75-readback-reboot.log` 再次出现headtouch任务启动，refresh200/3533ms、usage200/3374ms，原授权未丢；随后仅Wi-Fi读取 `logs/device-wifi-ota75-final-status.json` 为READY/HTTP200/61%，observed_at1793681440。所有串口采集已关闭。

## 摸头互动与电量菜单：新业务镜像无线安装通过（2026-11-03）

新增 BOTIFIER LIVE 约3秒摸头回应及短间隔亲昵变体，眼神先行、机头曲线跟随；菜单恢复电量和充电标记。复用既有动作、表情与电池缓存，无新增依赖。实现说明见 DEVELOPMENT.md 的“BOTIFIER 头顶触摸与电量菜单”。Host CTest **22/22**、真实App/LVGL硬件替身验证 **1897+9334项**、visual-verdict **96/pass**；完整IDF构建 `logs/firmware-20261103-161627.log` 通过。桌面验证不替代实体流畅度、电池精度或LCD帧率验收。

镜像 **4,532,656字节**，SHA256 `4e24ba7785f006c598cda6d3cdbb0fb26bb11e8d1ec1cff7016fd2ecf3b362a5`，ELF SHA256 `86b8fa6f668c9a3e6e76cda7f3a850b0c02e61f7e7d625af1d19301825e2e1a7`，应用槽剩余 **644,688字节**。bin/ELF/map和逐源摘要归档于 `.tools/firmware-artifacts/20261103-touch-pet76/`；76仅为批次名称，本轮没有USB读取实际OTA序号，不将其写成已验证seq76。

首次在LIVE上传未通过：`logs/device-touch-pet76-wireless-report.json` 显示客户端发送进度131072字节后中断、没有接收确认，后续仍是旧ota_0/ELF。该进度不能证明flash实际写入字节数，也不能证明新槽启动回滚。用户回到HOME后，同一镜像完整上传 **52.499秒**，POST200；LAN独立确认新ELF、运行/启动均 **ota_1**、状态 **2/VALID**，报告 `logs/device-touch-pet76-home-retry02-report.json`。全程无线，无串口读写；保留原登录和设置。LIVE故障原因未闭环，不能套用历史canary结论或宣称已修复，当前升级流程为底座供电、HOME后上传。

用户原先仅靠内置电池时屏幕闪烁并关机，发生在此次镜像安装之前；接底座电源后用户确认屏幕稳定。升级后实际电量51%、随后55%→58%，均显示charging；`logs/device-touch-pet76-observe.json` 的7次只读快照中uptime **171227→232038ms** 连续增长、ToF有效、独立Wi-Fi额度READY/HTTP200/57%。最新启动的reset_reason为 **7（ESP_RST_WDT）**，按固定SDK枚举记录，不能描述成普通软件复位；它不能追溯此前电池闪烁或LIVE上传故障的原因。未修改省电、电压或背光策略，也不以充电后电量推断故障时电量。

用户实测反馈“有反应，但是被too close抢占了”，证明触摸回应出现，但完整实体动作和主观流畅度尚未验收。该批次源码由距离Alert取消触摸控制器；伸手遮挡ToF只是待验证解释，不能凭反馈确定。后续用户明确的新优先级及其修正记录如下。未commit/push。

## 互动高于过近提醒：三处局部修正与无线安装（2026-11-03）

用户明确“too close属于闲时任务中的略高优先级，互动和动作外显一定是更高优先级”。新优先级为受保护NFC/完整护眼沿用原仲裁，摸头互动高于距离提醒，距离提醒高于闲时栏目。只改 `app_distance.cpp`、`pet_avatar.h/.cpp`：Alert不再禁止/取消TouchReaction；活跃时每帧提起既有不透明头像面板，结束降回原层。既有警告仍在底层，真实Alert、采样和idle Context持续更新，不引入传感器暂停、全局屏蔽或过期状态恢复。NFC/EyeRest忙时丢弃触摸、显式HOME/设置退出均保留。

先加入真实App/LVGL回归，旧行为实际失败于“TOO CLOSE rejected a fresh touch interaction”；修正后 **3514项**通过，覆盖持续Alert启动、中途Alert、20/100ms边界同帧Press/Alert及结束前距离变安全，逐帧核对红色像素、运动命令和真实距离状态。原App回归 **9334项**、Host **22/22**、完整IDF5.5.4构建均通过，记录 `.tools/touch-pet-probe/priority-results.json`、`logs/host-tests-20261103-164052-845-test.log`、`logs/firmware-20261103-164103.log`。截图visual-verdict **96/pass**；硬件替身不能证明实体流畅度或LCD帧率。

新镜像 **4,532,704字节**，剩余 **644,640字节**；SHA256 `db06eccdfdaecbeabd20ba817318adc3d68ee9bd8b79b1d83cabe35582d698d0`，ELF SHA256 `a9b4f4bde4a82d2196bcd684a74d20f9a9cd8c532319f9f4ddf469f776870aae`。55份源摘要及bin/ELF/map在 `.tools/firmware-artifacts/20261103-touch-priority77/`；相对touch76只有上述三处生产文件变化。77为归档批次，没有USB读回实际OTA序号。

首次HOME上传仍失败，客户端发送进度131072字节后中断；当前旧ELF保留在ota_1/VALID。实际uptime1154898→201177ms、reset_reason7→**4（ESP_RST_PANIC）**，证明旧固件期间发生异常/panic复位，不能称HOME已修复传输问题；没有取得panic堆栈，不套用历史canary根因。最初将4误读为ESP_RST_INT_WDT，核对固定SDK `esp_system.h:25–32` 后更正，INT_WDT实际为5；已向用户纠正并在审计JSON保留原误判及更正依据。报告 `logs/device-touch-priority77-wireless-report.json`、`logs/device-touch-priority77-home-failure-audit.json`。随后仅在私有客户端实验中改为4096字节分块、首块后等待2秒、其他块后20ms，**同一镜像**完整上传 **49.645秒**、POST200，LAN确认目标 **ota_0、新ELF、2/VALID**，见 `logs/device-touch-priority77-paced-retry02-report.json`。一次限速成功不构成根因闭环或默认更新工具已修复的证据；公开更新工具未改，本轮无USB、无新依赖、未清设置、未commit/push。

首次新固件快照：89%充电、ToF有效636mm、独立额度READY/HTTP200/56%，原授权保留；reset_reason7按ESP_RST_WDT记录，不描述成普通软件复位。新启动后的被动观察另存 `logs/device-touch-priority77-observe.json`。实机“过近中摸头完整执行，再恢复红色提醒”已发起验证，尚不以主机结果冒充用户验收。

## 持续 TOO CLOSE 摇头：构建、抢占与无线安装（2026-11-03）

用户要求过近时增加持续动作。新增 `alert_head_motion.h`：从实测姿态600ms平滑回到校准正面，此后左约6°→回中→右约6°→回中，各段600ms，正面停顿1200ms后反复循环；俯仰保持不变，受硬件限位、±8°总窗口与100ms采样/1.8°最大步长约束。App只在LIVE有效Alert且无更高优先级动作时启用，恢复距离/数据失效/显式退出停止并释放自己锁；摸头、NFC、完整护眼、菜单均先取消低优先级动作。摸头结束仍过近则从当前姿态重新开始，持续提醒不重置护眼计时。保留红底、未加音频或设置项。

实际硬件的stop可能仍短暂报告ReadMove，触摸交接仅对刚停止的自有Alert动作放行这个状态，不放行外部修改锁/任意外部移动。相对77归档只改AppDistance、TouchReaction及其测试、测试CMake，新增AlertHeadMotion及其测试；无新增依赖、全局运动重构或传感器规则修改。最终Host **23/23**，真实App/LVGL硬件替身 **5892+11707项**通过；26秒持续场景177个目标/15次左右方向转换，红色画面与前版像素一致，30次生命周期内存无变化。证据 `.tools/touch-pet-probe/alert-motion-results.json`、`logs/host-tests-20261103-172658-804-test.log`。实体位移和流畅度尚不由替身验证证明。

首次IDF构建172410的uint32模板推导错误已通过一行显式类型修复；最终完整构建 `logs/firmware-20261103-172709.log` 成功。镜像 **4,533,712字节**、槽余量 **643,632字节**，SHA256 `54230a1b6ad5ccc3e92235635017715f26dd6fc71d5ac8aa55c4bea26fec90d7`，ELF SHA256 `7f602b8090dc0d81a37fe48e4f72f1f3bec512e6521934d9fc8008ef0aca69d7`。57份源摘要与bin/ELF/map归档 `.tools/firmware-artifacts/20261103-alert-motion78/`；78仅为批次名，未用USB读取实际OTA序号。

首次在HOME使用同一4096字节限速参数仍失败，发送进度 **77824字节**、19次暂停后断线；旧77 ELF仍在ota_0/VALID。随后实际uptime2359185→206677ms、reset_reason4=ESP_RST_PANIC，证明旧固件异常重启，无法据此定位具体panic源，也不能归因于尚未安装的新连续动作。失败和审计分别为 `logs/device-alert-motion78-wireless-report.json`、`logs/device-alert-motion78-first-failure-audit.json`，保留原始证据；限速不构成已修复OTA的结论。

确认重启后仍在主菜单，未再要求用户重复操作，同一镜像/同一限速重试 **49.051秒**成功：完整4533712字节、POST200，LAN独立确认 **ota_1、新ELF、2/VALID**，记录 `logs/device-alert-motion78-retry02-report.json`。未清设置、未改公开上传工具、无USB读写、未commit/push。后续只读观察存 `logs/device-alert-motion78-observe.json`；已请用户验证持续摇头、摸头抢占及移远停止，物理验收另记。

成功安装后7次只读观察：uptime **61154→121819ms** 连续递增，reset_reason **3=ESP_RST_SW**，100%电量、非充电/非放电；ToF有效约615–674mm，独立额度READY/HTTP200/**55%**，原授权保留。此窗口在主菜单且距离安全，不把“在线稳定”冒充持续过近的实体动作验收。

## iPhone / Spotify 歌词：首版无线安装与实机网络问题（2026-11-04）

用户确认回HOME并保持底座供电后，首版镜像5,011,232 bytes通过纯Wi-Fi安装。前两次未确认成功：客户端分别发送5,011,232和69,632 bytes，但均无POST接收回执；设备后续仍是旧ELF/ota_1/VALID，最新启动reset_reason4=ESP_RST_PANIC。没有USB堆栈，不能确定故障指令、固件阶段或证明新镜像曾启动；发送进度不等于flash实际写入量。报告logs/device-lyrics-install-20261104-{161954,162306}.json，审计logs/device-lyrics-{first,second}-install-audit.json，原始失败保留。

仅在私有客户端调整同一镜像的节奏：4096字节/块、首块后4秒、其余60ms；第三次102.062秒完成POST200和摘要匹配，独立LAN读回目标ELF `8a04eb79e759ea09cc72af3fbbf2c093ff2e0595217139f2b0b77dd652c752ee`、运行/启动均ota_0、2/VALID。证据logs/device-lyrics-install-20261104-162655.json；image SHA256=74ebaf6cbdb0acc5982b64725a2b36bb90f574ffb5fdf5a233be75a0df9407f1。此前随机OTA复位根因未闭环，单次慢速成功不是修复证明；公开更新工具未变。无USB、未清NVS或授权、未改Wi-Fi。

新固件保留账号，独立额度READY/HTTP200/50%，ToF有效约600mm，reset3=软件复位。后续LAN观察捕获用户ENABLE、iPhone配对、AMS Ready、完整metadata、playing及播放进度持续增长。用户反馈Everything fine但LYRICS OFFLINE；取词实际error263/HTTP0，随后28674/HTTP0，line_count0。蓝牙已通不等于端到端取词通过，不能写成歌词实测成功。PC无代理直连同一LRCLIB查询200、49行，约1.7秒；该PC结果仍不能代替机器人网络证据。详细数据logs/device-lyrics-observe-20261104-{162906,163039}.json与logs/lyrics-lrclib-{pc,direct}-connectivity.json，不保存曲目名称或歌词正文。

另首次无媒体时出现418，核对pinned NimBLE与AppleSpec是ATT base0x100+AMS0xA2=Absent Attribute，非无线断连。后续恢复播放时自行重试Ready；修正版仅对Ready后的Select/Read A2特殊处理：空album为本代空值，必填缺失清旧词/playing、保留订阅句柄，每5秒重试并让新媒体通知提前恢复。其他ATT错误仍保留错误路径。

当前修正版将歌词改为与已成功额度服务一致的同步HTTPS，socket10秒、首次CONNECTED后另给正文10秒、45秒软总预算及返回后过期拒绝。阻塞DNS不能硬中断；不能将此描述为45秒强杀。异步1秒select之后fd_set未重填属于从固定SDK核实的待验证路径，不声称已证明是本次263/28674根因。新增五个数字诊断字段到鉴权status：阶段、耗时、errno、TLS错误与flags，不输出URL/曲目/歌词。28/28 Host与16640项真实App/LVGL回归通过，8张关键界面与首版逐字节一致，原553份界面证据未覆盖。修正版构建日志logs/lyrics-repair-idf-final.log、源摘要归档.tools/firmware-artifacts/20261104-lyrics-repair-700c190b2c23；镜像5,012,448 bytes，槽剩164,896 bytes，image SHA700c190b2c2384da967819fd459928f751310637273bf5c86c2758f35b61372d，ELF SHAd9394a112b2794fbffa57b64a7044ae919841eeaa7e0b4c6e07a8d0400a598c7。用户已再次DISABLE并HOME，修正版安装与网络实测继续记录。

### 修复版 USB 安装与保留区校验

用户 DISABLE／HOME 后两次无线安装仍未确认：客户端分别发送5,012,448和69,632 bytes，均无POST接收回执，后续仍是首版ELF／ota_0／VALID；最新reset_reason4为PANIC。报告 `logs/device-lyrics-repair-install-20261104-{164335,164707}.json`。停止盲目重试后，用户明确接回USB。12秒被动串口采集正常，但没有历史panic堆栈；当前固件禁用了持久化core dump，不能由该采集声称找到OTA根因。无线更新异常仍未闭环。

USB实读原记录为sector0／seq79／VALID，sector1全FF，原ota_0内容MD5与首版归档一致。仅将冻结修复版写入备用ota_1（0x510000），完整镜像校验后才写另一条OTA记录（0xe000，seq80／NEW），保留原seq79／VALID扇区。重启前逐区校验当前NVS、bootloader／分区表、完整ota_0及0xa00000之后全部资源和数据未变。没有全片擦除、重写分区表或清账号／手机绑定。

安装报告 `.tools/device-backups/20261104-165847-lyrics-repair-usb/deployment.json` 及同目录40秒 `early-boot-redacted.log`：bootloader实际从0x510000加载目标ELF，独立鉴权LAN读取确认运行／启动均ota_1、目标ELF `d9394a112b2794fbffa57b64a7044ae919841eeaa7e0b4c6e07a8d0400a598c7`、2／VALID。seq80来自USB写入并校验的记录，VALID来自新应用稳定运行后LAN读取，并未第二次进ROM读取其状态字。USB复位reset11，账号仍READY／HTTP200／49%，未以版本号1.6.0区分镜像。此时MUSIC仍OFF，修复后取词待重新ENABLE实测；串口前后均见ToF sensor_missing，不将历史正常测距写成此次已验证，也不将其归因于本次固件。

### 内存及无线诊断迭代：seq81–83

用户重新播放后seq80仍OFFLINE，串口记录内部RAM最低23 bytes、select内存不足和AES分配失败。seq81仅调整Wi-Fi／lwIP分配及静态TX配置；镜像5,012,656 bytes，SHA2914fe6aa99f065b359aa90d051ad4d993d8b9b82bde60e877ccbe336f1672a1，写入ota_0／seq81，原活动ota_1、NVS、bootloader／分区表、资源与尾部全部校验未变。报告 `.tools/device-backups/20261104-181548-lyrics-memory-usb/deployment.json` 保留初次安装阶段；热点关闭导致随后LAN确认失败，未将安装脚本退出1改写成成功。启动日志已确认目标ELF并标记VALID，下一次USB实读再次独立确认seq81／VALID和镜像摘要。

用户恢复热点后地址变为<ROBOT_HOST>，串口确认已联网，但内部空闲约4.9–5.1KiB、最低1371 bytes，AES／TLS仍失败，HTTP服务反复listen112。seq82把6144 bytes的AMS CPU缓存移到PSRAM，镜像5,006,512 bytes、SHAb8515206145fc543e41b141eb8d9c5b0ff8231fe21159875ef76629b165b0fc5，写入ota_1／seq82。当次保留完整seq81／ota_0及配置资源并核对MD5；报告 `.tools/device-backups/20261104-183218-lyrics-buffers-usb/deployment.json` 明确usb_write_verified=true、success=false，因为独立LAN仍不可用。启动后额度一度READY／HTTP200／49%，用户反馈NO SYNCED LYRICS；没有同期歌词HTTP值，不据提示声称取词成功。

seq83将调试服务提前到Wi-Fi初始化完成时启动，保持内部栈和鉴权。镜像5,006,592 bytes、SHA70783b54221b9a34843fa1fdf03200b2c83efb553d7a2d5a40b9aa2198e63885、ELF777feeab87c59f760978f227d2db1bfeef13c577b5c5e601b4a3b1f5b38e2cf7；USB实读seq82／VALID并验证完整原镜像后写ota_0／seq83，完整ota_1及其余保护区未变。报告 `.tools/device-backups/20261104-184327-lyrics-network-usb/deployment.json`：3174ms监听8765成功，独立LAN确认目标ELF、运行／启动ota_0、2／VALID。但随后仍有AES分配失败与TLS错误，不能把无线诊断恢复等同于歌词问题解决。同期手机尚未重连，music为pair_phone／waiting_track；审计 `logs/device-lyrics-network-usb-audit.json` 记录lyrics_received=false。

用户说明因USB位置临时拆掉ToF；缺失传感器不构成歌词页面的屏蔽条件。LIVE采集 `logs/device-lyrics-buffers-playing-redacted.log` 显示SC_PET代号持续增加、active／owns_lock为true，证明是反复的新摸头互动占屏。尚未取得用户移开手与线缆后的结果或触摸原始数据，未认定为电气噪声，未禁用合法互动。

### seq84：软件AES与可选专辑修复

候选仅在seq83基础上更改sdkconfig／defaults及歌词响应的可选专辑校验与回归测试。SDK配置关闭硬件AES，保留证书包、其他密码功能与内部任务栈；未修改SDK源码。官方S3 DMA分支每块上限1600 bytes，先前8KiB说法来自P4条件分支，已明确撤回。AMS不提供专辑时不再拒绝同歌曲／歌手／时长匹配的响应，已知专辑仍严格比较；不是模糊匹配，也不能据此断言用户NO SYNC提示的具体原因。

IDF完整重编及冻结输入再次构建检查通过，Host28/28；只复用未变化显示源码的既有LVGL报告。冻结归档 `.tools/firmware-artifacts/20261104-lyrics-tls-fe0c787efd44`：镜像5,012,752 bytes，槽剩164,592 bytes，SHA256 `fe0c787efd440de9ae4e63b6916081f55309816fa5bdfb1abc302c31fa26c068`，ELF SHA256 `ffe896a37c8a34a6d54983998285a0351225ca04e17c0b59315aad8b8f2ad590`。

USB独立读出原seq83／VALID并验证其完整镜像摘要，只写备用ota_1（0x510000），校验完成后才写另一OTA扇区seq84／NEW。逐区核对完整原ota_0、NVS、bootloader／分区表及资源尾部未变，seq83保留为当前回退；历史seq82的ota_1已被覆盖，外部归档仍在。安装报告 `.tools/device-backups/20261104-185449-lyrics-tls-usb/deployment.json`、40秒启动日志及独立鉴权LAN确认运行／启动均ota_1、目标ELF、2／VALID。此处VALID来自运行后LAN，seq84写入值由USB校验；没有全片擦除或清账号／配对。

3222ms启动无线诊断成功；设备授权刷新3591ms、额度查询3574ms，均esp_error0／HTTP200／TLS error0／flags0／socket errno0，当前额度48%。约30秒内部空闲13015 bytes、最低9339 bytes；短窗口未见AES/TLS错误，不作长期稳定承诺。此时手机尚未重新连接，music为pair_phone／waiting_track、0行，`logs/device-lyrics-tls-usb-audit.json`记录lyrics_received=false。已请用户重连Botifier Music并播放Spotify确认当前歌曲和屏幕；尚未宣称同步歌词端到端通过，也未验证历史无线上传panic已解决。

随后120秒被动串口观察仍未见AES/TLS错误，内部空闲约9147 bytes、最低8271 bytes，uptime连续；证据 `logs/device-lyrics-tls-observe-redacted.log`。两轮LAN观察仍为手机未连接，额度保持READY／HTTP200／48%。未携带密钥的状态与固件GET均返回401（`logs/device-lyrics-tls-auth-audit.json`）；未执行新的无线上传。

### seq85：触摸过滤诊断，实体问题未解决

2026-11-05更新前用户LIVE重现连续新摸头互动，seq84内部RAM仅323 bytes／最低43 bytes，原因未定。USB安装seq85至ota_0，独立鉴权LAN核对目标ELF、运行／启动ota_0、2／VALID；完整seq84／ota_1、NVS、bootloader／分区表、资源及尾部MD5校验未变。原seq83／ota_0已覆盖，外部归档保留。

归档`.tools/firmware-artifacts/20261105-head-touch85-8ee79aab493e`，镜像5,013,104 bytes，槽剩164,240 bytes；SHA256 `8ee79aab493e612e1911bbaf09249eaa63b4f1fa711b3a761046d25a0cce124b`，ELF `f7889fa29b2e145725ff3180a8e63eb30e49a67b4b0ab4955c89d0437ae3960b`。51份源输入冻结，Host29/29、IDF构建通过，未重新运行UI渲染。报告`.tools/device-backups/20261105-094622-head-touch85-usb/deployment.json`、`logs/device-head-touch85-usb-audit.json`。

用户明确无触摸时仍被表情打断。原始SC_HEAD接触边沿和Press持续增加、I2C读取错误0，100／200ms过滤未解决；证据`logs/device-head-touch85-live-after-redacted.log`。启动40秒安静不是通过验收。音乐AMS已连接／ready，取词HTTP404、error0／TLS0、0行；这只证明本次查询没有同步歌词，未宣称歌词端到端通过。用户移除了ToF，sensor_missing保留如实记录。

### seq86：配置读回正确，异常触摸仍未消除

USB写备用ota_1／seq86并独立鉴权LAN核对目标ELF、运行／启动ota_1、2／VALID；完整seq85／ota_0及配置资源MD5保持不变。seq84／ota_1现已覆盖，仅保留外部归档。报告`.tools/device-backups/20261105-100157-head-touch86-usb/deployment.json`、`logs/device-head-touch86-usb-audit.json`。

归档`.tools/firmware-artifacts/20261105-head-touch86-346ca5577c93`：镜像5,013,696 bytes，槽剩163,648 bytes；SHA256 `346ca5577c939cd78d94ddcbd4a6d04e342f2f660ad3ebe9ccf4d310b8530d5d`，ELF `7300b7c8807428be3708c57c336f2beb360de95579bc59c8fdae3f4309181121`。Host30/30、IDF构建通过；无新的显示代码或UI渲染。

启动日志实际核对13个寄存器全部符合期望，包括0x0D=0x30、灵敏度0x33、CFIG0x22、CTRL0x07及参考复位后0值，随后明确setup/reference verified。触摸任务仍运行且读取错误0，但38秒时已有50次原始接触边沿／23次Press；因此不能把配置通过当成误触修复成功。正在采集`logs/device-head-touch86-nfc-ab-redacted.log`，通过现有READER OFF临时关闭NFC活动做对照；尚未取得对照结果，不能将NFC射频干扰写成确定原因。

### seq87镜像与seq89启动：连续输入隔离、LIVE有限互动

修复镜像归档`.tools/firmware-artifacts/20261105-head-touch87-062d165da098`：5,014,528 bytes，槽剩162,816 bytes，SHA256 `062d165da0987e9274bcce61415af2e97a1363e6352b7b598da15463fa517c99`，ELF `d3067506fe457f8037f70dd3ce98335e24a39c6cc35745f5e81268f7f46df5be`。59份输入冻结；Host30/30、IDF构建、新实际App/LVGL 40,361项及274个旧帧一致性通过，五种连续触摸回放最长占屏6000ms，其后歌曲至少显示2000ms。配置、优先级及合法单／二次触摸保留，输入层增加频繁接触隔离；策略与取舍见DEVELOPMENT.md。

第一次USB写ota_0及seq87／NEW，镜像与保护区均核验正确；首启在AXP2101初始化首次读寄存器返回0x103，发生在触摸任务之前，自动回退seq86。`.tools/device-backups/20261105-104059-head-touch87-usb/deployment.json`保持success=false，完整启动堆栈保留，没有把fallback的旧SC_HEAD当作新逻辑运行。该I2C错误具体机制未查明。

受控重试没有改代码、重编或重写镜像：独立ROM读回seq87／ABORTED、seq86／VALID，校验ota_0仍是完全相同的修复镜像，只将原失败OTA扇区改为seq89／NEW。前后核验两个完整应用槽、NVS、bootloader／分区表及资源／尾部MD5不变。`.tools/device-backups/20261105-104912-head-touch89-metadata-retry/deployment.json`记录application_flash_written=false、metadata_only_write_verified=true；40秒新启动日志及独立鉴权LAN核对目标ELF、运行／启动ota_0、2／VALID。seq86完整保留于ota_1；历史seq85的ota_0已覆盖，外部归档保留。

这次实际进入Si12T配置核验并运行新SC_TOUCH。初步日志中原始边沿继续增长，但Press停在2、isolated=true、suppressed=1、read_errors=0，证明异常输入不再被逐次投递；底层信号异常仍存在。随后被动观察`logs/device-head-touch89-observe-redacted.log`用于核验持续状态；`logs/device-head-touch89-usb-audit.json`记录本轮实际范围。没有请求新的用户人工复测，不将主机硬件替身等同实体摸头灵敏度验收。

后续120秒采集得到59条完整SC_TOUCH记录，覆盖116,708ms：原始边沿79→250，接受Press始终2、隔离始终true、filtered始终0、读取错误始终0。设备输入隔离在持续异常信号下已验证。未进行新的人体摸头准确率或LCD实拍验收；正常单／二次摸头、恢复和界面最大占用保证来自真实生产逻辑的主机测试。异常期间摸头暂时不可用，安静两秒自动恢复；物理误报机制与一次启动I2C读错的机制均未被此次保护策略解决。

### seq90自动对照与seq92固定配置

seq90为临时诊断镜像，独立LAN确认ota_1／VALID后完成15段自动对照。原始数据、配置和按FTC分段统计见`logs/head-touch90-ab-analysis.json`；全部读取错误0。三通道awake条件下LOW3→5→3的非零计数为113/480→0/480→129/480，随后LOW3仍可复现，LOW7也为0。仅关闭未接通道或关闭sleep未解决。4条直方图被并发日志打断，完整数值头仍可解析，缺失直方图明确记null；原日志保留。该结果证明门槛相关性，尚未定位信号的电气来源。

正式镜像已安装为**seq92／ota_1／VALID**，归档`.tools/firmware-artifacts/20261105-head-touch92-5e21d5b08409`：5,014,544 bytes，槽剩162,800 bytes，SHA256 `5e21d5b0840974dddf98fbc1894e16df187e1e04a044b3a78df0fa36ffc2c34b`，ELF `6dc93dc1ae39c284249d39e170508d9985775e933b84f0d614f436a425de819c`。采用LOW5／活动TS1–3／保留sleep，临时自动轮测已从生产源码移除。60份输入冻结，Host30/30与IDF构建通过；App与渲染源码未变，沿用seq87已有App/LVGL报告作为未改界面的证据，没有声称重跑渲染。

为保留正常回退而不是让诊断程序再次轮测，ROM先核验seq90／89两条VALID和两份完整镜像；将90条目标记ABORTED后复用其ota_1写入生产镜像，校验后同扇区设seq92／NEW。全过程独立保护seq89／ota_0整槽、NVS、bootloader／分区表及资源／尾部；独立鉴权LAN确认新ELF、运行／启动ota_1、VALID。报告`.tools/device-backups/20261105-113447-head-touch92-usb/deployment.json`。90和86不再留在应用槽中，外部归档及失败历史保留。

本轮启动实际读回SEN1–6均55、CFIG22、CTRL07、0C=F8、0D=3F、参考及校准寄存器均0，14项完全符合期望。随后SC_TOUCH持续输出，证明采样任务运行；人体摸头灵敏度、实际LIVE画面与NFC并行场景尚未据此验收，歌词404也不是本轮触摸调整解决的问题。

生产配置再被动采集120秒，`logs/device-head-touch92-observe-redacted.log`含60条完整SC_TOUCH，uptime64,215→182,776ms，覆盖118,561ms。raw、filtered、raw_edges、presses、read_errors、suppressed均始终0，isolated始终false；设备没有依靠持续隔离获得这次静置结果。审计入口`logs/device-head-touch92-usb-audit.json`保存实际状态及上述边界。没有新增人工反复试验；后续实际摸头和LIVE使用仍应与这次静置测量分开判断。

### 歌词备用源候选93与无线安装中断

用户提供《这世界那么多人》后，候选加入LRCLIB完整匹配／省略专辑、网易及酷狗的有界备用链。生产解析器与真实HTTPS响应交叉验证：该歌LRCLIB完整签名404，省略专辑后Synced39行；网易中文艺名候选1842025914取得Synced49行。酷狗另以Hello／Adele取得45行。此为电脑端接口与生产解析器验证，不是手机当前元数据或机器人LCD验收。

最终Host30/30和ESP-IDF5.5.4构建通过，60份输入冻结，相对92只有歌词协议／服务及两个测试共5份文件变化。归档`.tools/firmware-artifacts/20261105-lyrics-sources93-6e37f1789c02`，5,026,672 bytes，剩150,672 bytes；SHA256 `6e37f1789c0262d4b8bc499be44a31764339e0f48c05b07cd64e52734adfff2e`，ELF `e72ac9bbe8356a00f0102750c4c5b7fe0346a0feb39d6756935e134c1a3e0727`。93是候选及预期下次序号，尚未安装，不能写成实机seq93／VALID。

安装前电脑没有枚举到机器人USB，改用既有鉴权Wi-Fi OTA。独立预检为92 ELF／ota_1／VALID，电量87%、放电中；4KiB分块，首块后停4秒，其余60ms，未修改设备端OTA实现。报告`logs/device-lyrics-sources93-wifi-install-20261105-124504-931265.json`：客户端发出77,824 bytes后连接中断，无接受回执，180秒内75次轮询未确认新固件，读回一直为旧92 ELF／ota_1／VALID。发送字节数不等于flash已写字节数。

上传前uptime4,007,233ms／reset_reason11，之后195,536ms／reset_reason4；SDK枚举4为ESP_RST_PANIC，证明发生了异常重启，但没有串口堆栈，不能进一步归因到某个任务或内存机制。旧89在ota_0的完整保留仅是92安装时历史证据，本次上传目标ota_0可能已擦写，当前内容未读回；外部89归档仍在，不继续声称它是当前已核验回退槽。旧92仍正常运行，AMS Ready／暂停、HTTP404、0行；已请求临时接回USB，以采集日志并安装已完成的候选，不再要求用户反复做摸头或播放试验。

失败审计`logs/device-lyrics-sources93-wifi-failure-audit.json`再次独立确认旧92 ELF／ota_1／VALID，并将候选、真实接口回放及失败状态写入environment.json的PendingLyricsSources93；已部署FirmwareBinary记录逐字段保持92不变。89槽标为当前未验证，92静置触摸证据保留原始时间与序号。尚无新固件、实机同步歌词或新屏幕验收结论。

### 追加样本：你要的全拿走

用户继续报告该歌NO SYNCED LYRICS。`logs/device-lyrics-niyaode-preflight.json`确认实际仍为旧92 ELF／ota_1／VALID，AMS Ready但暂停、HTTP404／error0／0行，电量12%且放电中；未发现机器人USB串口或ESP32／常见USB串口芯片的当前PnP设备。本轮没有再次无线安装，已提示先给底座供电。

以[Spotify公开条目](https://open.spotify.com/embed/track/37BXaP8wCJrH0M3AdtSNyk)提供的Tiger Hu／299333ms，用冻结候选的生产解析器请求LRCLIB：专辑覅忒好或无专辑均HTTP200／Synced103行；仅把专辑改成曲名则404。公开embed没有提供专辑，测试专辑来自网易同曲，手机AMS实际字段未知。因此该对照证明有源和专辑匹配可影响结果，不证明现场404就是该专辑差异。网易中文艺名候选529668356与公开时长差13ms，取得125行；英文艺名因严格匹配未被接纳。酷狗候选最近时长仍差10333ms，按±2秒规则拒绝。没有因此增加硬编码艺名替换或放宽版本校验，也没有修改生产源码；旧已哈希的歌词探针保持不变。新结果仅在ignored的`.tools/lyrics-song-niyaode/brief-summary.json`及同目录完整summary，不是实机同步歌词验收。

### seq93 USB安装：新版本正常启动，真实请求仍未命中

用户接回数据线后枚举COM38，LAN仍为旧92／ota_1／VALID，电量6%、充电true、放电false。对已经冻结的93镜像执行USB安装；没有重新编译，也没有重复无线上传。ROM核对MAC、有效92条目、当前92完整镜像及工厂bootloader／分区表，备份前缀；写备用ota_0并校验后，写seq93／NEW的独立OTA扇区。前后校验完整92／ota_1、NVS及其余前缀、资源／尾部保护区不变。独立鉴权LAN确认ELF `e72ac9bbe8356a00f0102750c4c5b7fe0346a0feb39d6756935e134c1a3e0727`、运行／启动ota_0、2／VALID；seq93的数字来自USB写后读回NEW，LAN本身不提供序号。

安装报告`.tools/device-backups/20261105-143333-lyrics-sources93-usb/deployment.json`成功；90秒单读者启动采集保留数字SC_LYRICS，没有发现panic标记。实际generation2依次为stage0／404／NotFound、stage1／404／NotFound、stage2网易搜索200／未选候选、stage4酷狗搜索200／未选候选。最终读取AMS Ready、暂停、position20843ms、0行、error0、HTTP200、耗时18729ms；该200是最终搜索响应，不等于取到了歌词。实机同步歌词未通过，继续读取实际AMS歌名／艺人／专辑／时长，不能用公开Spotify样本的成功替代现场请求签名证据。

审计`logs/device-lyrics-sources93-usb-audit.json`与environment.json已更新为当前93；先前待安装记录归入HistoricalLyricsSources93Preparation，失败Wi-Fi及89槽风险转历史，当前已核验回退为92／ota_1。原92静置触摸样本没有改成93测量，旧App/LVGL报告也未冒充新屏幕验收。所有生产输入仍匹配原93冻结清单。
