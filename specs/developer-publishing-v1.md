# TinyRT 应用开发与网站发布规范 v1

制定日期：2026-10-03。读者：应用开发者、网站前后端开发者、固件维护者和发布管理员。

本文规定从开发应用、准备 PNG 素材、构建签名包，到上传 ZIP、网站解析、审核、下载和设备安装的完整流程。网站接收一个 ZIP，设备只接收其中的 `.trpkg`。v1 采用一个 ZIP 对应一个应用版本的一种构建产物。

**实施状态**：TinyRT/SDK 的 `.trpkg` 格式、签名、Wasm/AOT、封面与 BLE 工具已经存在；本文的 `release.json`、上传 ZIP 布局、Schema、网站 API 和审核状态是本次制定的对接契约，网站尚未实现。随附 ZIP 是可用于开发解析器的真实样例，不是已经上线的发布服务。旧 `export_web.py` 产生的 `catalog.json` 交付目录属于旧格式，不符合本规范；不要直接当作 v1 上传包。

## 1. Wasm 包和 AOT 包的区别

| 项目 | Wasm 包 | Wasm 加 AOT 包 |
|---|---|---|
| 示例旧文件名 | `demo.sky-hop-wasm.trpkg` | `demo.sky-hop.trpkg` |
| 内容 | Wasm、资源、可选封面 | 相同 Wasm、资源、封面，加目标机器码和来源元数据 |
| 编译路径 | C → Wasm → 签名打包 | C → Wasm → 固定 wamrc 编译 → AOT → 签名打包 |
| 执行 | WAMR 解释器 | 条件满足时执行目标 AOT，否则按严格规则决定是否回退 |
| 兼容性 | 要求正确 ABI、权限、导入和签名信任 | 还要求目标 CPU、兼容 ID、编译来源及 AOT 签名授权 |
| 性能 | 需要在设备实测 | 通常减少解释开销，仍须实测，不能承诺固定 FPS |
| 当前示例大小 | 444248 字节 | 468388 字节 |

两个文件都是完整且带签名的包。“裸的”只是文件名没有 `-wasm`，不是未编译、未签名或裸二进制。网站必须解析 section，不得按文件名猜后端。

**回退限制**：包含 AOT 的包即使带 Wasm，若签名钥没有 AOT 授权，也会拒绝整包；只有授权、结构与来源检查先通过，后端禁用或目标/兼容 ID 不匹配时才可能回退。本次安装错误 28681（0x7009）为 `TINYRT_VERIFY_FAILED`，原构建既没有受信任应用公钥，也未配置开发 AOT，不能依靠改文件名解决。

v1 网站默认支持 Wasm；可增加 `wasm+aot` 变体。暂不接收 AOT-only。纯 Wasm 不等于任何固件都能运行，新音频导入仍需匹配固件。

## 2. 各层职责和开发前提

- ESP-IDF/FreeRTOS：固件和任务调度；LVGL：设备 UI 与最终显示；BSP/svc：屏幕、音频、输入等硬件服务。
- TinyRT core：WAMR 沙箱、ABI/权限、资源与内存限制、签名验证、安装库和应用生命周期。
- TinyRT SDK：C 头文件、模板、Wasm 编译、桌面预览、资源打包、签名与 BLE 安装工具。应用开发不需要单独编译 ESP-IDF。
- 网站：账号与应用归属、上传解析、签名策略、审核、目录和下载；不能替设备决定最终接受或绕过设备验签。
- 固件部署方：配置受信任应用公钥、作用域、AOT 授权和运行能力。固件签名钥与应用签名钥是两套独立用途。

准备 Python 3.10+、SDK requirements、Zig 0.13 或 SDK 支持的 wasm32 Clang、匹配的 WAMR runner；素材转换需要 Pillow。SDK 发布应锁定 core 契约提交及工具链。不要只写“兼容 0.1.0”：相同显示版本可能包含不同导入能力。

当前 Flappy Bird 所需核心契约提交为 `3f9ff9b0e20e293fe386ed7764d5f2a3711880cb`，这只是兼容性核查线索；固件仍须实际配置公钥、导入和能力。不能简单用 Git 提交先后顺序判断兼容。

## 3. 应用源码目录与 app.json

