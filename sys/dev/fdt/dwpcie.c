/*	$OpenBSD: dwpcie.c,v 1.12 2019/01/11 08:03:24 patrick Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define PCIE_PORT_LINK_CTRL		0x710
#define  PCIE_PORT_LINK_CTRL_LANES_MASK			(0x3f << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_1			(0x1 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_2			(0x3 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_4			(0x7 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_8			(0xf << 16)
#define PCIE_PHY_DEBUG_R1		0x72c
#define  PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING	(1 << 29)
#define  PCIE_PHY_DEBUG_R1_XMLH_LINK_UP			(1 << 4)
#define PCIE_LINK_WIDTH_SPEED_CTRL	0x80c
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_MASK		(0x1f << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_1		(0x1 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_2		(0x2 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_4		(0x4 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_8		(0x8 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE		(1 << 17)

#define MISC_CONTROL_1		0x8bc
#define  MISC_CONTROL_1_DBI_RO_WR_EN	(1 << 0)
#define IATU_VIEWPORT		0x900
#define  IATU_VIEWPORT_INDEX0		0
#define  IATU_VIEWPORT_INDEX1		1
#define  IATU_VIEWPORT_INDEX2		2
#define IATU_OFFSET_VIEWPORT	0x904
#define IATU_OFFSET_UNROLL(x)	(0x200 * (x))
#define IATU_REGION_CTRL_1	0x000
#define  IATU_REGION_CTRL_1_TYPE_MEM	0
#define  IATU_REGION_CTRL_1_TYPE_IO	2
#define  IATU_REGION_CTRL_1_TYPE_CFG0	4
#define  IATU_REGION_CTRL_1_TYPE_CFG1	5
#define IATU_REGION_CTRL_2	0x004
#define  IATU_REGION_CTRL_2_REGION_EN	(1U << 31)
#define IATU_LWR_BASE_ADDR	0x08
#define IATU_UPPER_BASE_ADDR	0x0c
#define IATU_LIMIT_ADDR		0x10
#define IATU_LWR_TARGET_ADDR	0x14
#define IATU_UPPER_TARGET_ADDR	0x18

#define PCIE_GLOBAL_CTRL	0x8000
#define  PCIE_GLOBAL_CTRL_APP_LTSSM_EN		(1 << 2)
#define  PCIE_GLOBAL_CTRL_DEVICE_TYPE_MASK	(0xf << 4)
#define  PCIE_GLOBAL_CTRL_DEVICE_TYPE_RC	(0x4 << 4)
#define PCIE_GLOBAL_STATUS	0x8008
#define  PCIE_GLOBAL_STATUS_RDLH_LINK_UP	(1 << 1)
#define  PCIE_GLOBAL_STATUS_PHY_LINK_UP		(1 << 9)
#define PCIE_PM_STATUS		0x8014
#define PCIE_GLOBAL_INT_CAUSE	0x801c
#define PCIE_GLOBAL_INT_MASK	0x8020
#define  PCIE_GLOBAL_INT_MASK_INT_A		(1 << 9)
#define  PCIE_GLOBAL_INT_MASK_INT_B		(1 << 10)
#define  PCIE_GLOBAL_INT_MASK_INT_C		(1 << 11)
#define  PCIE_GLOBAL_INT_MASK_INT_D		(1 << 12)
#define PCIE_ARCACHE_TRC	0x8050
#define  PCIE_ARCACHE_TRC_DEFAULT		0x3511
#define PCIE_AWCACHE_TRC	0x8054
#define  PCIE_AWCACHE_TRC_DEFAULT		0x5311
#define PCIE_ARUSER		0x805c
#define PCIE_AWUSER		0x8060
#define  PCIE_AXUSER_DOMAIN_MASK		(0x3 << 4)
#define  PCIE_AXUSER_DOMAIN_INNER_SHARABLE	(0x1 << 4)
#define  PCIE_AXUSER_DOMAIN_OUTER_SHARABLE	(0x2 << 4)

/* i.MX8MQ registers */
#define PCIE_RC_LCR				0x7c
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1		0x1
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2		0x2
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK		0xf
#define  PCIE_RC_LCR_L1EL_MASK				(0x7 << 15)
#define  PCIE_RC_LCR_L1EL_64US				(0x6 << 15)

