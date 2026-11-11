# iPhone 歌词（AMS）

<!-- lyrics111-current:start -->
## 111：通用匹配、手机输入识别与自动滚动修复

本轮直接读取已认证的机器人状态，固定16首独立目录元数据作对照，没有要求用户继续提供失败歌名。当前手机播放器为网易云音乐；现场原始title与来源LRC的当前句完整相等，artist则包含来源歌手、歌名及来源提供的译名说明。110将它按普通歌曲搜索，并把标题更新当作换歌。另已用真实BLE回调复现重复queue通知触发清空；实际Engine→App→LVGL负对照确认Unsynced不随时间推进、插播后丢失翻页位置。

111对可能的网易复合输入先做有界查询，要求来源字段能完整重建原artist、专辑相同、时长相差不超过2秒，再核对当前title等于活动歌词句，才启用歌词标题模式。确认期间隐藏待验证的词；45秒未证明则转普通查询。临时网络错误保留最多3轮重试及Retry-After，不把网络故障当作身份错误。重复完整BLE通知不再清空，真正queue、player、artist、album、duration变化仍失效。保留原始输入，不拆写AC/DC或合作歌手。

匹配修复仅涉及通用规则：单个坏候选不再拖垮有效响应；同一完整歌曲身份及相同精确时长的重复来源ID可选一个，并仅备用下载一次；有同专辑和±2秒佐证时允许明确的唱片版／Album Version后缀。Live、翻唱、重录等版本限制保留，所有请求仍共享8次／45秒预算。无单曲ID、歌名或艺名硬编码。

Unsynced保留完整原文和时间点，默认按原始LRC时间自动推进，显示小号TIMING?；不缩放、不裁尾、不宣称精确同步。左右点击可暂时浏览，实际继续播放6秒后回到当前句；暂停冻结、seek立即跟随，NFC／摸头／距离／护眼插播保留浏览位置。已校准Synced的35张既有画面逐字节一致，黑底、左对齐、上下文与简体偏好保持。

验证：38/38 Host、协议924项、服务303项、真实LVGL7431项通过。固定16首从14/16变为16/16 Synced，6个实际不同版本反例全部拒绝；方法是原输入和完整响应回放，加3次由新选择器决定的下载，不是用户歌单覆盖率或同一网络时点重测。最终冻结复验未新增网络请求。私有原始媒体字段和歌词正文留在ignored目录。

现有字库由32改48字形/压缩块，数据与索引省7052B，SPIRAM缓存增1525B；全部2171938像素、字形指标和Unicode映射相同，未新增依赖或改变分区。111已于11月11日通过两步Wi-Fi安装，独立确认精确ELF、ota_1／VALID；MUSIC恢复原ON，10个样本无新增重启。最终手机未连接、没有歌曲信息，尚未验证本轮实机取词与滚动。部署结果详见DEVICE_DEPLOYMENT.md。

证据：`.tools/lyrics111-coverage/final/EVIDENCE.md`、`.tools/lyrics111-scroll-audit/phone-title-validation.json`、`.tools/lyrics111-ui/evidence/lyrics111-ui-final.json`、`.tools/lyrics111-size/font48/equivalence.json`、`logs/lyrics111-host.log`。当前边界：第三方来源可能缺词；AMS输入仍可能随播放器变化；未经校准的时间轴可能错位；未测用户整张歌单或本轮实体LCD帧率。动态title仍参与原始来源搜索，尚未证明扩大查询的收益，未贸然改成只搜artist而损害普通斜杠艺人。
<!-- lyrics111-current:end -->

<!-- lyrics110-current:start -->
## 当前110：歌词优先简体，已无线安装

按用户要求，在显示阶段复用已有汉字映射，将当前歌词、上下行和TEXT ONLY手动翻页内容优先显示为简体。原始歌词、时间戳、歌曲身份、来源选择和缓存保持不变；英文大小写、空格和标点不转换。`後→后`仅补在显示层；`著`、`乾`保留原字，避免逐字转换造成词义错误。目标简体字形缺失时回退到原字。这是字符级显示偏好，不是完整词组转换器。

