// SPDX-License-Identifier: GPL-2.0
/*
 * Realtek RTL8367S bring-up for the TP-Link WTA301 (IPQ5018).
 *
 * Why this exists
 * ---------------
 * The four front-panel jacks are the RTL8367S's internal PHYs; the SoC reaches
 * them over a 2.5G HSGMII trunk on extension interface 1 (MAC8, switch port 6).
 * The stock level-1 u-boot does have a Realtek SDK driver, but it only runs it
 * when *it* reaches its own prompt (it needs the network stack). On a normal
 * cold boot level-1 never calls board_eth_init(), so the switch stays at its
 * reset defaults and nothing forwards. Our u-boot must therefore set the switch
 * up itself.
 *
 * Everything below is taken from external evidence, not from guesswork:
 *   - mainline rtl8365mb DSA driver (SerDes PCS, HSGMII support merged as
 *     netdev v6 / OpenWrt backport 943-02)
 *   - the vendor rtl8367c API as shipped in GPL drops
 *     (rtk_switch_init, rtk_vlan_init, rtk_port_phyEnableAll_set)
 *   - TP-Link Archer AX55 v1 (same IPQ5018 + same RTL8367S) field notes
 * See work/docs/RTL8367S-COLD-BOOT-EXTERNAL-EVIDENCE.md and
 *     work/docs/RTL8367S-PHY-POWERON-FINDING.md
 *
 * Three separate things are needed, and missing any one of them leaves the link
 * either dead or half-alive:
 *
 *   1. the vendor "initial state" jam tables (chip + common), which are what the
 *      vendor's rtk_switch_init() writes and which the chip reset puts back;
 *   2. rtk_port_phyEnableAll_set(ENABLED): the internal PHYs come out of reset
 *      POWERED DOWN (BMCR = 0x1940, bit 11 set). Without this the panel jacks
 *      never link at all - the switch answers SMI perfectly the whole time,
 *      which is what makes this so easy to misdiagnose;
 *   3. the HSGMII SerDes sequence, including the *HSGMII* tuning table (the
 *      SGMII one does not work at 2.5G) and forcing 0x1D11 bit 9 (LINK), which
 *      literally gates the SerDes transmitter.
 */
#include <common.h>
#include <asm/io.h>
#include <miiphy.h>

/* SMI address of the switch on the IPQ MDIO1 bus */
#define RTL_SMI_BUS		"IPQ MDIO1"
#define RTL_SMI_ADDR		0x1d

/* SMI command registers (Realtek SMI protocol) */
#define RTL_SMI_CTRL0		0x1f
#define RTL_SMI_CTRL1		0x15
#define RTL_SMI_ADDRREG		0x17
#define RTL_SMI_DATAW		0x18
#define RTL_SMI_DATAR		0x19
#define   RTL_OP_ADDR		0x000e
#define   RTL_OP_READ		0x0001
#define   RTL_OP_WRITE		0x0003

/* Chip registers */
#define RTL_REG_CHIP_ID		0x1301	/* chip version */
#define   RTL_CHIP_ID_8367S	0x00a0
#define RTL_REG_MAGIC		0x13c2	/* write 0x0249 to unlock */
#define   RTL_MAGIC_VALUE	0x0249
#define RTL_REG_CHIP_ID_NUM	0x1300	/* only readable after unlock */
#define RTL_REG_MISC_CFG0	0x130c
#define   RTL_MISC_CFG0_DW8051_EN	0x0020
#define RTL_REG_CHIP_RESET	0x1322
#define   RTL_RESET_DW8051	0x0010
#define RTL_REG_BYPASS_RATE	0x03f7
#define   RTL_BYPASS_BIT_PORT6	0x0002	/* BIT(port - 5), port 6 -> bit 1 */

/* External interface 1 (switch port 6) */
#define RTL_REG_DIGI_SEL	0x1305
#define RTL_REG_DIGI_FORCE1	0x1311	/* NOTE: 0x1310 is EXT0 - wrong one */
#define   RTL_IF_FORCE_EN	0x1000
#define   RTL_IF_FORCE_NWAY	0x0080
#define   RTL_IF_FORCE_LINK	0x0010
#define   RTL_IF_FORCE_DUPLEX	0x0004
#define   RTL_IF_FORCE_SPD_MASK	0x0003
#define   RTL_SPEED_1000M	2
#define   RTL_EXT_MODE_HSGMII	10