#define IOMUXC_GPR12				0x30
#define  IMX8MQ_GPR_PCIE2_DEVICE_TYPE_MASK		(0xf << 8)
#define  IMX8MQ_GPR_PCIE2_DEVICE_TYPE_RC		(0x4 << 8)
#define  IMX8MQ_GPR_PCIE1_DEVICE_TYPE_MASK		(0xf << 12)
#define  IMX8MQ_GPR_PCIE1_DEVICE_TYPE_RC		(0x4 << 12)
#define IOMUXC_GPR14				0x38
#define IOMUXC_GPR16				0x40
#define  IMX8MQ_GPR_PCIE_REF_USE_PAD			(1 << 9)
#define  IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE_EN		(1 << 10)
#define  IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE		(1 << 11)

#define ANATOP_PLLOUT_CTL			0x74
#define  ANATOP_PLLOUT_CTL_CKE				(1 << 4)
#define  ANATOP_PLLOUT_CTL_SEL_SYSPLL1			0xb
#define  ANATOP_PLLOUT_CTL_SEL_MASK			0xf
#define ANATOP_PLLOUT_DIV			0x7c
#define  ANATOP_PLLOUT_DIV_SYSPLL1			0x7

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct dwpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct dwpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_cfg0_ioh;
	bus_space_handle_t	sc_cfg1_ioh;

	bus_addr_t		sc_cfg0_base;
	bus_size_t		sc_cfg0_size;
	bus_addr_t		sc_cfg1_base;
	bus_size_t		sc_cfg1_size;
	bus_addr_t		sc_io_base;
	bus_addr_t		sc_io_bus_addr;
	bus_size_t		sc_io_size;
	bus_addr_t		sc_mem_base;
	bus_addr_t		sc_mem_bus_addr;
	bus_size_t		sc_mem_size;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct dwpcie_range	*sc_ranges;
	int			sc_nranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;

	struct arm64_pci_chipset sc_pc;
	int			sc_bus;

	int			sc_num_viewport;
	bus_addr_t		sc_atu_base;
	int			sc_atu_unroll;

	void			*sc_ih;
};

int dwpcie_match(struct device *, void *, void *);
void dwpcie_attach(struct device *, struct device *, void *);

struct cfattach	dwpcie_ca = {
	sizeof (struct dwpcie_softc), dwpcie_match, dwpcie_attach
};

struct cfdriver dwpcie_cd = {
	NULL, "dwpcie", DV_DULL
};

int
dwpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "marvell,armada8k-pcie") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-pcie"));
}

void	dwpcie_atu_config(struct dwpcie_softc *, int, int,
	    uint64_t, uint64_t, uint64_t);
void	dwpcie_link_config(struct dwpcie_softc *);
int	dwpcie_link_up(struct dwpcie_softc *);

void	dwpcie_armada8k_init(struct dwpcie_softc *);
int	dwpcie_armada8k_link_up(struct dwpcie_softc *);
int	dwpcie_armada8k_intr(void *);

void	dwpcie_imx8mq_init(struct dwpcie_softc *);
int	dwpcie_imx8mq_intr(void *);

void	dwpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	dwpcie_bus_maxdevs(void *, int);
pcitag_t dwpcie_make_tag(void *, int, int, int);
void	dwpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	dwpcie_conf_size(void *, pcitag_t);
pcireg_t dwpcie_conf_read(void *, pcitag_t, int);
void	dwpcie_conf_write(void *, pcitag_t, int, pcireg_t);

int	dwpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int	dwpcie_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int	dwpcie_intr_map_msix(struct pci_attach_args *, int,
	    pci_intr_handle_t *);
const char *dwpcie_intr_string(void *, pci_intr_handle_t);
void	*dwpcie_intr_establish(void *, pci_intr_handle_t, int,
	    int (*)(void *), void *, char *);
void	dwpcie_intr_disestablish(void *, void *);

void	*dwpcie_armada8k_intr_establish(void *, pci_intr_handle_t, int,
	    int (*)(void *), void *, char *);

int	dwpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	dwpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