```text
my-game/
  app.json                 SDK 构建输入，不上传给设备
  main.c                   应用逻辑
  cover.png                210×210 安装封面
  resources.bin            可选，包内只读资源
  assets/                  原始美术和音频，以及来源/许可证
  events.json              可选，桌面回放事件
  README.md                用户说明
  build/                   构建产物
```

示例：

```json
{
  "app_id": "demo.sky-hop",
  "title": "Flappy Bird",
  "version": 2,
  "abi_version": 1,
  "permissions": 31,
  "memory_pages": 4,
  "budget": 100000,
  "sources": ["main.c"],
  "cover": "cover.png",
  "assets": "resources.bin"
}
```

| 字段 | 现有 SDK 规则 |
|---|---|
| app_id | 1–31 字节 ASCII，字符 `[a-z0-9._-]`；网站要求账号拥有命名空间；开发钥仅允许 `demo.` 的非空后代 |
| title | 1–63 UTF-8 字节，无 NUL；中文按字节计数 |
| version | 1–4294967295 的整数，升级递增；不要填字符串 `1.0.0` |
| abi_version | 当前固定 1 |
| permissions | 位掩码 DRAW=1、INPUT=2、STORAGE=4、CLOCK=8、AUDIO=16，其他位必须为 0 |
| memory_pages | 1–16，每页 65536 字节；还存在 runtime 总配额，不能只看线性内存 |
| budget | 每回调 1–100000 指令预算；AOT 使用对应保护机制，不视为设备耗时保证 |
| sources | 1–64 个应用目录内部的唯一 `.c` 文件 |
| cover/assets | 可选，本地路径解析后仍须在应用目录内；网站 v1 要求实际发布包具有 cover |

不要把网站介绍、作者、截图等字段塞入 `app.json`，SDK 会拒绝未知字段。网站元数据使用独立的 `release.json`。

## 4. 生命周期、交互、资源和音频

实现 `tinyrt_init(width,height)`、`tinyrt_event(kind,x,y,arg)`、`tinyrt_render()`；可选 `tinyrt_stop()`。成功返回 0。回调及时返回，不自己忙等。渲染从 `draw_clear` 开始，或只使用 `draw_skip` 保留上一帧。

466×466 圆屏的四角不可见，重要按钮与文字放在中央安全区域；背景可铺满正方形画布，由屏幕裁剪成圆。实际 hitbox 应与可见按钮一致。测试触摸按下/释放/取消、拖动、KEY1、宿主退出和队列溢出。CANCEL 清理所有保持输入，防止释放事件丢失后卡住。

资源通过 `asset_read` 读取，只允许 init/event，每次最多 4096 字节。一帧最多 128 条绘制命令；RGB565 合计只能提交一张图，源尺寸不超过 256×240，可缩放到 466×466。大像素处理分片，避免解释器预算超限。KV 为每应用 16 个 i32，按变化保存；成功不表示已经立即写入 Flash。

`audio_play(offset,length,16000)` 从当前签名包资源播放 PCM16 LE、单声道、16 kHz；长度 2–32000 偶数字节，最多 1 秒。仅 init/event，每回调最多 4 次请求；0 接受、1 忙/不可用，队列满可丢弃音效，不让游戏退出。SDK 不自动把任意 WAV/MP3 转换成此格式；开发者的资源管线负责转换及偏移表。桌面 runner 静音接受请求，不能据此标记喇叭实测通过。

## 5. PNG 和说明文件规范

以下是网站 v1 额外限制，包格式本身的限制以 `specs/package.md` 为准。

| 文件 | 必需 | 格式和上限 |
|---|---|---|
| `cover.png` | 是 | 单帧 PNG、210×210、≤65536 字节；RGB/RGBA；禁止 APNG |
| `screenshots/01.png` 等 | 是，1–6 张 | 单帧 PNG、466×466、每张≤1 MiB；编号连续 01–06；禁止 APNG |
| `README.md` | 是 | UTF-8 无 BOM、LF、≤65536 字节；操作、玩法、退出、兼容要求、已知问题 |
| `LICENSES.md` | 是 | UTF-8 无 BOM、LF、≤65536 字节；代码、美术、字体、音效分别写来源与许可，包含需要保留的许可证正文 |
| `release.json` | 是 | UTF-8 无 BOM、≤32768 字节；禁止重复键、NaN、Infinity；严格按随附 Schema |
| `package.trpkg` | 是 | 完整 TinyRT 包，≤2097152 字节，禁止改写签名后字节 |

