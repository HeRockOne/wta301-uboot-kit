# 网页一键切 A/B 槽（`POST /bootflag`）

> 目标：**进 u-boot kit 的网页恢复页就能切启动槽**，不需要进系统跑脚本。
> 实现：`tpimg_write_boot_flag()` + `failsafe/modules/tplinkinfo.c` 里的 `/bootflag` 端点
> + `failsafe/fsdata/index.html` 的「启动槽」卡片。

---

## 1. 为什么以前非得靠脚本

这块板子是**双槽**布局：同一片 NAND 上存两套完整的 u-boot + kernel + rootfs。
一级 u-boot（`factory_boot`，物理 `0x0`）自己不写死用哪套，它读一个 128 KiB
小分区 `boot_flag`（物理 `0x4000000`）决定：

```
首 8 字节 = 魔数 55 aa 42 46 + 大端 u32 槽号
  槽号 1 → 先验 image_1（B 槽：u-boot 0x0700000 / kernel 0x0880000 / rootfs 0x0FC0000）
  槽号 2 → 先验 image_2（A 槽：u-boot 0x4700000 / kernel 0x4880000 / rootfs 0x4FC0000）
```

每段二级 u-boot 又只启动**自己那一段**的 kernel/rootfs（`/chosen/bootargs` 里
的 `root=` 是内核按槽自己算出来的），所以

> **改这 4 个字节 ≡ 换一个系统。**

问题在于：原厂 u-boot 不读这个值（永远先 image_2），而我们刷进板子以后，
要切系统过去只能在**已经能启动的系统里**跑一个脚本去写那 4 个字节
（原厂侧 `wta301-slot`、OpenWrt 侧 `wta301-slot`）。

**这条路的死穴**：如果 A 槽的 OpenWrt 起不来、或 B 槽的原厂固件挂了，
你就没有系统可以跑脚本 —— 明明只差 4 个字节，却只能上串口或 TTL。

kit 的网页恢复页正好相反：它是 **u-boot 自己**（不依赖任何系统），
网页刷机能跑就说明这个 HTTP 服务活着，那它就能写这 4 个字节。

---

## 2. 机制

### 2.1 写 flash（`tplink_tpimg.c`）

`int tpimg_write_boot_flag(int slot)`：

| 步 | 做什么 | 为什么这么写 |
|---|---|---|
| 1 | 参数只收 `1` / `2`，其它直接拒 | 防止前端传进 0 或 3 把槽号写坏 |
| 2 | `nand_read` 整块 `0x20000` 到 static 缓冲 | 保留原厂在这一块里可能存在的其它字段 |
| 3 | 校验首 4 字节魔数 `55aa4246` | 魔数不对 = 这分区被别的东西改过，**拒写**，不冲未知结构 |
| 4 | 只改第 4~7 字节为大端 slot | 其余字节原样写回 |
| 5 | `nand erase` + `nand write` 整块 | 不做部分写（`nand erase` 粒度是 `0x20000`） |
| 6 | **换一个缓冲**回读前 8 字节核对 | 拿写入源自己比自己 = 同源自证，写失败也看不出来 |

两个实现细节值得记：

- **缓冲用 `static`，不用 `malloc`**。`CONFIG_SYS_MALLOC_LEN` 只有 756 KB
  （`include/configs/ipq5018.h:188`），而 `tpimg_get_status()` 最坏要同时持有
  hdr(128K) + slot(256K) + dec(640K)。网页来切槽时很可能刚跑完一次状态刷新，
  池子还没还干净 —— 再要 128 KB malloc 有 OOM 风险，OOM 会被误报成"写失败"。
  `.bss` 里的 static 既不占镜像体积，也不和 httpd/tftp 抢池子。
- **回读必须落到不同地址/不同缓冲**。这条是本项目的老教训（`nand read` 回读
  核对必须换地址，否则镜像自己续自己）。

### 2.2 网页端点（`failsafe/modules/tplinkinfo.c`）