void
dwpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwpcie_softc *sc = (struct dwpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	bus_addr_t iobase, iolimit;
	bus_addr_t membase, memlimit;
	uint32_t bus_range[2];
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	pcireg_t bir, blr, csr;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;

	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    faa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    faa->fa_scells);
	sc->sc_pacells = faa->fa_acells;
	sc->sc_pscells = faa->fa_scells;

	rangeslen = OF_getproplen(sc->sc_node, "ranges");
	if (rangeslen <= 0 || (rangeslen % sizeof(uint32_t)) ||
	     (rangeslen / sizeof(uint32_t)) % (sc->sc_acells +
	     sc->sc_pacells + sc->sc_scells)) {
		printf(": invalid ranges property\n");
		return;
	}

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges,
	    rangeslen);

	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_ranges = mallocarray(nranges,
	    sizeof(struct dwpcie_range), M_TEMP, M_WAITOK);
	sc->sc_nranges = nranges;

	for (i = 0, j = 0; i < sc->sc_nranges; i++) {
		sc->sc_ranges[i].flags = ranges[j++];
		sc->sc_ranges[i].pci_base = ranges[j++];
		if (sc->sc_acells - 1 == 2) {
			sc->sc_ranges[i].pci_base <<= 32;
			sc->sc_ranges[i].pci_base |= ranges[j++];
		}
		sc->sc_ranges[i].phys_base = ranges[j++];
		if (sc->sc_pacells == 2) {
			sc->sc_ranges[i].phys_base <<= 32;
			sc->sc_ranges[i].phys_base |= ranges[j++];
		}
		sc->sc_ranges[i].size = ranges[j++];
		if (sc->sc_scells == 2) {
			sc->sc_ranges[i].size <<= 32;
			sc->sc_ranges[i].size |= ranges[j++];
		}
	}

	free(ranges, M_TEMP, rangeslen);

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map ctrl registers\n");
		return;
	}

	sc->sc_cfg0_base = faa->fa_reg[1].addr;
	sc->sc_cfg0_size = faa->fa_reg[1].size / 2;
	sc->sc_cfg0_base = faa->fa_reg[1].addr + sc->sc_cfg0_size;
	sc->sc_cfg1_size = sc->sc_cfg0_size;

	if (bus_space_map(sc->sc_iot, sc->sc_cfg0_base,
	    sc->sc_cfg1_size, 0, &sc->sc_cfg0_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		printf(": can't map config registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, sc->sc_cfg1_base,
	    sc->sc_cfg1_size, 0, &sc->sc_cfg1_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_cfg0_ioh, sc->sc_cfg0_size);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		printf(": can't map config registers\n");
		return;
	}

	sc->sc_num_viewport = OF_getpropint(sc->sc_node, "num-viewport", 2);

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");

	clock_set_assigned(sc->sc_node);

	if (OF_is_compatible(sc->sc_node, "marvell,armada8k-pcie"))
		dwpcie_armada8k_init(sc);
	if (OF_is_compatible(sc->sc_node, "fsl,imx8mq-pcie"))
		dwpcie_imx8mq_init(sc);

	if (HREAD4(sc, IATU_VIEWPORT) == 0xffffffff) {
		sc->sc_atu_base = 0x300000;
		sc->sc_atu_unroll = 1;
	}

	/* Set up address translation for I/O space. */
	sc->sc_io_bus_addr = sc->sc_mem_bus_addr = -1;
	for (i = 0; i < sc->sc_nranges; i++) {
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000 &&
		    sc->sc_ranges[i].size > 0) {
			sc->sc_io_base = sc->sc_ranges[i].phys_base;
			sc->sc_io_bus_addr = sc->sc_ranges[i].pci_base;
			sc->sc_io_size = sc->sc_ranges[i].size;
		}
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000 &&
		    sc->sc_ranges[i].size > 0) {
			sc->sc_mem_base = sc->sc_ranges[i].phys_base;
			sc->sc_mem_bus_addr = sc->sc_ranges[i].pci_base;
			sc->sc_mem_size = sc->sc_ranges[i].size;
		}
	}

	dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX0,
	    IATU_REGION_CTRL_1_TYPE_MEM, sc->sc_mem_base,
	    sc->sc_mem_bus_addr, sc->sc_mem_size);
	if (sc->sc_num_viewport > 2)
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX2,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);

	/* Enable modification of read-only bits. */
	HSET4(sc, MISC_CONTROL_1, MISC_CONTROL_1_DBI_RO_WR_EN);

	/* A Root Port is a PCI-PCI Bridge. */
	HWRITE4(sc, PCI_CLASS_REG,
	    PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
	    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT);

	/* Clear BAR as U-Boot seems to leave garbage in it. */
	HWRITE4(sc, PCI_MAPREG_START, PCI_MAPREG_MEM_TYPE_64BIT);
	HWRITE4(sc, PCI_MAPREG_START + 4, 0);

	/* Make sure read-only bits are write-protected. */
	HCLR4(sc, MISC_CONTROL_1, MISC_CONTROL_1_DBI_RO_WR_EN);

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range) ||
	    bus_range[0] >= 32 || bus_range[1] >= 32) {
		bus_range[0] = 0;
		bus_range[1] = 31;
	}
	sc->sc_bus = bus_range[0];

	/* Initialize bus range. */
	bir = bus_range[0];
	bir |= ((bus_range[0] + 1) << 8);
	bir |= (bus_range[1] << 16);
	HWRITE4(sc, PPB_REG_BUSINFO, bir);

	/* Initialize I/O window. */
	iobase = sc->sc_io_bus_addr;
	iolimit = iobase + sc->sc_io_size - 1;
	blr = iolimit & PPB_IO_MASK;
	blr |= (iobase >> PPB_IO_SHIFT);
	HWRITE4(sc, PPB_REG_IOSTATUS, blr);
	blr = (iobase & 0xffff0000) >> 16;
	blr |= iolimit & 0xffff0000;
	HWRITE4(sc, PPB_REG_IO_HI, blr);

	/* Initialize memory mapped I/O window. */
	membase = sc->sc_mem_bus_addr;
	memlimit = membase + sc->sc_mem_size - 1;
	blr = memlimit & PPB_MEM_MASK;
	blr |= (membase >> PPB_MEM_SHIFT);
	HWRITE4(sc, PPB_REG_MEM, blr);

	/* Reset prefetchable memory mapped I/O window. */
	HWRITE4(sc, PPB_REG_PREFMEM, 0x0000ffff);
	HWRITE4(sc, PPB_REG_PREFBASE_HI32, 0);
	HWRITE4(sc, PPB_REG_PREFLIM_HI32, 0);

	csr = PCI_COMMAND_MASTER_ENABLE;
	if (iolimit > iobase)
		csr |= PCI_COMMAND_IO_ENABLE;
	if (memlimit > membase)
		csr |= PCI_COMMAND_MEM_ENABLE;
	HWRITE4(sc, PCI_COMMAND_STATUS_REG, csr);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = dwpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = dwpcie_bs_memmap;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = dwpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = dwpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = dwpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = dwpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = dwpcie_conf_size;
	sc->sc_pc.pc_conf_read = dwpcie_conf_read;
	sc->sc_pc.pc_conf_write = dwpcie_conf_write;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = dwpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = dwpcie_intr_map_msi;
	sc->sc_pc.pc_intr_map_msix = dwpcie_intr_map_msix;
	sc->sc_pc.pc_intr_string = dwpcie_intr_string;
	sc->sc_pc.pc_intr_establish = dwpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = dwpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = faa->fa_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
	if (OF_is_compatible(faa->fa_node, "marvell,armada8k-pcie"))
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