封面来源必须与包内 cover 一致：网站按 SDK 的 cover 转换规则生成两档 RGB565，再与签名包中的 cover section 比较；直接复用固定 SDK 转换器，避免不同缩放实现造成不一致。透明像素合成到 `#101418`；缩略图 150×150 使用 SDK Lanczos。网站可以生成自己的网页缩略图，但不能替换设备包的封面。

截图必须声明 `capture=wamr-preview` 或 `device`。桌面截图可以加圆屏遮罩，但不能称为设备实拍；不把 AI 概念图作为运行截图。截图外部圆角/遮罩可由网页 CSS 绘制。v1 不收宣传长图、SVG、GIF、视频或音频附件；后续扩展升 schema。

README 不嵌入原始 HTML，不引用外部图片；允许链接 HTTPS 文档，网站统一清洗 Markdown 并禁用脚本。LICENSES 内容为开发者声明，网站审核需独立确认。Flappy 原版素材来源仓库的代码许可证不能自动证明美术和音效的商业授权。

## 6. 上传 ZIP 布局

推荐名称 `<app_id>-v<version>-<variant>.zip`，variant 为 `wasm` 或 `wasm-aot`。文件名只用于人读，真实身份以清单和签名包为准。

```text
demo.sky-hop-v2-wasm.zip
  release.json
  package.trpkg
  cover.png
  README.md
  LICENSES.md
  screenshots/01.png
  screenshots/02.png
  screenshots/03.png
```

根目录直接是上述文件，不能再包一层目录。ZIP 只含普通文件条目，不写独立目录条目；总数 6–11 个。允许 ZIP_STORED 和 DEFLATE，禁止加密、多卷和 ZIP64。压缩文件≤12 MiB，累计实际解压字节≤10 MiB；每条目同时执行上述文件上限。以实际流式解压计数为准，不能信任 central directory 的 size。CRC 检查必须通过，但不能代替 SHA-256/验签。

白名单路径全部采用上述小写文件名与 `/`。拒绝绝对路径、`..`、反斜杠、冒号、空路径、重复条目、大小写折叠后冲突、符号链接和其他特殊文件、尾随点/空格。拒绝未知文件，因此私钥、源码、构建脚本、设备固件、`.git` 不得混入。先校验再按受控临时路径读取，不能对任意 ZIP 直接 extractall 到网站目录。

## 7. release.json 契约

机器约束见 `distribution-v1.schema.json`；语义、字节数、跨文件一致性与信任检查见本文。JSON Schema 通过不代表可以发布或运行。

| 字段 | 规则 |
|---|---|
| schema_version | 固定 1；不认识的版本拒绝，不猜测兼容 |
| app_id/title/version | 必须与签名包逐项相等 |
| variant | `wasm` 或 `wasm-aot`；从 section 验证，不能仅相信声明 |
| requested_channel | `development`、`beta`、`stable`；只是申请，服务器决定最终状态 |
| publisher_display | 作者显示名；1–80 字符，不作为账号身份依据 |
| locale | v1 固定 `zh-CN`；多语言使用未来版本 |
| summary | 1–120 Unicode 字符，纯文本；详细说明在 README |
| category/tags | category 为 game/tool/art；tags 0–8 项，每项1–20字符、唯一 |
| controls | 1–8 条，每条1–120字符，用户可理解的操作说明 |
| display | 固定 width=466、height=466、shape=round；full_bleed 为是否铺满屏幕 |
| package | 固定路径、size、sha256，以及 format=1、abi=1、permissions、memory_pages、budget、key_id；全部核对包内值 |
| requirements | core_contract_revision 为40位小写 Git SHA；imports 为实际 tinyrt 导入名全集、无重复；aot 为 null 或目标和64位 compat_id |
| signing | development/release 声明；必须根据服务器可信公钥注册表重新裁定 |
| verification | native、wamr、device 为 passed/not-run/failed；只是作者自报，服务端另存自己的验收结果 |
| rights | status 为 declared-cleared/unverified；note 是授权范围或欠缺说明，不是自动授权凭据 |
| cover/readme/licenses | 固定 path，实际 size、SHA-256；cover 再加 width=height=210 |
| screenshots | 1–6 项，固定连续路径、size、sha256、466尺寸及 capture 标识 |