/* SerDes control block */
#define RTL_REG_SDS_MISC	0x1d11
#define   RTL_SDS_MISC_MAC8_HSGMII	0x0800
#define   RTL_SDS_MISC_MAC8_SGMII	0x0040
#define   RTL_SDS_MISC_FDUP		0x0400
#define   RTL_SDS_MISC_LINK		0x0200
#define   RTL_SDS_MISC_SPD_MASK		0x0180
#define   RTL_SDS_MISC_SPD_1000		0x0100

/* CPU port */
#define RTL_REG_CPU_MASK	0x1219
#define RTL_REG_CPU_CTRL	0x121a

/* SerDes indirect access window */
#define RTL_SDS_INDACS_CMD	0x6600
#define   RTL_SDS_CMD_BUSY	0x0100
#define   RTL_SDS_CMD_RUN	0x0080
#define   RTL_SDS_CMD_WR	0x0040
#define RTL_SDS_INDACS_ADR	0x6601
#define RTL_SDS_INDACS_DATA	0x6602

/* SerDes internal registers */
#define RTL_SDS_REG_BMCR	0x0000
#define   RTL_SDS_BMCR_DPRST1	(0x1000 | 0x0400 | 0x1)	/* ANEN|ISOLATE|1 */
#define   RTL_SDS_BMCR_DPRST2	(0x1000 | 0x0400 | 0x3)
#define RTL_SDS_REG_NWAY	0x0002
#define   RTL_SDS_NWAY_EN	0x0200
#define   RTL_SDS_NWAY_RESTART	0x0100
#define RTL_SDS_REG_RESET	0x0003
#define   RTL_SDS_RESET_DEASSERT	0x7106

#define RTL_SDS_EXT_IF_ID	1

/* PHY indirect access (internal PHYs 0..3 = the four panel jacks) */
#define RTL_IND_CTRL		0x1f00
#define   RTL_IND_CMD		0x0001
#define   RTL_IND_RW_WRITE	0x0002
#define RTL_IND_STATUS		0x1f01
#define RTL_IND_ADDR		0x1f02
#define RTL_IND_WDATA		0x1f03
#define RTL_IND_RDATA		0x1f04
#define RTL_PHY_BASE		0x2000
#define RTL_PHY_OCP_BASE	0xa400	/* OCP address of PHY register 0 */
#define RTL_NUM_UTP_PORTS	4

#define RTL_BMCR_POWERDOWN	0x0800
#define RTL_BMCR_ANRESTART	0x0200

/* ---------- SMI access ---------- */

static int smi_write(u16 reg, u16 val)
{
	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_CTRL0, RTL_OP_ADDR))
		return -1;
	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_ADDRREG, reg))
		return -1;
	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_DATAW, val))
		return -1;
	return miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_CTRL1, RTL_OP_WRITE);
}

static int smi_read(u16 reg, u16 *out)
{
	u16 v = 0;

	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_CTRL0, RTL_OP_ADDR))
		return -1;
	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_ADDRREG, reg))
		return -1;
	if (miiphy_write(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_CTRL1, RTL_OP_READ))
		return -1;
	if (miiphy_read(RTL_SMI_BUS, RTL_SMI_ADDR, RTL_SMI_DATAR, &v))
		return -1;

	*out = v;
	return 0;
}

static int sw_rmw(u16 reg, u16 clear, u16 set)
{
	u16 cur;

	if (smi_read(reg, &cur))
		return -1;
	return smi_write(reg, (u16)((cur & ~clear) | set));
}

/* ---------- SerDes indirect access ---------- */

static int sds_write(u16 addr, u16 data)
{
	if (smi_write(RTL_SDS_INDACS_DATA, data))
		return -1;
	if (smi_write(RTL_SDS_INDACS_ADR, addr))
		return -1;
	return smi_write(RTL_SDS_INDACS_CMD, RTL_SDS_CMD_RUN | RTL_SDS_CMD_WR);
}