void
dwpcie_link_config(struct dwpcie_softc *sc)
{
	uint32_t mode, width, reg;
	int lanes;

	lanes = OF_getpropint(sc->sc_node, "num-lanes", 0);

	switch (lanes) {
	case 1:
		mode = PCIE_PORT_LINK_CTRL_LANES_1;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_1;
		break;
	case 2:
		mode = PCIE_PORT_LINK_CTRL_LANES_2;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_2;
		break;
	case 4:
		mode = PCIE_PORT_LINK_CTRL_LANES_4;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_4;
		break;
	case 8:
		mode = PCIE_PORT_LINK_CTRL_LANES_8;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_8;
		break;
	default:
		printf("%s: %d lanes not supported\n", __func__, lanes);
		return;
	}

	reg = HREAD4(sc, PCIE_PORT_LINK_CTRL);
	reg &= ~PCIE_PORT_LINK_CTRL_LANES_MASK;
	reg |= mode;
	HWRITE4(sc, PCIE_PORT_LINK_CTRL, reg);

	reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
	reg &= ~PCIE_LINK_WIDTH_SPEED_CTRL_LANES_MASK;
	reg |= width;
	HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);

	reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
	reg |= PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE;
	HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);
}

void
dwpcie_armada8k_init(struct dwpcie_softc *sc)
{
	uint32_t reg;
	int timo;

	clock_enable_all(sc->sc_node);

	dwpcie_link_config(sc);

	if (!dwpcie_armada8k_link_up(sc)) {
		reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
		reg &= ~PCIE_GLOBAL_CTRL_APP_LTSSM_EN;
		HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);
	}

	/* Enable Root Complex mode. */
	reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
	reg &= ~PCIE_GLOBAL_CTRL_DEVICE_TYPE_MASK;
	reg |= PCIE_GLOBAL_CTRL_DEVICE_TYPE_RC;
	HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);

	HWRITE4(sc, PCIE_ARCACHE_TRC, PCIE_ARCACHE_TRC_DEFAULT);
	HWRITE4(sc, PCIE_AWCACHE_TRC, PCIE_AWCACHE_TRC_DEFAULT);
	reg = HREAD4(sc, PCIE_ARUSER);
	reg &= ~PCIE_AXUSER_DOMAIN_MASK;
	reg |= PCIE_AXUSER_DOMAIN_OUTER_SHARABLE;
	HWRITE4(sc, PCIE_ARUSER, reg);
	reg = HREAD4(sc, PCIE_AWUSER);
	reg &= ~PCIE_AXUSER_DOMAIN_MASK;
	reg |= PCIE_AXUSER_DOMAIN_OUTER_SHARABLE;
	HWRITE4(sc, PCIE_AWUSER, reg);

	if (!dwpcie_armada8k_link_up(sc)) {
		reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
		reg |= PCIE_GLOBAL_CTRL_APP_LTSSM_EN;
		HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);
	}

	for (timo = 40; timo > 0; timo--) {
		if (dwpcie_armada8k_link_up(sc))
			break;
		delay(1000);
	}

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_AUDIO | IPL_MPSAFE,
	    dwpcie_armada8k_intr, sc, sc->sc_dev.dv_xname);

	/* Unmask INTx interrupts. */
	HWRITE4(sc, PCIE_GLOBAL_INT_MASK,
	    PCIE_GLOBAL_INT_MASK_INT_A | PCIE_GLOBAL_INT_MASK_INT_B |
	    PCIE_GLOBAL_INT_MASK_INT_C | PCIE_GLOBAL_INT_MASK_INT_D);
}