所有 SHA-256 使用原始文件全部字节计算，输出64个小写十六进制字符；JSON、Markdown 不做换行归一化后再比较。release.json 不包含自己的哈希，避免循环；服务器另外计算整个 ZIP 的 SHA-256。禁止任意 URL 代替包内文件路径。

`requirements.imports` 从真实 Wasm 导入表提取，必须等于包内全集；不得有 WASI 或未支持模块。`wasm` 要求 AOT section 不存在且 requirements.aot=null；`wasm-aot` 要求两种 section 均存在，requirements.aot 与 AOT 元数据一致。不要用权限位推断所有导入能力，例如同为 DRAW，不同固件不一定支持同样绘制函数。

未知字段按 v1 拒绝。新增字段通过新版 Schema 演进，不依赖各端忽略陌生字段。本次样例的完整 JSON 在随附上传 ZIP 根目录，不使用占位哈希。

## 8. 开发者从源码到上传

以下在 SDK 根目录执行。`path/to/...` 替换为本机工具路径；不是完整可直接执行的机器路径。

```powershell
python -m pip install -r requirements.txt
python tools/tinyrt.py new my-game --app-id demo.my-game --title "My Game" --template game
python tools/tinyrt.py build my-game --cc path/to/zig.exe
python tools/tinyrt.py run my-game --runner path/to/tinyrt-run.exe --events my-game/events.json --frames 32,1000,5000
```

先编写逻辑、封面及资源，再编译；模板不会自动生成你的截图、授权说明和发布清单。为游戏添加有效的交互/碰撞/存档测试，检查实际圆屏布局、预算和资源上限。

开发期 Wasm：

```powershell
python tools/tinyrt.py pack my-game --development-key --output my-game/build/game-wasm.trpkg
python tools/tinyrt.py validate my-game/build/game-wasm.trpkg --development-key
```

开发期 Wasm+AOT，仅对具备开发 AOT 授权的固件：

```powershell
python tools/tinyrt.py pack my-game --aot --development-key --wamrc path/to/wamrc.exe --output my-game/build/game-wasm-aot.trpkg
python tools/tinyrt.py validate my-game/build/game-wasm-aot.trpkg --development-key
```

设备打开安装窗口后：

```powershell
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF --pair install my-game/build/game-wasm.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF launch my-game/build/game-wasm.trpkg
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF info --app-id demo.my-game
python tools/ble_install.py --address AA:BB:CC:DD:EE:FF runtime
```

必须在真机验收触摸/按键、退出重开、最高分、声音/静音、运行持续性和帧率，再把 device 标为 passed。安装成功不等于游玩通过，桌面时间不等于设备 FPS。

最后准备符合规范的 PNG、README、LICENSES，复制选定 `.trpkg` 为 ZIP 内的 `package.trpkg`；解析包元数据、计算每文件 SHA/size、生成 release.json，再使用 ZIP 工具按白名单路径归档。打 ZIP 不重新编译或重签 `.trpkg`。**当前 SDK 尚未提供通用 v1 ZIP 导出命令**；网站/SDK 团队按随附规范实现，不能把旧 `export_web.py` 宣称为通用发布器。

## 9. 正式签名与发布职责

PUBLIC 开发钥仅用于受控测试入口，不具备发布者身份保证；不能把 development 包修改为 stable 就正式上架。网站用户也不得把生产私钥放进 ZIP。

生产 Wasm 签名由已登记发布者或平台隔离签名任务完成：

```powershell
python tools/tinyrt.py pack my-game --key path/to/application-signing.pem --key-id 100 --output my-game/build/game-release.trpkg
python tools/tinyrt.py validate my-game/build/game-release.trpkg --public-key path/to/application-public.sec1 --key-id 100
```

