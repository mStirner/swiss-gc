/***************************************************************************
* Network Read code for GC via Broadband Adapter
* Extrems 2017
***************************************************************************/

#include <stdint.h>
#include <string.h>
#include "../../reservedarea.h"
#include "../base/common.h"
#include "bba.h"

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define BBA_NCRA				0x00		/* Network Control Register A, RW */
#define BBA_NCRA_RESET			(1<<0)	/* RESET */
#define BBA_NCRA_ST0			(1<<1)	/* ST0, Start transmit command/status */
#define BBA_NCRA_ST1			(1<<2)	/* ST1,  " */
#define BBA_NCRA_SR				(1<<3)	/* SR, Start Receive */

#define BBA_IR 0x09		/* Interrupt Register, RW, 00h */
#define BBA_IR_FRAGI       (1<<0)	/* FRAGI, Fragment Counter Interrupt */
#define BBA_IR_RI          (1<<1)	/* RI, Receive Interrupt */
#define BBA_IR_TI          (1<<2)	/* TI, Transmit Interrupt */
#define BBA_IR_REI         (1<<3)	/* REI, Receive Error Interrupt */
#define BBA_IR_TEI         (1<<4)	/* TEI, Transmit Error Interrupt */
#define BBA_IR_FIFOEI      (1<<5)	/* FIFOEI, FIFO Error Interrupt */
#define BBA_IR_BUSEI       (1<<6)	/* BUSEI, BUS Error Interrupt */
#define BBA_IR_RBFI        (1<<7)	/* RBFI, RX Buffer Full Interrupt */

#define BBA_RWP  0x16/*+0x17*/	/* Receive Buffer Write Page Pointer Register */
#define BBA_RRP  0x18/*+0x19*/	/* Receive Buffer Read Page Pointer Register */

#define BBA_WRTXFIFOD 0x48/*-0x4b*/	/* Write TX FIFO Data Port Register */

#define BBA_RX_STATUS_BF      (1<<0)
#define BBA_RX_STATUS_CRC     (1<<1)
#define BBA_RX_STATUS_FAE     (1<<2)
#define BBA_RX_STATUS_FO      (1<<3)
#define BBA_RX_STATUS_RW      (1<<4)
#define BBA_RX_STATUS_MF      (1<<5)
#define BBA_RX_STATUS_RF      (1<<6)
#define BBA_RX_STATUS_RERR    (1<<7)

#define BBA_INIT_TLBP	0x00
#define BBA_INIT_BP		0x01
#define BBA_INIT_RHBP	0x0f
#define BBA_INIT_RWP	BBA_INIT_BP
#define BBA_INIT_RRP	BBA_INIT_BP

enum {
	EXI_READ = 0,
	EXI_WRITE,
	EXI_READ_WRITE,
};

enum {
	EXI_DEVICE_0 = 0,
	EXI_DEVICE_1,
	EXI_DEVICE_2,
	EXI_DEVICE_MAX
};

enum {
	EXI_SPEED_1MHZ = 0,
	EXI_SPEED_2MHZ,
	EXI_SPEED_4MHZ,
	EXI_SPEED_8MHZ,
	EXI_SPEED_16MHZ,
	EXI_SPEED_32MHZ,
};

static int exi_selected(void)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	return !!(exi[0] & 0x380);
}

static void exi_select(void)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	exi[0] = (exi[0] & 0x405) | ((1 << EXI_DEVICE_2) << 7) | (EXI_SPEED_32MHZ << 4);
}

static void exi_deselect(void)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	exi[0] &= 0x405;
}

static void exi_imm_write(uint32_t data, int len)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	exi[4] = data;
	exi[3] = ((len - 1) << 4) | (EXI_WRITE << 2) | 1;
	while (exi[3] & 1);
}

static uint32_t exi_imm_read(int len)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	exi[3] = ((len - 1) << 4) | (EXI_READ << 2) | 1;
	while (exi[3] & 1);
	return exi[4] >> ((4 - len) * 8);
}

static void exi_immex_write(void *buffer, int length)
{
	do {
		int count = MIN(length, 4);

		exi_imm_write(*(uint32_t *)buffer, count);

		buffer += count;
		length -= count;
	} while (length);
}

static void exi_dma_read(void *data, int len)
{
	volatile uint32_t *exi = (uint32_t *)0xCC006800;
	exi[1] = (uint32_t)data;
	exi[2] = len;
	exi[3] = (EXI_READ << 2) | 3;
	while (exi[3] & 1);
}

static uint8_t bba_in8(uint16_t reg)
{
	uint8_t val;

	exi_select();
	exi_imm_write(0x80 << 24 | reg << 8, 4);
	val = exi_imm_read(1);
	exi_deselect();

	return val;
}

static void bba_out8(uint16_t reg, uint8_t val)
{
	exi_select();
	exi_imm_write(0xC0 << 24 | reg << 8, 4);
	exi_imm_write(val << 24, 1);
	exi_deselect();
}

static uint8_t bba_cmd_in8(uint8_t reg)
{
	uint8_t val;

	exi_select();
	exi_imm_write(0x00 << 24 | reg << 24, 2);
	val = exi_imm_read(1);
	exi_deselect();

	return val;
}

static void bba_cmd_out8(uint8_t reg, uint8_t val)
{
	exi_select();
	exi_imm_write(0x40 << 24 | reg << 24, 2);
	exi_imm_write(val << 24, 1);
	exi_deselect();
}