```
POST /bootflag
Content-Type: multipart/form-data
字段: slot=1 | slot=2
```

返回：

```json
200 {"status":"success","info":{"slot":2,"name":"image_2","label":"A 槽"}}
400 {"status":"fail","reason":"bad_slot"}
500 {"status":"fail","reason":"write_failed"}
```

`write_failed` 的具体原因打在串口（`tpimg: boot_flag ...`）。

> ⚠️ **必须是 multipart POST，不能写成 `/bootflag?slot=1`。**
> `net/httpd.c:518` 拿到 URI 后第一件事就是
> `p = strchr(uri_ptr, '?'); if (p) *p = 0;` —— query string 被**直接丢弃**，
> 只有 multipart body 才会被解析进 `request->form`。

### 2.3 前端（`failsafe/fsdata/index.html`）

「固件升级」页的槽位卡片下面多一张「启动槽」卡片：

- 显示当前 `boot_flag`（`image_1 · B 槽` / `image_2 · A 槽` / `读取失败`）
- 两个按钮：「下次从 image_1 启动」「下次从 image_2 启动」
- 当前所在槽的按钮自动置灰
- 若某个槽的 u-boot 校验不过（`hdr_ok/md5_match/decode_ok` 任一为假），
  在按钮下面直接提示"切过去会被一级 u-boot 自动回退到另一段"
- 点按钮 → 确认框 → 写入 → 成功后提示「立即重启 / 稍后手动重启」

`file://` 打开时页面走预览模式（`PREVIEW`/`MOCK`），`/bootflag` 也有 mock 实现：
点按钮会真的改掉 mock 里的 `boot_flag`，所以刷新后「当前」能和刚才点的一致。

---

## 3. 安全边界

### 3.1 切过去那个槽坏了会怎样

**不会变砖**，两层兜底都在：

1. **一级 u-boot**：按 flag 挑中的那段二级 u-boot 要先过
   `verifying uboot partition... ok`（md5 比对 `+0x90`）。不过就静默落另一段。
2. **二级 u-boot**（本 kit 的补丁）：`tpimg_boot_slot()` 先试 flag 指的槽，
   起不来再试另一个槽 —— 比原厂"永远先 image_2"多一层。

所以最坏情况是「切了但没换成」，回到原来那个系统，而不是黑屏。

### 3.2 这个操作不动什么

只写 `boot_flag` 分区（`0x4000000`，`0x20000`）。**不碰**：

- 一级 `factory_boot`（`0x0`，TTL 命脉）
- 两段二级 u-boot（`0x0700000` / `0x04700000`）
- 两个 TP 头、kernel、rootfs

### 3.3 生效时机

**不会立即生效。** 当前跑的 u-boot 已经把内核拉起来了，改 flag 只影响
**下一次一级 u-boot 决策**。所以写完要 reset。

---

## 4. 怎么验证（不需要板子）

1. 用浏览器打开 `failsafe/fsdata/index.html`（`file://` 即可）
2. 页面左下角状态是「预览模式」
3. 点「下次从 image_1 启动」→ 确认 → 应提示「已切到 image_1」→
   点「立即重启」不会真重启（mock）
4. 刷新页面 → 「当前」应变成 `image_1 · B 槽`，且该按钮已置灰

真机验证要点：

- 串口应出现
  ```
  tpimg: boot_flag 2 -> 1 (image_1 / B slot)
  ### nand erase 0x4000000 0x20000
  ### nand write 0x44000000 ...
  tpimg: boot_flag= OK, next reset boots image_1 (B slot)
  ```
- 独立核对（不依赖本功能）：串口上
  ```
  nand read 0x44000000 0x4000000 0x20000
  md.b 0x44000000 8
  ```
  应为 `55 aa 42 46 00 00 00 01`
- **反向核对**：重启后一级日志应打出
  ```
  Dual image validation, to check image_1 now
  ```
  然后二级 u-boot 打 `tplboot: boot_flag=1 -> image_1 first`