协议876项、服务135项、Host38/38与实际LVGL5215项通过；35帧原ASCII／简体页面逐字节一致，新增验证覆盖三行、双语、22页长句完整往返和字形回退。保持纯黑、左对齐及现有字号，无新依赖。正式固件5176384B，原分区余960B。界面证据`.tools/lyrics110-meta/ui-validation.json`；这是桌面LVGL验证，尚未完成110实体LCD验收。

110已通过两步Wi-Fi安装，独立确认ELF `4223f809fb2e1a94d270ebf74c5c5374171726404659e5002d55637039ebbaf2`／ota_1／VALID，两段均一次上传成功。MUSIC已恢复原ON且手机重连，10个恢复样本同一boot；最终设备取回48行、HTTP200／error0，取词17355ms。设备当时停在HOME、手机暂停，未做110实体LCD目视验收。证据为`logs/lyrics110-maintenance-product-install-20261109.json`、`logs/lyrics110-music-restore-20261109.json`、`logs/lyrics110-final-status-20261109.json`。以下保留历史版本记录。
<!-- lyrics110-current:end -->

<!-- lyrics109-current:start -->
## 当前109已无线安装：保留时间不匹配的歌词正文

2026-11-09，108 实机收到 `Free Loop / Daniel Powter / Daniel Powter / 192614ms`，用户核对手机 Spotify 总时长为 3:12，与设备一致。当前证据不支持“机器人把时长读短”或“手机上报错误”的解释。本次 `HTTP200 / invalid_lyrics / 0行` 的完整来源链已在主机复现：LRCLIB 精确查询404，目录选中198000ms记录，其合法LRC含49行、末208180ms／最后非空205760ms，触发来源及手机两层时间上界；网易同标题歌手候选224880／232893ms被原时长条件挡住，酷狗返回0候选。

额外目录180000／216000ms记录返回完全相同的LRC哈希，说明来源总长标签和歌词时间轴不一致。没有找到与手机192614ms完全适配的时间轴；不能把截尾、缩放或扩大门槛包装成准确同步。私有原响应和10次有界请求证据保存在 `.tools/lyrics109-probe/EVIDENCE.md`，歌词正文不写入版本库。

109只将格式、资源限制与有效内容均通过、但超出既有时间边界的LRC保留为 `Unsynced` 文本。所有身份、版本、候选时长条件保持；公开strict `parse_lrc`契约保持。服务在原8请求／45秒预算内继续找同步版本，后续成功同步优先；没有成功时才显示已保存的完整文本，网络失败或404不再擦掉它。不缩放、不裁尾，不将Unsynced放入成功缓存；换曲、断连及旧generation仍隔离。

Unsynced页面显示TEXT ONLY，点击屏幕左／右半边查看前／后页；同时间的翻译保持同组，长句完整分页，不随播放进度自动跳动。已有Synced布局和30帧动效逐字节保持。协议863项、服务135项、Host38/38、真实响应10/10和实际LVGL4221项通过；原Free Loop完整20候选、原响应进入生产全流程6请求，得到Unsynced49行／44非空，文本和时间逐行未改。

为装入原分区，已有Han表仅更换为无损页索引存储，65536码点与冻结108映射0差异；最终镜像5176224B、槽余1120B。新证据为`.tools/lyrics109-han/postpack/summary.json`、`.tools/lyrics109-meta/ui-validation.json`和`logs/lyrics109-idf.log`。本轮不修改BLE、优先级或播放进度，也不新增来源和依赖。