int
dwpcie_armada8k_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg, mask;

	mask = PCIE_GLOBAL_STATUS_RDLH_LINK_UP;
	mask |= PCIE_GLOBAL_STATUS_PHY_LINK_UP;
	reg = HREAD4(sc, PCIE_GLOBAL_STATUS);
	return ((reg & mask) == mask);
}

int
dwpcie_armada8k_intr(void *arg)
{
	struct dwpcie_softc *sc = arg;
	uint32_t cause;

	/* Acknowledge interrupts. */
	cause = HREAD4(sc, PCIE_GLOBAL_INT_CAUSE);
	HWRITE4(sc, PCIE_GLOBAL_INT_CAUSE, cause);

	/* INTx interrupt, so not really ours. */
	return 0;
}

void
dwpcie_imx8mq_init(struct dwpcie_softc *sc)
{
	uint32_t *clkreq_gpio, *disable_gpio, *reset_gpio;
	ssize_t clkreq_gpiolen, disable_gpiolen, reset_gpiolen;
	struct regmap *anatop, *gpc, *gpr;
	uint32_t off, reg;
	int timo;

	anatop = regmap_bycompatible("fsl,imx8mq-anatop");
	gpc = regmap_bycompatible("fsl,imx8mq-gpc");
	gpr = regmap_bycompatible("fsl,imx8mq-iomuxc-gpr");
	KASSERT(anatop != NULL);
	KASSERT(gpc != NULL);
	KASSERT(gpr != NULL);

	clkreq_gpiolen = OF_getproplen(sc->sc_node, "clkreq-gpio");
	disable_gpiolen = OF_getproplen(sc->sc_node, "disable-gpio");
	reset_gpiolen = OF_getproplen(sc->sc_node, "reset-gpio");

	if (clkreq_gpiolen > 0) {
		clkreq_gpio = malloc(clkreq_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "clkreq-gpio", clkreq_gpio,
		    clkreq_gpiolen);
		gpio_controller_config_pin(clkreq_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(clkreq_gpio, 1);
	}

	if (disable_gpiolen > 0) {
		disable_gpio = malloc(disable_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "disable-gpio", disable_gpio,
		    disable_gpiolen);
		gpio_controller_config_pin(disable_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(disable_gpio, 0);
	}

	if (reset_gpiolen > 0) {
		reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "reset-gpio", reset_gpio,
		    reset_gpiolen);
		gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(reset_gpio, 1);
	}

	power_domain_enable(sc->sc_node);
	reset_assert(sc->sc_node, "pciephy");
	reset_assert(sc->sc_node, "apps");

	reg = regmap_read_4(gpr, IOMUXC_GPR12);
	if (OF_getpropint(sc->sc_node, "ctrl-id", 0) == 0) {
		off = IOMUXC_GPR14;
		reg &= ~IMX8MQ_GPR_PCIE1_DEVICE_TYPE_MASK;
		reg |= IMX8MQ_GPR_PCIE1_DEVICE_TYPE_RC;
	} else {
		off = IOMUXC_GPR16;
		reg &= ~IMX8MQ_GPR_PCIE2_DEVICE_TYPE_MASK;
		reg |= IMX8MQ_GPR_PCIE2_DEVICE_TYPE_RC;
	}
	regmap_write_4(gpr, IOMUXC_GPR12, reg);

	if (OF_getproplen(sc->sc_node, "ext_osc") == 0) {
		reg = regmap_read_4(gpr, off);
		reg |= IMX8MQ_GPR_PCIE_REF_USE_PAD;
		regmap_write_4(gpr, off, reg);
	} else {
		reg = regmap_read_4(gpr, off);
		reg &= ~IMX8MQ_GPR_PCIE_REF_USE_PAD;
		regmap_write_4(gpr, off, reg);

		regmap_write_4(anatop, ANATOP_PLLOUT_CTL,
		    ANATOP_PLLOUT_CTL_CKE | ANATOP_PLLOUT_CTL_SEL_SYSPLL1);
		regmap_write_4(anatop, ANATOP_PLLOUT_DIV,
		    ANATOP_PLLOUT_DIV_SYSPLL1);
	}

	clock_enable(sc->sc_node, "pcie_phy");
	clock_enable(sc->sc_node, "pcie_bus");
	clock_enable(sc->sc_node, "pcie");

	/* Allow clocks to stabilize. */
	delay(200);

	if (reset_gpiolen > 0) {
		gpio_controller_set_pin(reset_gpio, 1);
		delay(20000);
		gpio_controller_set_pin(reset_gpio, 0);
		delay(20000);
	}

	reset_deassert(sc->sc_node, "pciephy");

	reg = HREAD4(sc, 0x100000 + PCIE_RC_LCR);
	reg &= ~PCIE_RC_LCR_L1EL_MASK;
	reg |= PCIE_RC_LCR_L1EL_64US;
	HWRITE4(sc, 0x100000 + PCIE_RC_LCR, reg);

	dwpcie_link_config(sc);

	reg = HREAD4(sc, PCIE_RC_LCR);
	reg &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
	reg |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1;
	HWRITE4(sc, PCIE_RC_LCR, reg);

	reset_deassert(sc->sc_node, "apps");

	for (timo = 20000; timo > 0; timo--) {
		if (dwpcie_link_up(sc))
			break;
		delay(10);
	}
	if (timo == 0)
		printf("%s:%d: timeout\n", __func__, __LINE__);

	if (OF_getpropint(sc->sc_node, "fsl,max-link-speed", 1) >= 2) {
		reg = HREAD4(sc, PCIE_RC_LCR);
		reg &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
		reg |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2;
		HWRITE4(sc, PCIE_RC_LCR, reg);

		reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
		reg |= PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE;
		HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);

		for (timo = 20000; timo > 0; timo--) {
			if (dwpcie_link_up(sc))
				break;
			delay(10);
		}
		if (timo == 0)
			printf("%s:%d: timeout\n", __func__, __LINE__);
	}

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_AUDIO | IPL_MPSAFE,
	    dwpcie_imx8mq_intr, sc, sc->sc_dev.dv_xname);

	/* Unmask INTx interrupts. */
	HWRITE4(sc, PCIE_GLOBAL_INT_MASK,
	    PCIE_GLOBAL_INT_MASK_INT_A | PCIE_GLOBAL_INT_MASK_INT_B |
	    PCIE_GLOBAL_INT_MASK_INT_C | PCIE_GLOBAL_INT_MASK_INT_D);

	if (clkreq_gpiolen > 0)
		free(clkreq_gpio, M_TEMP, clkreq_gpiolen);
	if (disable_gpiolen > 0)
		free(disable_gpio, M_TEMP, disable_gpiolen);
	if (reset_gpiolen > 0)
		free(reset_gpio, M_TEMP, reset_gpiolen);
}