key_id=100 是示例，必须由平台注册并与设备信任表对应。生产钥不能是公开开发标量；不要复用固件 Secure Boot 私钥。

生产 AOT 不能直接接收上传者提供的裸机器码后代签。平台隔离编译器按受控 source-lock、provenance、release-profile 和 runtime 补丁重编译，调用已有 `tools/release_compile.py`：

```powershell
python tools/release_compile.py --manifest path/to/app.json --wasm path/to/app.wasm --source-lock path/to/source-lock.json --provenance path/to/provenance.json --release-profile path/to/release-profile.json --runtime-patch path/to/first.patch --runtime-patch path/to/second.patch --key path/to/application-signing.pem --output path/to/game-release.trpkg
```

补丁数量与顺序来自受控配置，示例两项不是固定数量。该工具默认含 Wasm 回退，网站 v1 不使用 `--without-wasm`。这是受控发布流水线的输入，不是本文上传 ZIP 必须额外包含的文件。

v1 ZIP 是**已构建已签名分发物上传协议**。平台代编译/代签是另一个受权限控制的流程；若网站以后支持“仅上传源码”，应设计独立接口，不能自动运行 ZIP 内脚本或混入生产密钥。服务器不得静默重签现有上传包，因为会改变包身份；重签结果应重新生成清单并走新产物审核。

## 10. 网站解析与审核顺序

1. 认证账号、限额/限流，上传到隔离暂存区，计算 ZIP 摘要；不立即加入公开目录。
2. 校验 ZIP 格式、路径白名单、条目数、压缩/解压上限、重复/链接/特殊文件和 CRC。
3. 解析 release.json，拒绝重复键与非标准数值，按 Schema v1 校验。
4. 对白名单文件逐一核对存在性、数量、size、SHA-256；解码 PNG 确认尺寸、单帧及格式，不能仅信扩展名或 IHDR。
5. 解析 `.trpkg` 头与 section，校验规范布局、总长、权限/内存/预算。与 JSON 的身份及策略逐项比对。
6. 通过受控公钥注册表选择 key_id，检查上传账号拥有应用命名空间、key 可用且授权覆盖该 app_id。公钥不能取自上传 ZIP；JSON 的作者名不是授权。
7. 验证 ECDSA-P256-SHA256/low-S、载荷摘要、AOT 来源绑定和 cover 一致性。复用 SDK/core 验证器，避免网站自行维护一个宽松解析器。
8. 使用隔离 core 校验 Wasm 的结构、imports/exports/内存等；需要运行时的回放在资源受限子进程进行，不在网站主进程执行上传代码。普通 SDK validate 仅验证信封，不能代替此步。
9. 对 AOT 验证受控编译来源与目标 profile；桌面不能执行 Xtensa 机器码，不把主机信封检查叫作真机验证。
10. 根据固件能力登记表计算兼容性；RUNTIME_INFO 可查询后端/profile，但当前协议不报告全部导入和公钥信任表，不能单靠此接口保证能安装。未知组合显示“兼容性待验证”，设备保留最终裁决。
11. 分别记录平台测试和作者自报，审核素材权利、描述、截图与实际玩法；决定 development/beta/stable。
12. 审核通过后原子发布不可变文件、目录记录与下载地址；失败保留结构化错误，不产生半发布版本。

封面/截图衍生图使用独立媒体域或严格内容类型、`nosniff`；README 经过清洗再渲染。不要把 README 的指令作为服务端任务执行。

## 11. 发布身份、版本与状态

应用身份为 app_id；版本为 app_id+version；构建产物为 app_id+version+variant；设备安装身份还包含完整 `.trpkg` SHA-256。

同一版本允许分别上传 wasm 与 wasm-aot；包内 title、permissions、abi、memory_pages、budget 和来源 Wasm 必须一致，避免把两个不同游戏装成两个变体。相同产物键重复上传相同包、相同资源与规范化清单视为幂等；仅 ZIP 压缩或时间戳不同不算新版本。相同产物键内容不同返回冲突，禁止覆盖。

**设备注意**：同一 app_id/version 的不同摘要可能被设备拒绝为冲突；已经安装 Wasm v2 后，不保证能直接换为 AOT v2。切换应发布递增版本，或由用户明确卸载旧版本（会删除该应用存档）后安装，不自动卸载。网站最好在用户首次安装时选定变体，后续升级递增版本。