109现已通过两步Wi-Fi安装，独立确认新ELF `fe5a13e46c72b685de3f8ec38e4d8d62b06dbc95df1740d2146c70e6ab0eced2`、ota_1／VALID。MUSIC恢复原ON、手机连接、10个恢复样本无新增重启。手机当时已换歌；实机《Dear John》取回46行Synced、HTTP200、error0，取词15573ms，记录`.tools/lyrics109-meta/device-after-fetch-20261109.json`。这证明现有同步路径在新固件上仍工作，不能替代Free Loop的实机TEXT ONLY／手动翻页验收；未找到该3:12版本的准确同步时间轴。
<!-- lyrics109-current:end -->

<!-- lyrics108-current:start -->
## 当前108：已无线安装，同一首实机取到41行

2026-11-09，107实机再次返回 `no_synced_lyrics`。已用受认证的实际手机字段和六次有界源请求复现：LRCLIB有精确歌曲记录但只有plain歌词；网易前8条没有完整原唱匹配；酷狗返回10条，其中正确同步词先被繁简差异拒绝，统一文字后的多个同长度ID又被当成歧义。HTTP200只说明接口响应成功，不表示选到同步歌词。

108增加固定2473项BMP汉字等价表，供三个源的完整元数据比较使用；显示、查询原文、缓存身份和歌词时间轴不变。表从本机已有Windows `LCMapStringEx` 离线生成并固定哈希，无新增运行时依赖；它不是完整词组转换，`後`、`乾`等上下文相关转换不在此表覆盖范围。

仅酷狗增加0–100整数评分作为最后排序条件：标题、完整歌手集合和原有±2秒门槛先通过，专辑匹配和最近时长仍优先；评分只能在未经汉字折叠的相同三字段、相同精确时长内选择歌词条目。多对一文字碰撞、不同录音时长、同最高分仍拒绝；来源明确标为伴奏／纯音乐／instrumental的条目被排除。没有按单首歌硬编码ID或删除版本后缀。

旧代码负控复现失败；新协议846项、服务117项、Host38/38和IDF构建通过。保存的完整真实响应8项回放通过：本次原始繁体手机字段能从完整10条集合选到41行同步词，末尾246490ms不超过手机287947ms；此前四例仍为46／29／29／28行。证据为`logs/lyrics108-negative.log`、`logs/lyrics108-host.log`和`.tools/lyrics108-replay/summary.json`。

108现已通过两步Wi-Fi安装，独立确认ELF `0c5116b202ab247c1f1e79cc1cc306270d5cba08a15a404382301bc3739397d1`、ota_1／VALID。MUSIC恢复原ON、手机重连，10个观察样本同一boot且无新增重启。随后用原始相同手机字段读回实机`lyrics=synced`、41行、error0／HTTP200，取词19704ms；这是设备结果，不只主机回放。曲目当时暂停，未做新的LCD目视或整首播放验收，也未测整体歌单命中率。实机私有记录：`.tools/lyrics108-meta/device-after-install-20261109.json`；部署／恢复记录：`logs/lyrics108-maintenance-product-install-20261109.json`、`logs/lyrics108-music-restore-20261109.json`。

反复出现漏匹配说明此前围绕少数已报错歌曲的验证覆盖不足；不能把几个样本通过扩大成总体覆盖已经可靠。当前新增样本保留原始手机元数据、源响应和拒绝对照，后续以完整生产选择与下载链路回放，避免只验证单独下载能解析。
<!-- lyrics108-current:end -->

<!-- lyrics107-installed:start -->
## 当前107：已通过两步Wi-Fi安装，Spotify实机取词待确认

2026-11-09，冻结107镜像已无线安装到`ota_1`，独立读回ELF `8b765bd53e63fbe711c291101e786d9854f98dec4fe703c4713a54155a079564`、running／boot一致且VALID。全程未使用USB。原MUSIC ON状态已恢复，一次POST／202后的10个样本保持同一boot，手机已重连；额度查询也为ready／HTTP200。