static void bba_ins(uint16_t reg, void *val, int len)
{
	exi_select();
	exi_imm_write(0x80 << 24 | reg << 8, 4);
	exi_dma_read(val, len);
	exi_deselect();
}

static void bba_outs(uint16_t reg, void *val, int len)
{
	exi_select();
	exi_imm_write(0xC0 << 24 | reg << 8, 4);
	exi_immex_write(val, len);
	exi_deselect();
}

void bba_transmit(void *data, int size)
{
	while (bba_in8(BBA_NCRA) & (BBA_NCRA_ST0 | BBA_NCRA_ST1));
	bba_outs(BBA_WRTXFIFOD, data, size);
	bba_out8(BBA_NCRA, (bba_in8(BBA_NCRA) & ~BBA_NCRA_ST0) | BBA_NCRA_ST1);
}

void fsp_output(const char *file, uint8_t filelen, uint32_t offset, uint32_t size);
void eth_input(void *bba, void *eth, uint16_t size);

void bba_receive_end(bba_header_t *bba)
{
	bba_page_t *page = (bba_page_t *)bba + 1;
	int count = (bba->length - 1) / sizeof(*page);
	int rrp;

	if (count > 0) {
		dcache_flush_icache_inv(page, sizeof(*page) * count);

		rrp = bba_in8(BBA_RRP);

		do {
			rrp = rrp % BBA_INIT_RHBP + 1;
			bba_out8(BBA_RRP, rrp);
			bba_ins(rrp << 8, page++, sizeof(*page));
		} while (--count);
	}
}

static void bba_receive(void)
{
	uint8_t rwp = bba_in8(BBA_RWP);
	uint8_t rrp = bba_in8(BBA_RRP);

	while (rrp != rwp) {
		bba_page_t buffer[6];
		bba_header_t *bba = (bba_header_t *)buffer;
		uint16_t size = sizeof(*buffer);

		dcache_flush_icache_inv(buffer, size);

		bba_ins(rrp << 8, buffer, size);

		if (bba->length <= sizeof(buffer))
			size = bba->length;

		size -= sizeof(*bba);

		eth_input(bba, (void *)bba->data, size);
		bba_out8(BBA_RRP, rrp = bba->next);
		rwp = bba_in8(BBA_RWP);
	}
}

static void bba_interrupt(void)
{
	uint8_t ir = bba_in8(BBA_IR);

	while (ir) {
		if (ir & BBA_IR_RI)
			bba_receive();

		bba_out8(BBA_IR, ir);
		ir = bba_in8(BBA_IR);
	}
}

static void bba_poll(void)
{
	uint8_t status = bba_cmd_in8(0x03);

	while (status) {
		if (status & 0x80)
			bba_interrupt();

		bba_cmd_out8(0x03, status);
		status = bba_cmd_in8(0x03);
	}
}

void trigger_dvd_interrupt(void)
{
	volatile uint32_t *dvd = (uint32_t *)0xCC006000;

	uint32_t dst = dvd[5] | 0x80000000;
	uint32_t len = dvd[6];

	dvd[2] = 0xE0000000;
	dvd[3] = 0;
	dvd[4] = 0;
	dvd[5] = 0;
	dvd[6] = 0;
	dvd[8] = 0;
	dvd[7] = 1;

	dcache_flush_icache_inv((void *)dst, len);
}

void perform_read(void)
{
	volatile uint32_t *dvd = (uint32_t *)0xCC006000;

	uint32_t off = dvd[3] << 2;
	uint32_t len = dvd[4];
	uint32_t dst = dvd[5] | 0x80000000;

	*(uint32_t *)VAR_FSP_POSITION = off;
	*(uint32_t *)VAR_TMP2 = len;
	*(uint32_t *)VAR_TMP1 = dst;
	*(uint16_t *)VAR_FSP_DATA_LENGTH = 0;

	if (!is_frag_read(off, len) && !exi_selected())
		fsp_output((const char *)VAR_FILENAME, *(uint8_t *)VAR_FILENAME_LEN, off, len);
}

void *tickle_read(void)
{
	if (!exi_selected()) {
		bba_poll();

		uint32_t position    = *(uint32_t *)VAR_FSP_POSITION;
		uint32_t remainder   = *(uint32_t *)VAR_TMP2;
		uint8_t *data        = *(uint8_t **)VAR_TMP1;
		uint32_t data_length = read_frag(data, remainder, position);

		if (data_length) {
			data      += data_length;
			position  += data_length;
			remainder -= data_length;

			*(uint32_t *)VAR_FSP_POSITION = position;
			*(uint32_t *)VAR_TMP2 = remainder;
			*(uint8_t **)VAR_TMP1 = data;
			*(uint16_t *)VAR_FSP_DATA_LENGTH = 0;

			if (!remainder) trigger_dvd_interrupt();
		} else if (remainder) {
			tb_t end, *start = (tb_t *)VAR_TIMER_START;
			mftb(&end);

			if (tb_diff_usec(&end, start) > 1000000)
				fsp_output((const char *)VAR_FILENAME, *(uint8_t *)VAR_FILENAME_LEN, position, remainder);
		}
	}

	return NULL;
}

void tickle_read_hook(uint32_t enable)
{
	tickle_read();
	restore_interrupts(enable);
}

void tickle_read_idle(void)
{
	disable_interrupts();
	tickle_read();
	enable_interrupts();
}
