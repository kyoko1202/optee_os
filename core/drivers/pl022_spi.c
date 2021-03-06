/*
 * Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <assert.h>
#include <drivers/pl022_spi.h>
#include <gpio.h>
#include <initcall.h>
#include <io.h>
#include <kernel/panic.h>
#include <kernel/tee_time.h>
#include <platform_config.h>
#include <trace.h>
#include <util.h>

/* SPI register offsets */
#define SSPCR0		0x000
#define SSPCR1		0x004
#define SSPDR		0x008
#define SSPSR		0x00C
#define SSPCPSR		0x010
#define SSPIMSC		0x014
#define SSPRIS		0x018
#define SSPMIS		0x01C
#define SSPICR		0x020
#define SSPDMACR	0x024

#ifdef PLATFORM_hikey
/* HiKey extensions */
#define SSPTXFIFOCR	0x028
#define SSPRXFIFOCR	0x02C
#define SSPB2BTRANS	0x030
#endif

/* test registers */
#define SSPTCR		0x080
#define SSPITIP		0x084
#define SSPITOP		0x088
#define SSPTDR		0x08C

#define SSPPeriphID0	0xFE0
#define SSPPeriphID1	0xFE4
#define SSPPeriphID2	0xFE8
#define SSPPeriphID3	0xFEC

#define SSPPCellID0	0xFF0
#define SSPPCellID1	0xFF4
#define SSPPCellID2	0xFF8
#define SSPPCellID3	0xFFC

/* SPI register masks */
#define SSPCR0_SCR		SHIFT_U32(0xFF, 8)
#define SSPCR0_SPH		SHIFT_U32(1, 7)
#define SSPCR0_SPH1		SHIFT_U32(1, 7)
#define SSPCR0_SPH0		SHIFT_U32(0, 7)
#define SSPCR0_SPO		SHIFT_U32(1, 6)
#define SSPCR0_SPO1		SHIFT_U32(1, 6)
#define SSPCR0_SPO0		SHIFT_U32(0, 6)
#define SSPCR0_FRF		SHIFT_U32(3, 4)
#define SSPCR0_FRF_SPI		SHIFT_U32(0, 4)
#define SSPCR0_DSS		SHIFT_U32(0xFF, 0)
#define SSPCR0_DSS_16BIT	SHIFT_U32(0xF, 0)
#define SSPCR0_DSS_8BIT		SHIFT_U32(7, 0)

#define SSPCR1_SOD		SHIFT_U32(1, 3)
#define SSPCR1_SOD_ENABLE	SHIFT_U32(1, 3)
#define SSPCR1_SOD_DISABLE	SHIFT_U32(0, 3)
#define SSPCR1_MS		SHIFT_U32(1, 2)
#define SSPCR1_MS_SLAVE		SHIFT_U32(1, 2)
#define SSPCR1_MS_MASTER	SHIFT_U32(0, 2)
#define SSPCR1_SSE		SHIFT_U32(1, 1)
#define SSPCR1_SSE_ENABLE	SHIFT_U32(1, 1)
#define SSPCR1_SSE_DISABLE	SHIFT_U32(0, 1)
#define SSPCR1_LBM		SHIFT_U32(1, 0)
#define SSPCR1_LBM_YES		SHIFT_U32(1, 0)
#define SSPCR1_LBM_NO		SHIFT_U32(0, 0)

#define SSPDR_DATA	SHIFT_U32(0xFFFF, 0)

#define SSPSR_BSY	SHIFT_U32(1, 4)
#define SSPSR_RNF	SHIFT_U32(1, 3)
#define SSPSR_RNE	SHIFT_U32(1, 2)
#define SSPSR_TNF	SHIFT_U32(1, 1)
#define SSPSR_TFE	SHIFT_U32(1, 0)

#define SSPCPSR_CPSDVR	SHIFT_U32(0xFF, 0)

#define SSPIMSC_TXIM	SHIFT_U32(1, 3)
#define SSPIMSC_RXIM	SHIFT_U32(1, 2)
#define SSPIMSC_RTIM	SHIFT_U32(1, 1)
#define SSPIMSC_RORIM	SHIFT_U32(1, 0)

#define SSPRIS_TXRIS	SHIFT_U32(1, 3)
#define SSPRIS_RXRIS	SHIFT_U32(1, 2)
#define SSPRIS_RTRIS	SHIFT_U32(1, 1)
#define SSPRIS_RORRIS	SHIFT_U32(1, 0)

#define SSPMIS_TXMIS	SHIFT_U32(1, 3)
#define SSPMIS_RXMIS	SHIFT_U32(1, 2)
#define SSPMIS_RTMIS	SHIFT_U32(1, 1)
#define SSPMIS_RORMIS	SHIFT_U32(1, 0)