static int sds_read(u16 addr, u16 *data)
{
	u16 v = 0;
	int i;

	if (smi_write(RTL_SDS_INDACS_ADR, addr))
		return -1;
	if (smi_write(RTL_SDS_INDACS_CMD, RTL_SDS_CMD_RUN))
		return -1;
	for (i = 0; i < 25; i++) {
		if (smi_read(RTL_SDS_INDACS_CMD, &v))
			return -1;
		if (!(v & RTL_SDS_CMD_BUSY))
			break;
	}
	return smi_read(RTL_SDS_INDACS_DATA, data);
}

/* ---------- PHY indirect access ---------- */

static int rtl_phy_poll_idle(void)
{
	u16 v = 0;
	int i;

	for (i = 0; i < 40; i++) {
		if (smi_read(RTL_IND_STATUS, &v))
			return -1;
		if (!v)
			return 0;
		udelay(50);
	}
	return -1;
}

static int rtl_phy_prepare(int phy, u16 ocp)
{
	u16 addr;

	if (rtl_phy_poll_idle())
		return -1;

	addr = RTL_PHY_BASE | ((phy << 5) & 0x00e0) |
	       ((ocp >> 1) & 0x001f) | (((ocp >> 6) & 0x000f) << 8);
	return smi_write(RTL_IND_ADDR, addr);
}

static int rtl_phy_read(int phy, u8 reg, u16 *out)
{
	u16 ocp = RTL_PHY_OCP_BASE + reg * 2;

	if (rtl_phy_prepare(phy, ocp))
		return -1;
	if (smi_write(RTL_IND_CTRL, RTL_IND_CMD))
		return -1;
	if (rtl_phy_poll_idle())
		return -1;
	return smi_read(RTL_IND_RDATA, out);
}

static int rtl_phy_write(int phy, u8 reg, u16 val)
{
	u16 ocp = RTL_PHY_OCP_BASE + reg * 2;

	if (rtl_phy_prepare(phy, ocp))
		return -1;
	if (smi_write(RTL_IND_WDATA, val))
		return -1;
	if (smi_write(RTL_IND_CTRL, RTL_IND_CMD | RTL_IND_RW_WRITE))
		return -1;
	return rtl_phy_poll_idle();
}

/*
 * rtk_port_phyEnableAll_set(ENABLED) - the internal PHYs power up disabled.
 * Identical register action to the vendor API: clear BMCR bit 11, set bit 9.
 */
static int rtl8367s_phy_enable_all(void)
{
	int i, ok = 0;

	for (i = 0; i < RTL_NUM_UTP_PORTS; i++) {
		u16 bmcr = 0;

		if (rtl_phy_read(i, 0, &bmcr))
			continue;
		/* PHY_CONTROL_REG: power down is bit 11, restart AN is bit 9 */
		if (rtl_phy_write(i, 0, (u16)((bmcr & ~0x0800) | 0x0200)))
			continue;
		ok++;
	}
	return ok == RTL_NUM_UTP_PORTS ? 0 : -1;
}

/*
 * Vendor-defined initial state, in two tables exactly as the upstream driver
 * writes them in rtl8365mb_switch_init() (chip jam first, then common jam).
 */
static const u16 rtl_jam_chip[][2] = {
	{ 0x13eb, 0x15bb }, { 0x1303, 0x06d6 }, { 0x1304, 0x0700 },
	{ 0x13e2, 0x003f }, { 0x13f9, 0x0090 }, { 0x121e, 0x03ca },
	{ 0x1233, 0x0352 }, { 0x1237, 0x00a0 }, { 0x123a, 0x0030 },
	{ 0x1239, 0x0084 }, { 0x0301, 0x1000 }, { 0x1349, 0x001f },
	{ 0x18e0, 0x4004 }, { 0x122b, 0x241c }, { 0x1305, 0xc000 },
	{ 0x13f0, 0x0000 },
};

static const u16 rtl_jam_common[][2] = {
	{ 0x1200, 0x7fcb }, { 0x0884, 0x0003 }, { 0x06eb, 0x0001 },
	{ 0x03fa, 0x0007 }, { 0x08c8, 0x00c0 }, { 0x0a30, 0x020e },
	{ 0x0800, 0x0000 }, { 0x0802, 0x0000 }, { 0x09da, 0x0013 },
	{ 0x1d32, 0x0002 },
};