本次先用709168字节的临时维护镜像完成无线启动和鉴权确认，再从维护环境上传5165008字节的完整107；两步均HTTP200并独立确认启动身份。它绕过了本次安装阻塞，没有定位原完整应用中的OTA abort根因。现在机上备用槽为已确认的维护固件，106仅保留外部归档。

歌词改动仍是下方的唯一身份／时长兜底，38/38主机测试与4/4真实响应回放保持有效。当前手机传来的是非音乐内容，0行歌词不能用于验收两个Spotify失败案例；已请求播放逃跑计划《海鸥》，不把主机回放46／29行冒充本次实体结果。未测量整体歌单命中率或新的实体显示效果。部署证据：`logs/maintenance-bridge-install-20261109.json`、`logs/maintenance-lyrics107-install-20261109.json`、`logs/maintenance-lyrics107-music-restore-20261109.json`。
<!-- lyrics107-installed:end -->

<!-- lyrics107-current:start -->
## 107匹配规则及此前直传失败记录

实际 AMS 数据复现了两例：手机给出的总时长分别为 288763ms、211452ms；LRCLIB 目录中歌名、歌手、专辑完全一致的唯一记录分别标为 311000ms、226000ms。原有 `min(10秒,3%)` 门槛拒绝了它们，但下载的全部歌词时间点仍在手机时长内。仅用公开 Spotify 条目测试曾无法复现，不能把公开元数据当作手机实际输入。

107保留原严格匹配；严格路径无结果时，才允许目录响应中三字段相同的唯一ID绕过总时长启发式。无词／伴奏的同身份记录也参与歧义判断。下载再次检查ID、目录时长、全部身份字段及每条歌词不超过手机末尾；不删除版本后缀，不缩放／裁剪歌词时间，不加入艺名翻译表或新依赖。

38/38 Host、协议802项、服务113项及ESP-IDF构建通过。原代码的两个合成回归先失败，新代码通过；保存的真实响应回放恢复46／29行，原有两个公开条目仍通过。证据：`logs/lyrics107-protocol-negative.log`、`logs/lyrics107-protocol-service.log`、`logs/lyrics107-host.log`、`.tools/lyrics107-replay/summary.json`。第三方歌词和实际手机字段只保存在ignored诊断目录。

107候选已冻结，但两次Wi-Fi安装均未成功，设备仍为106：第一次全量发送且镜像校验返回成功，第二次限速后在接收途中中断；两次均留存abort／reset4，没有有效故障PC。不能只归因为启动槽切换，也不能把第一次校验过的目标槽当作第二次之后的完整镜像。106只增加设备密钥认证后的可选元数据诊断，已验证默认状态不含曲目字段、未认证请求401。

用户反对反复切换USB后，已撤回“USB是必要下一步”的判断，未执行USB操作。MUSIC已恢复原ON状态，恢复窗口曾确认手机连接；随后读回仍为106／VALID、同一boot3，最新样本手机已断开。证据：`logs/lyrics107-wifi-retry-slow-20261106.json`、`logs/lyrics107-failure-restore106.json`、`logs/lyrics107-after-restore-status.json`。107实机取词尚未验证，没有新的LCD观感或整体歌单命中率测量；同一身份可能存在未标注剪辑版，这项兜底不能保证所有歌曲同步准确。
<!-- lyrics107-current:end -->

<!-- lyrics104-current:start -->
## 当前104：歌词统一左对齐

当前句、换行后的文字及上下两行均使用16px左边距。生产代码相对103只有一处 `LV_TEXT_ALIGN_CENTER` → `LV_TEXT_ALIGN_LEFT` 替换。36/36 Host及实际LVGL的3172项既有检查通过，[左对齐预览](demos/lyrics-lab/firmware-left-104.png)已检查。