#define SSPICR_RTIC	SHIFT_U32(1, 1)
#define SSPICR_RORIC	SHIFT_U32(1, 0)

#define SSPDMACR_TXDMAE	SHIFT_U32(1, 1)
#define SSPDMACR_RXDMAE	SHIFT_U32(1, 0)

#define SSPPeriphID0_PartNumber0	SHIFT_U32(0xFF, 0) /* 0x22 */
#define SSPPeriphID1_Designer0		SHIFT_U32(0xF, 4) /* 0x1 */
#define SSPPeriphID1_PartNumber1	SHIFT_U32(0xF, 0) /* 0x0 */
#define SSPPeriphID2_Revision		SHIFT_U32(0xF, 4)
#define SSPPeriphID2_Designer1		SHIFT_U32(0xF, 0) /* 0x4 */
#define SSPPeriphID3_Configuration	SHIFT_U32(0xFF, 0) /* 0x00 */

#define SSPPCellID_0	SHIFT_U32(0xFF, 0) /* 0x0D */
#define SSPPCellID_1	SHIFT_U32(0xFF, 0) /* 0xF0 */
#define SSPPPCellID_2	SHIFT_U32(0xFF, 0) /* 0x05 */
#define SSPPPCellID_3	SHIFT_U32(0xFF, 0) /* 0xB1 */

#define MASK_32 0xFFFFFFFF
#define MASK_28 0xFFFFFFF
#define MASK_24 0xFFFFFF
#define MASK_20 0xFFFFF
#define MASK_16 0xFFFF
#define MASK_12 0xFFF
#define MASK_8 0xFF
#define MASK_4 0xF
/* SPI register masks */

#define SSP_CPSDVR_MAX		254
#define SSP_CPSDVR_MIN		2
#define SSP_SCR_MAX		255
#define SSP_SCR_MIN		0
#define SSP_DATASIZE_MAX	16

enum pl022_data_size {
	PL022_DATA_SIZE4 = 0x3,
	PL022_DATA_SIZE5,
	PL022_DATA_SIZE6,
	PL022_DATA_SIZE7,
	PL022_DATA_SIZE8 = SSPCR0_DSS_8BIT,
	PL022_DATA_SIZE9,
	PL022_DATA_SIZE10,
	PL022_DATA_SIZE11,
	PL022_DATA_SIZE12,
	PL022_DATA_SIZE13,
	PL022_DATA_SIZE14,
	PL022_DATA_SIZE15,
	PL022_DATA_SIZE16 = SSPCR0_DSS_16BIT
};

enum pl022_spi_mode {
	PL022_SPI_MODE0 = SSPCR0_SPO0 | SSPCR0_SPH0, /* 0x00 */
	PL022_SPI_MODE1 = SSPCR0_SPO0 | SSPCR0_SPH1, /* 0x80 */
	PL022_SPI_MODE2 = SSPCR0_SPO1 | SSPCR0_SPH0, /* 0x40 */
	PL022_SPI_MODE3 = SSPCR0_SPO1 | SSPCR0_SPH1  /* 0xC0 */
};