---

## 5. 与用户态脚本的关系

| 场景 | 用什么 |
|---|---|
| 两个系统**都**能起来 | 随便：网页 `/bootflag`（无需网页恢复页，kit 页在正常启动时进不去）或系统内的 `wta301-slot` |
| 只剩网页恢复页 | **`/bootflag`**（本补丁的全部意义） |
| 连 u-boot 都起不来 | 串口 `nand` 裸写 / TTL |

注意 kit 的网页恢复页只在 `bootipq` **失败**后才进得去
（`board/qca/arm/common/cmd_bootqca.c:1019`：`run_command("httpd", 0)` 阻塞控制台，
之后才 `cli_loop()`）。所以"能进系统时"用系统内的脚本更顺手，`/bootflag`
的定位是**救场**。

---

## 6. 本版产物（P22）

| 文件 | 大小 | md5 | crc32 |
|---|---|---|---|
| `u-boot.bin`（中间产物） | 560,200 B (0x88C48) | `b16152e5eda7e85265f5dfaeff634528` | — |
| `wta301-uboot-p22.bin`（槽镜像，刷这个） | 262,144 B (0x40000) | **`0cf778c20152f37f3ad306d1c0dbe6d0`** | **`0x675D0AF2`** |

槽内 LZMA 有效载荷 229,412 B（0x38024），填充 32,732 B（12.5%）。
一级解压缓冲 655,360 B，`u-boot.bin` 560,200 B，余 95,160 B（14.5%）—— **先撞的是这条**。

### 6.1 两次独立生成，同一 md5（可确定性重出）

```
# 路一：build_j1.sh 末尾自动出槽
bash build/build_j1.sh
#   u-boot.bin  560200 B  md5 b16152e5…
#   lzma preset 9  229412 bytes (0x38024) = 87.5% of slot

# 路二：单独跑打包器
python3 build/mk_uboot_slot.py u-boot.bin /tmp/out
#   round-trip  ok（解压后与 u-boot.bin 逐字节一致）
#   slot md5    0cf778c20152f37f3ad306d1c0dbe6d0
#   slot crc32  0x675D0AF2
```

两条路出来的 `uboot_slot_0x40000.bin` 逐字节相同 ⇒ 打包是确定性的。

### 6.2 刷写

**走 kit 自己的网页**（这就是本版的目的，不用串口）：

1. 进恢复页 `http://192.168.1.1/`（板子在 u-boot 里自己是 `192.168.1.1`）
2. 高级 → 单独升级 U-Boot → 选 `wta301-uboot-p22.bin` → 上传并校验 → 写入
3. 重启后横幅应变为
   ```
   U-Boot 2016.01-9ce3157-dirty-9ce3157 (2026-11-09 …)
   ```
   末尾多出来的 `-9ce3157` 是 `UBOOT_SKIP_GITDIRTY=1` 导致短哈希拼了两次，
   **纯字符串，不影响行为**（见 README §1.1）。
4. 进「固件升级」页，槽位卡片下方应出现 **「启动槽」** 卡片

> ⚠️ 本版**尚未上板**。上面第 3、4 步是在离线编译与浏览器预览下推出来的预期，
> 不是实测记录。刷之前先确认板子没在做别的事（当前它是一台 5G 中继）。

### 6.3 边界：网页切槽**只改 flag，不校验目标槽**

`tpimg_write_boot_flag()` 只保证「这 4 个字节写对了」，**不保证目标槽能启动**。
目标槽坏掉时不是死局：一级 `verifying uboot partition` 会失败并落另一段，
二级 `tpimg_boot_slot()` 也会把两段都试完（`tplink_tpimg.c:1180`）。

页面在切之前会把坏槽标出来（`paintBootSel()` 读 `hdr_ok/md5_match/decode_ok`），
但那只是**提醒**，不是拦截 —— 需要强切时没人该被 UI 挡住。