USB已读回seq104／NEW，独立LAN确认104 ELF／ota_1／VALID，完整103备用槽及NVS／资源保护校验通过。MUSIC已按升级前的状态恢复为开启，10个恢复后样本未见重启；保存的最新状态尚未收到手机歌曲信息（connected=false、track_ready=false、0行），实体歌词观感待手机连接后查看。审计：`logs/device-lyrics104-audit.json`。本轮没有尝试Wi-Fi升级，下方103及更早记录均为历史快照。
<!-- lyrics104-current:end -->

<!-- lyrics103-current:start -->
## 当前103：纯歌词界面已通过USB安装

当前句默认36px居中，上下各一行18px灰色歌词，背景纯黑。删除歌名／歌手、播放状态、时间、进度条和装饰；长句使用32px、26px回退，最多两行，仍放不下时省略。首尾不存在的上下文留空。切句保留短促入场，稳定或暂停后停止刷新；没有同步歌词时仅显示一条状态。

生产改动仅为 `StackChan/firmware/main/apps/common/lyrics/view.cpp`，保留102字体PSRAM工作区、歌词时轴及既有交互优先级。36/36 Host、固件构建、真实生产LVGL的3172项检查／28种布局通过；稳定播放与暂停各100步均零刷新，100次页面重建未见堆增长。实际代码绘制预览：[静态](demos/lyrics-lab/firmware-focus-103.png)、[切句动图](demos/lyrics-lab/firmware-focus-103.gif)。这些是桌面LVGL输出，不是实体LCD实拍或硬件帧率测量。

已安装：USB读回真实seq103／NEW，随后独立LAN确认新ELF／ota_0／VALID。102完整备用槽、NVS和资源保护校验通过。MUSIC已重新启用，一次POST返回202，手机已连接并取回34行同步歌词（HTTP200）；10个启用后样本未见重启，60秒UART无panic／reset／诊断错误。保存状态中手机处于暂停、机器人在HOME；进入BOTIFIER→LIVE即可查看。实体LCD观感尚未人工确认。

本轮较早一次Wi-Fi上传失败：关闭音乐并回HOME后曾读到片内free783／largest512字节，用户重启后恢复到51,951／40,960字节，但上传仍在主机发出90,112字节后中断并abort/reset4。没有具体故障PC；USB安装成功不等于修复该无线问题，未重复相同上传。

103归档 `.tools/firmware-artifacts/20261106-lyrics103-3a6a9a68731c`，镜像5,162,336字节，槽余量15,008字节。最新审计：`logs/device-lyrics103-audit.json`；视觉：`logs/lyrics103-ui-final.json`。旧无线失败证据保留于 `logs/device-lyrics103-wifi-failed-audit.json`。下方版本段落为历史快照。
<!-- lyrics103-current:end -->

<!-- lyrics102-current:start -->
## 当前 102：ENABLE 重启已通过实机对照，歌词显示待继续确认

2026-11-06，USB 已安装 102／ota_1，独立 LAN 核对新 ELF `a13ebba2…` 与 VALID。旧版 100／101 的串口堆栈落在蓝牙 EM 映射断言，101 另实录 `BLE_MALLOC_FAILED`；断言在关中断状态下停住，最终触发中断看门狗。不能把此前“初始化后仍有数KB空闲”当成 DMA 内存充足：该统计还包含不可用于控制器 DMA 的 RTC RAM。

修复将字体解压器和缓存改为一个延迟分配的 14,048 字节 PSRAM 工作区，无片内内存回退。两版 ELF 的堆起点表明实际释放 14,064 字节片内空间；字体压缩数据、字形、字距和灰度完全不变，歌词时轴未改。36/36 Host、固件构建、生成器像素校验和独立代码审查通过。

一次远程 ENABLE 返回 HTTP202，蓝牙初始化／设置保存完成且 error0，手机已连接；10 个状态样本运行时间连续，60 秒串口无 panic、reset 或分配失败事件。这证明本轮 ENABLE 回归通过，不是长期压力或实体 LCD 验收。该窗口手机尚未提供可用播放曲目、歌词0行；正在等待播放与 LIVE 验证。证据入口：`logs/device-lyrics102-audit.json`。