/*
 * SerDes tuning. These are the *HSGMII* values (rtl8365mb_sds_jam_hsgmii).
 * Using the SGMII table here leaves the trunk dead: 0x0500..0x0504 carry the
 * 2.5G equaliser/amplitude tuning and 0x0001 is part of the HSGMII set.
 */
static const u16 rtl_sds_jam_hsgmii[][2] = {
	{ 0x0500, 0x82f0 }, { 0x0501, 0xf195 }, { 0x0502, 0x31a2 },
	{ 0x0503, 0x7960 }, { 0x0504, 0x9728 }, { 0x0423, 0x9d85 },
	{ 0x0424, 0xd810 }, { 0x0001, 0x0f80 }, { 0x002e, 0x83f2 },
};

/*
 * SoC-side UNIPHY power-on for SGMII+ / 2500base-x.
 *
 * Source: level-1 disassembly, rtl_switch_enable() case 6 (this board's cfg id),
 * recorded verbatim in board/qca/arm/ipq5018/ipq5018.c next to the comment that
 * used to say "these three steps are DELIBERATELY skipped".  Level-1's order is
 *
 *     clocks OFF  ->  block reset  ->  clocks ON  ->  wait PLL lock
 *
 * Leaving the block out (what this driver did until now) works ONLY when the
 * board is entered through level-1's own prompt, because level-1 has already
 * run exactly this sequence.  On a cold boot into image_2 level-1 never calls
 * board_eth_init(), so nobody does it: the panel jacks link, ARP dies both
 * ways, and the MAC still reports "up 2500base-x".
 *
 * Measured on this board: with level-1 having configured the switch, level-1
 * itself pings 192.168.1.12 fine; booting image_2 from that same switch state
 * gives "ARP Retry count exceeded".  SoC dumps of both states differ in these
 * UNIPHY registers and in nothing else that is not a free-running counter.
 */
#define UNIPHY_BASE		0x00098000
#define UNIPHY_MODE_CTRL	(UNIPHY_BASE + 0x46c)	/* 0x820 for cfg id 6 */
#define UNIPHY_CFG_218		(UNIPHY_BASE + 0x218)	/* 0x50  for cfg id 6 */
#define UNIPHY_CFG_780		(UNIPHY_BASE + 0x780)
#define UNIPHY_PLL_STAT		(UNIPHY_BASE + 0x1e0)
#define GCC_ETH_BASE		0x01856000
#define GCC_UNIPHY_RX_CLK	(GCC_ETH_BASE + 0x110)
#define GCC_UNIPHY_TX_CLK	(GCC_ETH_BASE + 0x114)
#define GCC_ETH_RESET		(GCC_ETH_BASE + 0x004)
#define   GCC_ETH_RESET_BITS	0x32
#define SGMII_CLK_BASE		0x01868000
#define SGMII_CLK_248		(SGMII_CLK_BASE + 0x248)
#define SGMII_CLK_24c		(SGMII_CLK_BASE + 0x24c)