int
dwpcie_imx8mq_intr(void *arg)
{
	struct dwpcie_softc *sc = arg;
	uint32_t cause;

	/* Acknowledge interrupts. */
	cause = HREAD4(sc, PCIE_GLOBAL_INT_CAUSE);
	HWRITE4(sc, PCIE_GLOBAL_INT_CAUSE, cause);

	/* INTx interrupt, so not really ours. */
	return 0;
}

void
dwpcie_atu_config(struct dwpcie_softc *sc, int index, int type,
    uint64_t cpu_addr, uint64_t pci_addr, uint64_t size)
{
	uint32_t reg, off;
	int timo;

	off = sc->sc_atu_base + IATU_OFFSET_UNROLL(index);
	if (!sc->sc_atu_unroll) {
		off = IATU_OFFSET_VIEWPORT;
		HWRITE4(sc, IATU_VIEWPORT, index);
	}

	HWRITE4(sc, off + IATU_LWR_BASE_ADDR, cpu_addr);
	HWRITE4(sc, off + IATU_UPPER_BASE_ADDR, cpu_addr >> 32);
	HWRITE4(sc, off + IATU_LIMIT_ADDR, cpu_addr + size - 1);
	HWRITE4(sc, off + IATU_LWR_TARGET_ADDR, pci_addr);
	HWRITE4(sc, off + IATU_UPPER_TARGET_ADDR, pci_addr >> 32);
	HWRITE4(sc, off + IATU_REGION_CTRL_1, type);
	HWRITE4(sc, off + IATU_REGION_CTRL_2, IATU_REGION_CTRL_2_REGION_EN);

	for (timo = 5; timo > 0; timo--) {
		reg = HREAD4(sc, off + IATU_REGION_CTRL_2);
		if (reg & IATU_REGION_CTRL_2_REGION_EN)
			break;
		delay(9000);
	}
	if (timo == 0)
		printf("%s:%d: timeout\n", __func__, __LINE__);
}