Wi-Fi 历史间歇性 panic 尚未定位：今天 101 的整包无线安装 HTTP200／104.609秒成功，只是一轮成功记录，不能宣称该问题已消失。以下各版本记录均为当时快照，以本节及审计入口为当前状态。
<!-- lyrics102-current:end -->

<!-- diagnostic101-pending:start -->
## 最新状态：100 仍在运行，101 未安装

2026-11-05：独立 LAN 读回确认运行／启动均为 `ota_1`、100 ELF `315c3441…`、VALID。MUSIC ENABLE 已复现中断看门狗重启，最后有效 BLE 标记在 `esp_bt_controller_enable` 调用前，调用后标记缺失；这不是具体崩溃栈，也不证明与 OTA panic 同因。功能尚未修复，歌词实体显示未验收。

101 增加数字 panic 地址留存和更细的蓝牙初始化标记，已完成固件构建、34/34 主机测试及最终 ELF 审计，但两次无线安装均失败。第一次应用层摘要已通过，最后记录在 `esp_ota_end` 返回前；第二次主机送出 94,208 字节后中断，最后记录在一次 `esp_ota_write` 返回前。两次都重启回 100，不能把阶段标记当成故障指令。已停止相同条件下的反复上传；下一步通过 USB 被动采集当前 100 的 ENABLE 崩溃日志，再决定修复。

最新证据：`logs/device-diagnostic101-audit.json`、`logs/diagnostic101-after-recheck-safe.json`。101 归档：`.tools/firmware-artifacts/20261105-diagnostic101-51c5794e9db3`。inactive `ota_0` 已被尝试更新，不再声称其中保留完整 99；外部历史归档保留。以下各版“当前”记录均为当时快照，以本节为准。
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

机器人通过 BLE 的 Apple Media Service（AMS）只读手机的当前媒体信息，使用自身 Wi-Fi 向 LRCLIB、网易云及酷狗查询逐行时间戳歌词。日常不需要电脑中转，声音仍从手机、耳机或用户的音箱输出。

本文件区分源码、主机验证与实机验证。浏览器效果稿在 [lyrics-lab](demos/lyrics-lab/README.md)，不是蓝牙连接或 LCD 实拍证据。

历史记录（2026-11-05，seq93安装时；当前状态以上述seq96为准）：**备用源修复当时已通过USB安装为seq93，独立LAN确认新ELF／ota_0／VALID。实际手机该轮请求仍未取得同步歌词，尚待核对AMS真实签名。** 90秒启动日志包含LRCLIB完整／无专辑两次404、网易和酷狗各一次HTTP200但未选中候选；最终AMS Ready、0行，不能把最后一个候选搜索的200写成歌词成功。电脑端已验证的《这世界那么多人》39／49行、《你要的全拿走》103行及酷狗Hello／Adele45行当时仅属于公开元数据的生产解析器验证。

当时93镜像5,026,672 bytes，分区剩150,672 bytes；完整92／ota_1及NVS、bootloader、分区表和资源保护区已通过该次USB前后校验，89的ota_0已由93覆盖，仅保留外部归档。之前Wi-Fi上传中断和panic记录保留，不认定无线安装问题已经解决。此前92触摸门槛LOW5配置两分钟静置观测原始边沿和误触均0、隔离关闭，但真实摸头灵敏度和电气异常来源仍未确认；该轮未修改触摸、App、显示或分区。显示源码未变，旧seq87的实际App/LVGL报告仅作历史证据，没有重新渲染或LCD实拍验收。证据见[DEVICE_DEPLOYMENT.md](DEVICE_DEPLOYMENT.md)。