static void uniphy_sgmii_plus_power_on(void)
{
	u32 v;
	int i;

	/* level-1 rtl_switch_enable() case 6, instruction for instruction:
	 *   4a947758  mov r2,#0x50   / str -> [0x98000+0x218]
	 *   4a947760  mov r2,#0x820  / str -> [0x98000+0x46c]
	 *   4a947774  movw r3,#0x2bf / str -> [0x98000+0x780]; delay(10)
	 *   4a947784  movw r3,#0x2ff / str -> [0x98000+0x780]; delay(10)
	 *   4a9477a0..7cc  str 0 -> GCC+0x110, GCC+0x114,
	 *                          SGMII+0x248, GCC+0x114, SGMII+0x248,
	 *                          SGMII+0x24c   (delay 500 after each)
	 *   4a9477d0  pulse bits 0x32 of GCC+4 (delay 500 while asserted)
	 *   4a9477ec..818  str 1 -> GCC+0x110, GCC+0x114,
	 *                          SGMII+0x248, SGMII+0x24c (delay 500 each)
	 *   4a947820  poll [0x98000+0x1e0] bit 0x80, 101 tries, delay(1)
	 */
	writel(0x00000050, UNIPHY_CFG_218);
	writel(0x00000820, UNIPHY_MODE_CTRL);

	writel(0x000002bf, UNIPHY_CFG_780);
	mdelay(10);
	writel(0x000002ff, UNIPHY_CFG_780);
	mdelay(10);

	writel(0, GCC_UNIPHY_RX_CLK);
	mdelay(500);
	writel(0, GCC_UNIPHY_TX_CLK);
	mdelay(500);
	writel(0, SGMII_CLK_248);
	mdelay(500);
	writel(0, GCC_UNIPHY_TX_CLK);
	mdelay(500);
	writel(0, SGMII_CLK_248);
	mdelay(500);
	writel(0, SGMII_CLK_24c);
	mdelay(500);

	v = readl(GCC_ETH_RESET);
	writel(v | GCC_ETH_RESET_BITS, GCC_ETH_RESET);
	mdelay(500);
	writel(v & ~GCC_ETH_RESET_BITS, GCC_ETH_RESET);

	writel(1, GCC_UNIPHY_RX_CLK);
	mdelay(500);
	writel(1, GCC_UNIPHY_TX_CLK);
	mdelay(500);
	writel(1, SGMII_CLK_248);
	mdelay(500);
	writel(1, SGMII_CLK_24c);
	mdelay(500);

	for (i = 0; i < 101; i++) {
		if (readl(UNIPHY_PLL_STAT) & 0x80)
			break;
		mdelay(1);
	}
	printf("RTL8367S: uniphy pll=0x%08x locked=%d\n",
	       readl(UNIPHY_PLL_STAT), i < 101);
}