int
dwpcie_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, PCIE_PHY_DEBUG_R1);
	if ((reg & PCIE_PHY_DEBUG_R1_XMLH_LINK_UP) != 0 &&
	    (reg & PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING) == 0)
		return 1;
	return 0;
}

void
dwpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
dwpcie_bus_maxdevs(void *v, int bus)
{
	struct dwpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

pcitag_t
dwpcie_make_tag(void *v, int bus, int device, int function)
{
	return ((bus << 24) | (device << 19) | (function << 16));
}

void
dwpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 24) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 19) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 16) & 0x7;
}

int
dwpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
dwpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct dwpcie_softc *sc = v;
	int bus, dev, fn;
	uint32_t ret;

	dwpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		return HREAD4(sc, tag | reg);
	}

	if (bus == sc->sc_bus + 1) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG0,
		    sc->sc_cfg0_base, tag, sc->sc_cfg0_size);
		ret = bus_space_read_4(sc->sc_iot, sc->sc_cfg0_ioh, reg);
	} else {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG1,
		    sc->sc_cfg1_base, tag, sc->sc_cfg1_size);
		ret = bus_space_read_4(sc->sc_iot, sc->sc_cfg1_ioh, reg);
	}
	if (sc->sc_num_viewport <= 2)
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);

	return ret;
}