追加样本《你要的全拿走》：按Spotify公开条目Tiger Hu／299333ms验证，生产解析器从LRCLIB取得103行同步歌词；带专辑覅忒好或省略专辑均成功，仅将专辑改为曲名则404。这是请求签名对照，手机AMS实际专辑未知，不能据此断言现场404的原因。网易以中文艺名匹配得到125行，英文艺名未通过当前严格匹配；这一备用源限制不妨碍上述LRCLIB结果。安装前旧92／HTTP404／0行、12%放电和USB缺失的记录保留；用户接回USB后已确认充电、不放电，再完成93安装，但真实签名仍待读取，尚未通过实机同步歌词验收。

[实际固件页面的桌面渲染](demos/lyrics-lab/firmware-preview.png)使用最终字体。它验证显示代码，不证明手机已连接或实机运行流畅。

## 使用

1. 在 BOTIFIER 顶部下滑或底部上滑打开菜单，进入 **MUSIC**，按 **ENABLE**。
2. 在 iPhone 的「设置 → 蓝牙」中选择 **Botifier Music**，完成配对；打开 Spotify 播放歌曲。
3. 按机器人 **LIVE** 返回全屏内容。音乐播放时显示当前歌词、前后句、歌名、播放状态与进度；首句开始前显示 INTRO，不提前高亮歌词。
4. **DISABLE** 关闭媒体功能。**FORGET** 只删除这项功能保存的手机配对；重新配对时，必要时也在 iPhone 蓝牙设置中忽略旧设备。

开关保存到机器人本机，默认关闭。首版已在用户手机上观察到 AMS Ready 和完整媒体数据；持续更新、锁屏与重连仍需分别验证，蓝牙连上不等于 AMS 已就绪。MUSIC 页显示连接、取词状态及错误码。暂不接入 Android、电脑媒体会话、音频接收或远程播放控制。

DISABLE 关闭媒体订阅并恢复原厂 BLE 模式，不是整机蓝牙电源开关。正在进行的同步 HTTPS 请求返回后才由同一 worker 处理物理关闭；画面和状态先禁用。当前首次由 MUSIC 启动蓝牙后，关闭媒体仍会保留原厂 BLE 广播。

## 显示和优先级

NFC 与完整护眼流程沿用互不打断规则；摸头互动和过近提醒也优先于歌词。歌词占普通信息栏目位置，不暂停测距或护眼时钟，不持续上报“用户活动”来推迟护眼。前台动作结束后根据手机当前进度重新选句，不补播过时内容。

暂停后保留歌词 15 秒，再让出画面；断连、禁用或换歌立即清除旧词。没有同步歌词、纯音乐、网络失败时显示明确状态与曲目信息。逐行同步不模拟逐字卡拉 OK；同一时间戳的翻译行作为一组展示。长句按真实字体宽度换行，最多三行并截断，不滚动等待。

中文字体由仓库已有 20px 字体转为 1bpp，保留原始 cmap、度量和 kerning。覆盖 6649 个字形，不等于全部 Unicode。转换脚本及来源摘要：

- [Prepare-LyricsFont.py](scripts/Prepare-LyricsFont.py)，`python scripts/Prepare-LyricsFont.py --check` 检查可复现性。
- [font_manifest.json](StackChan/firmware/main/apps/common/lyrics/font_manifest.json)。

## 实现与限制

共享已有 NimBLE host，订阅 AMS Player、Track 和 Queue 信息；加密后完成服务发现、CCC 和属性注册才发布 ready。截断值通过 Entity Attribute 长读取得，Track/Queue 变化先清旧词，再原子提交完整元数据。播放器名称、完整曲目信息和播放时钟按顺序刷新，旧连接或旧请求不能提交到新一代。

取词顺序为 LRCLIB 完整匹配 → 404 时省略专辑重试 → 网易云候选与歌词 → 酷狗候选与歌词。省略专辑只放宽不同发行专辑，歌名、歌手及±2秒时长仍需匹配；网易与酷狗先核对候选身份，再按候选 ID 下载，无法消除歧义时不猜测。不会将英文艺名硬编码替换成中文，也不删除现场版等版本标记。首个同步歌词或纯音乐结果结束查询。