int rtl8367s_switch_init(int reset_gpio)
{
	u16 id = 0;
	int i;

	/* 0. SoC side first: level-1's rtl_switch_enable() brings the UNIPHY up
	 *    before it ever talks to the switch. */
	uniphy_sgmii_plus_power_on();

	/* 1. Release the switch from reset and give it its full power-on time.
	 *    The chip needs ~1 s of internal bring-up after reset; configuration
	 *    written earlier is silently dropped (the AX55 cold-boot bug). */
	if (reset_gpio > 0) {
		u32 cfg = (u32)GPIO_CONFIG_ADDR(reset_gpio);
		u32 io = (u32)GPIO_IN_OUT_ADDR(reset_gpio);

		writel(0x203, cfg);	/* func 0 (GPIO), output enable */
		writel(0, io);		/* active-low reset asserted */
		mdelay(20);
		writel(2, io);		/* release */
	}
	mdelay(1000);

	for (i = 0; i < 30; i++) {
		if (!smi_read(RTL_REG_CHIP_ID, &id) && id == RTL_CHIP_ID_8367S)
			break;
		mdelay(100);
	}
	if (id != RTL_CHIP_ID_8367S) {
		printf("RTL8367S: not answering on %s addr 0x%02x (ver=0x%04x)\n",
		       RTL_SMI_BUS, RTL_SMI_ADDR, id);
		return -1;
	}

	/* 2. Unlock the register file. Without this 0x1300 reads back 0. */
	smi_write(RTL_REG_MAGIC, RTL_MAGIC_VALUE);
	{
		u16 chipnum = 0;

		smi_read(RTL_REG_CHIP_ID_NUM, &chipnum);
		printf("RTL8367S: chip 0x%04x ver 0x%04x\n", chipnum, id);
	}
	smi_write(RTL_REG_MAGIC, 0x0000);

	/* 3. Vendor-defined initial state. Do NOT soft-reset here: a
	 *    read-modify-write of 0x1322 wedges the SMI slave on this board,
	 *    and the GPIO pulse above already gave us a clean chip. */
	for (i = 0; i < ARRAY_SIZE(rtl_jam_chip); i++) {
		if (smi_write(rtl_jam_chip[i][0], rtl_jam_chip[i][1]))
			goto err;
	}
	for (i = 0; i < ARRAY_SIZE(rtl_jam_common); i++) {
		if (smi_write(rtl_jam_common[i][0], rtl_jam_common[i][1]))
			goto err;
	}

	/* 4. Power the four panel PHYs up. Nothing links without this. */
	if (rtl8367s_phy_enable_all()) {
		printf("RTL8367S: PHY enable-all failed\n");
		return -1;
	}

	/* 5. Keep the embedded DW8051 in reset: it polls the same SerDes
	 *    registers through the indirect window and races us. */
	if (sw_rmw(RTL_REG_CHIP_RESET, RTL_RESET_DW8051, RTL_RESET_DW8051))
		goto err;
	if (sw_rmw(RTL_REG_MISC_CFG0, RTL_MISC_CFG0_DW8051_EN, 0))
		goto err;

	/* 6. Line-rate bypass off for the ext port. The upstream mask is
	 *    BIT(port - 5); the CPU port is port 6, so that is bit 1 = 0x0002.
	 *    (This was written as 0x0040 twice - the port number is not the bit
	 *    number. check_rtl8367s.py now fails the build on it.) */
	if (sw_rmw(RTL_REG_BYPASS_RATE, RTL_BYPASS_BIT_PORT6, 0))
		goto err;

	/* 7. HSGMII SerDes tuning. */
	for (i = 0; i < ARRAY_SIZE(rtl_sds_jam_hsgmii); i++) {
		if (sds_write(rtl_sds_jam_hsgmii[i][0], rtl_sds_jam_hsgmii[i][1]))
			goto err;
	}

	/* 8. Mux the SerDes to MAC8 in HSGMII mode. */
	if (sw_rmw(RTL_REG_SDS_MISC,
		   RTL_SDS_MISC_MAC8_HSGMII | RTL_SDS_MISC_MAC8_SGMII,
		   RTL_SDS_MISC_MAC8_HSGMII))
		goto err;

	/* 9. External interface 1 -> HSGMII. */
	if (sw_rmw(RTL_REG_DIGI_SEL, 0xf << 4, RTL_EXT_MODE_HSGMII << 4))
		goto err;

	/* 10. Take the SerDes out of reset - only after mux and mode are set. */
	if (sds_write(RTL_SDS_REG_RESET, RTL_SDS_RESET_DEASSERT))
		goto err;

	/* 11. Data-path reset to flush the FIFOs and resync the PLL. */
	if (sds_write(RTL_SDS_REG_BMCR, RTL_SDS_BMCR_DPRST1))
		goto err;
	if (sds_write(RTL_SDS_REG_BMCR, RTL_SDS_BMCR_DPRST2))
		goto err;

	/* 12. The PLL and analog front end need ~98 ms after that reset,
	 *     otherwise TX stays dead on a cold boot (AX55 root cause 2). */
	udelay(98000);

	/* 13. In-band autonegotiation must stay off - both ends are forced, so
	 *     negotiation never completes and the SerDes would drop frames. */
	{
		u16 nway = 0;

		if (sds_read(RTL_SDS_REG_NWAY, &nway))
			goto err;
		nway &= ~RTL_SDS_NWAY_EN;
		nway |= RTL_SDS_NWAY_RESTART;
		if (sds_write(RTL_SDS_REG_NWAY, nway))
			goto err;
	}

	/* 14. Force link/full-duplex/1G-code on the SerDes side. 0x1D11 bit 9
	 *     literally gates the SerDes transmitter. */
	if (sw_rmw(RTL_REG_SDS_MISC,
		   RTL_SDS_MISC_LINK | RTL_SDS_MISC_FDUP | RTL_SDS_MISC_SPD_MASK,
		   RTL_SDS_MISC_LINK | RTL_SDS_MISC_FDUP | RTL_SDS_MISC_SPD_1000))
		goto err;

	/* 15. And on the external-interface side (EXT1, so 0x1311). */
	if (sw_rmw(RTL_REG_DIGI_FORCE1, 0x1fff,
		   RTL_IF_FORCE_EN | RTL_IF_FORCE_LINK |
		   RTL_IF_FORCE_DUPLEX | RTL_SPEED_1000M))
		goto err;

	/* 16. Port 6 is the CPU port. Leave 0x121A alone: its insert-mode field
	 *     controls the 4-byte 0x8899 CPU tag, which our gmac driver cannot
	 *     strip, so the tag must stay disabled. */
	if (sw_rmw(RTL_REG_CPU_MASK, 0x07ff, 0x0040))
		goto err;

	printf("RTL8367S: 4 panel PHYs up, HSGMII trunk to port 6 up\n");
	return 0;

err:
	printf("RTL8367S: init failed\n");
	return -1;
}