static void pl022_txrx8(struct spi_chip *chip, uint8_t *wdat,
	uint8_t *rdat, size_t num_txpkts, size_t *num_rxpkts)
{
	size_t i = 0;
	size_t j = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	while (i < num_txpkts) {
		if (read8(pd->base + SSPSR) & SSPSR_TNF)
			/* tx 1 packet */
			write8(wdat[i++], pd->base + SSPDR);
	}

	do {
		while ((read8(pd->base + SSPSR) & SSPSR_RNE) &&
			(j < *num_rxpkts))
			/* rx 1 packet */
			rdat[j++] = read8(pd->base + SSPDR);
	} while ((read8(pd->base + SSPSR) & SSPSR_BSY) && (j < *num_rxpkts));

	*num_rxpkts = j;

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_txrx16(struct spi_chip *chip, uint16_t *wdat,
	uint16_t *rdat, size_t num_txpkts, size_t *num_rxpkts)
{
	size_t i = 0;
	size_t j = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	while (i < num_txpkts) {
		if (read8(pd->base + SSPSR) & SSPSR_TNF)
			/* tx 1 packet */
			write16(wdat[i++], pd->base + SSPDR);
	}

	do {
		while ((read8(pd->base + SSPSR) & SSPSR_RNE)
			&& (j < *num_rxpkts))
			/* rx 1 packet */
			rdat[j++] = read16(pd->base + SSPDR);
	} while ((read8(pd->base + SSPSR) & SSPSR_BSY) && (j < *num_rxpkts));

	*num_rxpkts = j;

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_tx8(struct spi_chip *chip, uint8_t *wdat,
	size_t num_txpkts)
{
	size_t i = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	while (i < num_txpkts) {
		if (read8(pd->base + SSPSR) & SSPSR_TNF)
			/* tx 1 packet */
			write8(wdat[i++], pd->base + SSPDR);
	}

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_tx16(struct spi_chip *chip, uint16_t *wdat,
	size_t num_txpkts)
{
	size_t i = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	while (i < num_txpkts) {
		if (read8(pd->base + SSPSR) & SSPSR_TNF)
			/* tx 1 packet */
			write16(wdat[i++], pd->base + SSPDR);
	}

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_rx8(struct spi_chip *chip, uint8_t *rdat,
	size_t *num_rxpkts)
{
	size_t j = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	do {
		while ((read8(pd->base + SSPSR) & SSPSR_RNE) &&
			(j < *num_rxpkts))
			/* rx 1 packet */
			rdat[j++] = read8(pd->base + SSPDR);
	} while ((read8(pd->base + SSPSR) & SSPSR_BSY) && (j < *num_rxpkts));

	*num_rxpkts = j;

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_rx16(struct spi_chip *chip, uint16_t *rdat,
	size_t *num_rxpkts)
{
	size_t j = 0;
	struct pl022_data *pd = container_of(chip, struct pl022_data, chip);

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_LOW);

	do {
		while ((read8(pd->base + SSPSR) & SSPSR_RNE) &&
			(j < *num_rxpkts))
			/* rx 1 packet */
			rdat[j++] = read16(pd->base + SSPDR);
	} while ((read8(pd->base + SSPSR) & SSPSR_BSY) && (j < *num_rxpkts));

	*num_rxpkts = j;

	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

static void pl022_print_peri_id(struct pl022_data *pd __maybe_unused)
{
	DMSG("Expected: 0x 22 10 ?4 00");
	DMSG("Read: 0x %02x %02x %02x %02x",
		read32(pd->base + SSPPeriphID0),
		read32(pd->base + SSPPeriphID1),
		read32(pd->base + SSPPeriphID2),
		read32(pd->base + SSPPeriphID3));
}

static void pl022_print_cell_id(struct pl022_data *pd __maybe_unused)
{
	DMSG("Expected: 0x 0d f0 05 b1");
	DMSG("Read: 0x %02x %02x %02x %02x",
		read32(pd->base + SSPPCellID0),
		read32(pd->base + SSPPCellID1),
		read32(pd->base + SSPPCellID2),
		read32(pd->base + SSPPCellID3));
}

static void pl022_sanity_check(struct pl022_data *pd)
{
	assert(pd);
	assert(pd->chip.ops);
	assert(pd->base);
	assert(pd->cs_gpio_base);
	assert(pd->clk_hz);
	assert(pd->speed_hz && pd->speed_hz <= pd->clk_hz/2);
	assert(pd->mode <= SPI_MODE3);
	assert(pd->data_size_bits == 8 || pd->data_size_bits == 16);

	#ifdef PLATFORM_hikey
	DMSG("SSPB2BTRANS: Expected: 0x2. Read: 0x%x",
		read32(pd->base + SSPB2BTRANS));
	#endif
	pl022_print_peri_id(pd);
	pl022_print_cell_id(pd);
}

static inline uint32_t pl022_calc_freq(struct pl022_data *pd,
	uint8_t cpsdvr, uint8_t scr)
{
	return pd->clk_hz / (cpsdvr * (1 + scr));
}

static void pl022_calc_clk_divisors(struct pl022_data *pd,
	uint8_t *cpsdvr, uint8_t *scr)
{
	unsigned int freq1 = 0;
	unsigned int freq2 = 0;
	uint8_t tmp_cpsdvr1;
	uint8_t tmp_scr1;
	uint8_t tmp_cpsdvr2 = 0;
	uint8_t tmp_scr2 = 0;

	for (tmp_scr1 = SSP_SCR_MIN; tmp_scr1 < SSP_SCR_MAX; tmp_scr1++) {
		for (tmp_cpsdvr1 = SSP_CPSDVR_MIN; tmp_cpsdvr1 < SSP_CPSDVR_MAX;
			tmp_cpsdvr1++) {
			freq1 = pl022_calc_freq(pd, tmp_cpsdvr1, tmp_scr1);
			if (freq1 == pd->speed_hz)
				goto done;
			else if (freq1 < pd->speed_hz)
				goto stage2;
		}
	}

stage2:
	for (tmp_cpsdvr2 = SSP_CPSDVR_MIN; tmp_cpsdvr2 < SSP_CPSDVR_MAX;
		tmp_cpsdvr2++) {
		for (tmp_scr2 = SSP_SCR_MIN; tmp_scr2 < SSP_SCR_MAX;
			tmp_scr2++) {
			freq2 = pl022_calc_freq(pd, tmp_cpsdvr2, tmp_scr2);
			if (freq2 <= pd->speed_hz)
				goto done;
		}
	}

done:
	if (freq1 >= freq2) {
		*cpsdvr = tmp_cpsdvr1;
		*scr = tmp_scr1;
		DMSG("speed: requested: %u, closest1: %u",
			pd->speed_hz, freq1);
	} else {
		*cpsdvr = tmp_cpsdvr2;
		*scr = tmp_scr2;
		DMSG("speed: requested: %u, closest2: %u",
			pd->speed_hz, freq2);
	}
	DMSG("CPSDVR: %u (0x%x), SCR: %u (0x%x)",
		*cpsdvr, *cpsdvr, *scr, *scr);
}

static void pl022_flush_fifo(struct pl022_data *pd)
{
	uint32_t __maybe_unused rdat;

	do {
		while (read32(pd->base + SSPSR) & SSPSR_RNE) {
			rdat = read32(pd->base + SSPDR);
			DMSG("rdat: 0x%x", rdat);
		}
	} while (read32(pd->base + SSPSR) & SSPSR_BSY);
}

static const struct spi_ops pl022_ops = {
	.txrx8 = pl022_txrx8,
	.txrx16 = pl022_txrx16,
	.tx8 = pl022_tx8,
	.tx16 = pl022_tx16,
	.rx8 = pl022_rx8,
	.rx16 = pl022_rx16,
};

void pl022_configure(struct pl022_data *pd)
{
	uint16_t mode;
	uint16_t data_size;
	uint8_t cpsdvr;
	uint8_t scr;
	uint8_t lbm;

	pd->chip.ops = &pl022_ops;
	pl022_sanity_check(pd);
	pl022_calc_clk_divisors(pd, &cpsdvr, &scr);

	/* configure ssp based on platform settings */
	switch (pd->mode) {
	case SPI_MODE0:
		DMSG("SPI_MODE0");
		mode = PL022_SPI_MODE0;
		break;
	case SPI_MODE1:
		DMSG("SPI_MODE1");
		mode = PL022_SPI_MODE1;
		break;
	case SPI_MODE2:
		DMSG("SPI_MODE2");
		mode = PL022_SPI_MODE2;
		break;
	case SPI_MODE3:
		DMSG("SPI_MODE3");
		mode = PL022_SPI_MODE3;
		break;
	default:
		EMSG("Invalid SPI mode: %u", pd->mode);
		panic();
	}

	switch (pd->data_size_bits) {
	case 8:
		DMSG("Data size: 8");
		data_size = PL022_DATA_SIZE8;
		break;
	case 16:
		DMSG("Data size: 16");
		data_size = PL022_DATA_SIZE16;
		break;
	default:
		EMSG("Unsupported data size: %u bits", pd->data_size_bits);
		panic();
	}

	if (pd->loopback) {
		DMSG("Starting in loopback mode!");
		lbm = SSPCR1_LBM_YES;
	} else {
		DMSG("Starting in regular (non-loopback) mode!");
		lbm = SSPCR1_LBM_NO;
	}

	DMSG("set Serial Clock Rate (SCR), SPI mode (phase and clock)");
	DMSG("set frame format (SPI) and data size (8- or 16-bit)");
	io_mask16(pd->base + SSPCR0, SHIFT_U32(scr, 8) | mode | SSPCR0_FRF_SPI |
		data_size, MASK_16);

	DMSG("set master mode, disable SSP, set loopback mode");
	io_mask8(pd->base + SSPCR1, SSPCR1_SOD_DISABLE | SSPCR1_MS_MASTER |
		SSPCR1_SSE_DISABLE | lbm, MASK_4);

	DMSG("set clock prescale");
	io_mask8(pd->base + SSPCPSR, cpsdvr, SSPCPSR_CPSDVR);

	DMSG("disable interrupts");
	io_mask8(pd->base + SSPIMSC, 0, MASK_4);

	DMSG("set CS GPIO dir to out");
	gpio_set_direction(pd->cs_gpio_pin, GPIO_DIR_OUT);

	DMSG("pull CS high");
	gpio_set_value(pd->cs_gpio_pin, GPIO_LEVEL_HIGH);
}

void pl022_start(struct pl022_data *pd)
{
	DMSG("empty FIFO before starting");
	pl022_flush_fifo(pd);

	DMSG("enable SSP");
	io_mask8(pd->base + SSPCR1, SSPCR1_SSE_ENABLE, SSPCR1_SSE);
}

void pl022_end(struct pl022_data *pd)
{
	/* disable ssp */
	io_mask8(pd->base + SSPCR1, SSPCR1_SSE_DISABLE, SSPCR1_SSE);
}