一次查询最多6个串行 HTTP 请求，间隔250ms，共用45秒软预算；socket 超时最多10秒，首次连接后正文预算10秒。阻塞 DNS 不能在该预算到期时强制中断，但返回后的过期结果会被拒绝。HTTPS 保留系统证书包校验和SDK软件AES，禁用自动重定向，每个响应体限制64KiB。换歌、断连或禁用会使旧请求失效；没有成功结果时保留网络错误与 Retry-After，避免备用源的404掩盖限流。拒绝损坏 JSON、重复键、无效 UTF-8、损坏 Base64 和无效时间轴。网易搜索正常附加字段需要7层 JSON 容器，只为这项响应使用有界8层预算，其余仍为6层。

网易、酷狗使用平台域名上的公开客户端读取接口，不需要用户账号、Cookie或新增依赖，但它们没有本项目可依赖的稳定开放 API 合约。曲库覆盖、不同录音版本、手机事件精度和歌曲本身的 LRC 时间戳仍会影响效果；一首歌能在某个平台播放，不保证该接口提供同步歌词。

`/api/status` 的现有设备密钥鉴权保留，包含 `music` 的连接、播放、代号、位置、行数与错误信息，以及取词阶段、耗时、socket errno、TLS error/flags 五个数字诊断字段，不输出手机身份、曲目名称或歌词正文。串口 `SC_LYRICS` 仅记录数字阶段0–5、HTTP状态、解析状态、错误、行数与请求数。正常运行会把当前曲目的歌名、歌手、专辑和时长发送给歌词服务以完成匹配。

## 验证入口

- `scripts/Test-Host.ps1`：真实协议/呈现逻辑与原有固件回归。
- `scripts/Test-DistanceReminder.ps1`：电脑端原有测距回归。
- `scripts/Build-Firmware.ps1`：ESP-IDF 5.5.4 实际固件构建；已有本地 sdkconfig 必须启用 `CONFIG_BT_NIMBLE_NVS_PERSIST=y`，仅修改 defaults 不会覆盖已生成的配置。

设备配对、锁屏、拖动、耳机并用、断连重连，以及当前网络上的歌词请求需要单独实测，不能以主机替身或构建成功代替。

连续输入的占屏路径已修正：只有LIVE把Press转换为优先互动；此前每次新Press重置三秒计时，因而能无限隐藏歌曲。现在连续会话最多六秒，达到上限后至少两秒不接受触摸重启；四秒内第三次确认接触起暂停接收摸头，信号连续有效归零两秒后自动恢复。异常期间摸头会暂时不可用，普通两次触摸、长按与滑动的回归通过；不等于消除了硬件异常。ToF缺失本身不会压制歌词，NFC／完整护眼优先级保留。

SC_TOUCH记录原始值与隔离状态，SC_PET记录实际互动，SC_SURFACE区分触摸／护眼／闲时脸／歌曲／设置页。后采集的一段seq86日志没有SC_PET，同时媒体暂停、普通轮播与EyeRest正常执行；不能把所有脸部画面都解释成误触。设备当前暂停超过15秒时仍会按既有规则回到普通轮播，缺词与画面抢占也应分开定位。

## 协议来源

- [Apple AMS Specification](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Specification/Specification.html)
- [Apple AMS 属性和播放时钟](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/Appendix/Appendix.html)
- [LRCLIB 官方 API](https://lrclib.net/docs)
- [LrcApi 的网易客户端读取实现](https://github.com/HisAtri/LrcApi/blob/main/mod/searchx/netease.py)
- [Syncanto 的酷狗歌词读取实现](https://github.com/alfzcore/Syncanto/blob/main/src/providers/kugou.ts)