void
dwpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct dwpcie_softc *sc = v;
	int bus, dev, fn;

	dwpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		HWRITE4(sc, tag | reg, data);
		return;
	}

	if (bus == sc->sc_bus + 1) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG0,
		    sc->sc_cfg0_base, tag, sc->sc_cfg0_size);
		bus_space_write_4(sc->sc_iot, sc->sc_cfg0_ioh, reg, data);
	} else {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG1,
		    sc->sc_cfg1_base, tag, sc->sc_cfg1_size);
		bus_space_write_4(sc->sc_iot, sc->sc_cfg1_ioh, reg, data);
	}
	if (sc->sc_num_viewport <= 2)
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);
}

struct dwpcie_intr_handle {
	pci_chipset_tag_t	ih_pc;
	pcitag_t		ih_tag;
	int			ih_intrpin;
	int			ih_msi;
};

int
dwpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct dwpcie_intr_handle *ih;
	int pin = pa->pa_rawintrpin;

	if (pin == 0 || pin > PCI_INTERRUPT_PIN_MAX)
		return -1;

	if (pa->pa_tag == 0)
		return -1;

	ih = malloc(sizeof(struct dwpcie_intr_handle), M_DEVBUF, M_WAITOK);
	ih->ih_pc = pa->pa_pc;
	ih->ih_tag = pa->pa_intrtag;
	ih->ih_intrpin = pa->pa_intrpin;
	ih->ih_msi = 0;
	*ihp = (pci_intr_handle_t)ih;

	return 0;
}

int
dwpcie_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	struct dwpcie_intr_handle *ih;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, NULL, NULL) == 0)
		return -1;

	ih = malloc(sizeof(struct dwpcie_intr_handle), M_DEVBUF, M_WAITOK);
	ih->ih_pc = pa->pa_pc;
	ih->ih_tag = pa->pa_tag;
	ih->ih_intrpin = pa->pa_intrpin;
	ih->ih_msi = 1;
	*ihp = (pci_intr_handle_t)ih;

	return 0;
}

int
dwpcie_intr_map_msix(struct pci_attach_args *pa, int vec,
    pci_intr_handle_t *ihp)
{
	return -1;
}

const char *
dwpcie_intr_string(void *v, pci_intr_handle_t ihp)
{
	struct dwpcie_intr_handle *ih = (struct dwpcie_intr_handle *)ihp;

	if (ih->ih_msi)
		return "msi";

	return "intx";
}

void *
dwpcie_intr_establish(void *v, pci_intr_handle_t ihp, int level,
    int (*func)(void *), void *arg, char *name)
{
	struct dwpcie_softc *sc = v;
	struct dwpcie_intr_handle *ih = (struct dwpcie_intr_handle *)ihp;
	void *cookie;

	if (ih->ih_msi) {
		uint64_t addr, data;
		pcireg_t reg;
		int off;

		/* Assume hardware passes Requester ID as sideband data. */
		data = pci_requester_id(ih->ih_pc, ih->ih_tag);
		cookie = fdt_intr_establish_msi(sc->sc_node, &addr,
		    &data, level, func, arg, (void *)name);
		if (cookie == NULL)
			return NULL;

		/* TODO: translate address to the PCI device's view */

		if (pci_get_capability(ih->ih_pc, ih->ih_tag, PCI_CAP_MSI,
		    &off, &reg) == 0)
			panic("%s: no msi capability", __func__);

		if (reg & PCI_MSI_MC_C64) {
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MA, addr);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MAU32, addr >> 32);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MD64, data);
		} else {
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MA, addr);
			pci_conf_write(ih->ih_pc, ih->ih_tag,
			    off + PCI_MSI_MD32, data);
		}
		pci_conf_write(ih->ih_pc, ih->ih_tag,
		    off, reg | PCI_MSI_MC_MSIE);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		dwpcie_decompose_tag(sc, ih->ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih->ih_intrpin;

		cookie = fdt_intr_establish_imap(sc->sc_node, reg,
		    sizeof(reg), level, func, arg, name);
	}

	free(ih, M_DEVBUF, sizeof(struct dwpcie_intr_handle));
	return cookie;
}

void
dwpcie_intr_disestablish(void *v, void *cookie)
{
	panic("%s", __func__);
}

int
dwpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct dwpcie_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}

int
dwpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct dwpcie_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}