建议流程状态：uploaded → validating → awaiting_review → published；校验失败为 rejected，主动撤回为 withdrawn。channel（development/beta/stable）与状态分开。作者不得自行写入 published、平台验收通过或服务器时间。

- development：可用 PUBLIC demo 钥；隔离测试目录；清楚标出需要开发固件。
- beta：已登记非公开发布钥、兼容性/功能验证和素材审核完成，面向测试用户。
- stable：同 beta，并有匹配设备的完整体验验收、可复现构建记录和发布审核。
- unverified 权利或 not-run 真机状态：可以保留开发样例，不能自动发布到 beta/stable。

包/封面/运行截图变更必须生成新应用版本和新产物；页面纯介绍勘误可由平台独立、带审计地编辑，不覆盖原上传 ZIP。撤回保留历史审计并停止推荐安装；回滚通过重新发布更高 version 的已验证代码，不承诺设备支持版本倒退。

## 12. 建议网站 API 与错误码

以下是待网站实现的接口约定，不是当前在线服务：

| API | 用途 |
|---|---|
| POST `/api/v1/app-uploads` | multipart/form-data，字段 file 为一个 ZIP；返回202及 upload_id/status，不同步阻塞到审核完成 |
| GET `/api/v1/app-uploads/{id}` | 查询本账号上传状态、校验阶段、错误；返回平台解析元数据 |
| POST `/api/v1/app-uploads/{id}/publish` | 仅审核/发布权限可调用，事务检查版本冲突后发布 |
| GET `/api/v1/apps/{app_id}/releases` | 查询公开且设备条件匹配的版本与变体；隐藏未审核/撤回版本 |

上传可带 Idempotency-Key，同一账号同键重复不同内容返回409；键的保留期由服务器配置并对客户端明确。异步校验失败由查询结果表达，不能把202当作上传验收通过。

错误对象固定 `code/message/path`，path 为包内相对字段或文件，不能泄露服务器路径、栈或密钥信息。示例：

```json
{"status":"rejected","errors":[{"code":"PACKAGE_METADATA_MISMATCH","message":"清单版本与签名包不一致","path":"release.json/version"}]}
```

建议错误码：ZIP_INVALID、ZIP_PATH_INVALID、ZIP_LIMIT_EXCEEDED、SCHEMA_UNSUPPORTED、MANIFEST_INVALID、FILE_MISSING、FILE_HASH_MISMATCH、IMAGE_INVALID、PACKAGE_INVALID、PACKAGE_METADATA_MISMATCH、SIGNATURE_UNTRUSTED、NAMESPACE_FORBIDDEN、AOT_UNAUTHORIZED、RUNTIME_INCOMPATIBLE、VERSION_CONFLICT、RIGHTS_REVIEW_REQUIRED。

下载接口返回 `.trpkg` 的 size、SHA-256、variant、签名类别和兼容说明；客户端下载后重算摘要再通过 BLE 安装。网站 ZIP 不是设备固件，不能交给 `ota_push.py`；`.trpkg` 也不能用 esptool 写入任意偏移。

## 13. 网站实施验收清单

必须通过：合法样例解析；PNG尺寸错误；缺文件；伪造hash/size；元数据与签名包不符；未知schema；未知公钥；demo钥请求stable；AOT无权限但带Wasm；同版本不同内容冲突；相同内容幂等；路径穿越/反斜杠/大小写碰撞/重复JSON键；超额解压；未知文件或私钥混入；缺权限/未知Wasm导入；原子发布失败回滚。

实际已交付：本文、JSON Schema、Flappy Bird 完整清单示例和符合文件布局的真实 Wasm ZIP。本次只本地生成；尚未实现网站服务、通用SDK ZIP导出器、生产签名服务，也未完成这款游戏的真机游玩验收。

源依据：SDK `AGENTS.md`、`specs/package.md`、`tools/tinyrt.py`、`tools/release_compile.py`、`tools/ble_install.py`；固件 `tools/TINYRT_DEVELOPER_RELEASE.md`。这些规则中包内约束来自现有实现，上传ZIP与网站行为是本次新增规范。
