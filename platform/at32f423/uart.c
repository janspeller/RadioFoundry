// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The host port on a USART (hal_stream.h, RM 12): 8N1, receive by interrupt into a ring, transmit polled. The host link
// task is the only reader and writer; it sleeps in wait_rx() on a semaphore the receive interrupt gives. A write spins
// on the data register and sleeps a tick every TX_BURST bytes, so a long reply (a debug link view) never keeps the
// lower priority tasks out. The receive interrupt also counts flagged characters and the time of the last good byte, for
// a PTT that shares the line (drivers/input/sharedptt).
#include "config.h"
#include "platform.h"
#include "hal_time.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#ifndef AT32_HOST_RX_SIZE
#define AT32_HOST_RX_SIZE	512u	// power of two; CPS writes come in 32-byte chunks, MMDVM frames are 255 bytes at most
#endif
_Static_assert((AT32_HOST_RX_SIZE & (AT32_HOST_RX_SIZE - 1u)) == 0, "AT32_HOST_RX_SIZE must be a power of two");
#define TX_SPIN		100000u		// per byte, over 1 ms at 72 MHz: a stalled USART gives up rather than hangs
#define TX_BURST	64u		// bytes between a tick's sleep while the register stays busy (5.6 ms at 115200)
#define IRQ_PRIORITY	6u		// below configMAX_SYSCALL (4): the handler gives a semaphore

static const at32_host_uart_cfg_t *cfg;
static uint8_t rx_buf[AT32_HOST_RX_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t rx_overruns, rx_line_errors;
static volatile uint32_t rx_good_ms;
static volatile bool rx_good_seen;
static SemaphoreHandle_t rx_sem;
static bool started;

void at32_host_uart_configure(const at32_host_uart_cfg_t *c)
{
	cfg = c;
}

static void clock_on(USART_Type *u)
{
	if (u == USART1) {
		CRM->APB2EN |= CRM_APB2EN_USART1EN;
	} else if (u == USART6) {
		CRM->APB2EN |= CRM_APB2EN_USART6EN;
	} else if (u == USART3) {
		CRM->APB1EN |= CRM_APB1EN_USART3EN;
	} else {
		CRM->APB1EN |= CRM_APB1EN_USART2EN;
	}
}

// RM 12.6.2: USART1 and 6 run from PCLK2, the others from PCLK1 (CRM_PICLKS left at its reset value)
static uint32_t usart_clock(USART_Type *u)
{
	return (u == USART1 || u == USART6) ? at32_pclk2_hz() : at32_pclk1_hz();
}

void at32_host_uart_isr(void)
{
	if (cfg == NULL) {
		return;
	}
	USART_Type *u = cfg->usart;
	BaseType_t woken = pdFALSE;
	bool got = false;
	uint32_t sts;
	// Reading STS then DT clears RDBF and the error flags (RM 12.13.1)
	while ((sts = u->STS) & (USART_STS_RDBF | USART_STS_ERRORS)) {
		uint8_t b = (uint8_t)u->DT;
		if (sts & USART_STS_ERRORS) {
			rx_line_errors++;
			continue;	// a flagged character is noise or a held line (the PTT), not host data
		}
		rx_good_ms = hal_millis();
		rx_good_seen = true;
		uint32_t next = (rx_head + 1u) & (AT32_HOST_RX_SIZE - 1u);
		if (next == rx_tail) {
			rx_overruns++;
		} else {
			rx_buf[rx_head] = b;
			rx_head = next;
			got = true;
		}
	}
	if (got && rx_sem != NULL) {
		xSemaphoreGiveFromISR(rx_sem, &woken);
	}
	portYIELD_FROM_ISR(woken);
}

// --- A second USART for a chip on the board (at32_aux_uart_*): receive by interrupt into a ring, transmit by
// interrupt straight from the caller's frame. Nothing waits: the radio task's protocol engines run on it ---

static const at32_aux_uart_cfg_t *aux;
static volatile uint32_t aux_head, aux_tail, aux_overruns, aux_errors;
static uint8_t *volatile aux_tx;
static volatile uint32_t aux_tx_len, aux_tx_pos;

static void aux_isr(void)
{
	USART_Type *u = aux->usart;
	uint32_t sts;
	while ((sts = u->STS) & (USART_STS_RDBF | USART_STS_ERRORS)) {
		uint8_t b = (uint8_t)u->DT;
		if (sts & USART_STS_ERRORS) {
			aux_errors++;
			continue;
		}
		uint32_t next = (aux_head + 1u) & (aux->rx_size - 1u);
		if (next == aux_tail) {
			aux_overruns++;
		} else {
			aux->rx[aux_head] = b;
			aux_head = next;
		}
	}
	if ((u->CTRL1 & USART_CTRL1_TDBEIEN) != 0 && (sts & USART_STS_TDBE) != 0) {
		if (aux_tx_pos < aux_tx_len) {
			u->DT = aux_tx[aux_tx_pos];
			aux_tx[aux_tx_pos++] = 0;	// cleared once sent: a frame may carry a key
		} else {
			u->CTRL1 &= ~USART_CTRL1_TDBEIEN;
			aux_tx_len = aux_tx_pos = 0;
		}
	}
}

void at32_aux_uart_start(const at32_aux_uart_cfg_t *c)
{
	aux = c;
	USART_Type *u = c->usart;
	clock_on(u);
	u->CTRL1 = 0;
	u->CTRL2 = 0;
	u->CTRL3 = 0;
	u->BAUDR = (usart_clock(u) + c->baud / 2u) / c->baud;
	aux_head = aux_tail = 0;
	aux_tx_len = aux_tx_pos = 0;
	u->CTRL1 = USART_CTRL1_UEN | USART_CTRL1_TEN | USART_CTRL1_REN | USART_CTRL1_RDBFIEN;
	NVIC_SetPriority(c->irq, IRQ_PRIORITY);
	NVIC_ClearPendingIRQ(c->irq);
	NVIC_EnableIRQ(c->irq);
}

size_t at32_aux_uart_read(uint8_t *buf, size_t max)
{
	size_t n = 0;
	while (aux != NULL && n < max && aux_tail != aux_head) {
		buf[n++] = aux->rx[aux_tail];
		aux_tail = (aux_tail + 1u) & (aux->rx_size - 1u);
	}
	return n;
}

bool at32_aux_uart_write(uint8_t *buf, size_t len)
{
	if (aux == NULL || len == 0 || aux_tx_len != 0) {
		return false;		// the previous frame is still going out
	}
	aux_tx = buf;
	aux_tx_pos = 0;
	aux_tx_len = (uint32_t)len;
	aux->usart->CTRL1 |= USART_CTRL1_TDBEIEN;
	return true;
}

bool at32_aux_uart_tx_busy(void)
{
	return aux_tx_len != 0;
}

uint32_t at32_aux_uart_errors(void)
{
	return aux_errors + aux_overruns;
}

// Whichever USART the board named, the host's or the chip's; the others are never enabled
static void usart_irq(USART_Type *u)
{
	if (aux != NULL && aux->usart == u) {
		aux_isr();
	} else {
		at32_host_uart_isr();
	}
}

void USART1_IRQHandler(void)
{
	usart_irq(USART1);
}

void USART2_IRQHandler(void)
{
	at32_host_uart_isr();	// no USART2 in at32f423.h: never the second one
}

void USART3_IRQHandler(void)
{
	usart_irq(USART3);
}

void USART6_IRQHandler(void)
{
	usart_irq(USART6);
}

static bool start(void)
{
	if (started) {
		return true;
	}
	if (cfg == NULL) {
		return false;
	}
	if (rx_sem == NULL) {
		rx_sem = xSemaphoreCreateBinary();
	}
	USART_Type *u = cfg->usart;
	clock_on(u);
	u->CTRL1 = 0;
	u->CTRL2 = 0;
	u->CTRL3 = 0;
	u->BAUDR = (usart_clock(u) + cfg->baud / 2u) / cfg->baud;	// RM 12.6.2: written before UEN
	rx_head = rx_tail = 0;
	u->CTRL1 = USART_CTRL1_UEN | USART_CTRL1_TEN | USART_CTRL1_REN | USART_CTRL1_RDBFIEN;
	NVIC_SetPriority(cfg->irq, IRQ_PRIORITY);
	NVIC_ClearPendingIRQ(cfg->irq);
	NVIC_EnableIRQ(cfg->irq);
	started = true;
	return true;
}

static bool connected(void)
{
	return started;		// a UART can't tell whether a cable is there
}

static size_t rd(uint8_t *buf, size_t max)
{
	size_t n = 0;
	while (n < max && rx_tail != rx_head) {
		buf[n++] = rx_buf[rx_tail];
		rx_tail = (rx_tail + 1u) & (AT32_HOST_RX_SIZE - 1u);
	}
	return n;
}

static bool wait_rx(uint32_t timeout_ms)
{
	if (rx_tail != rx_head) {
		return true;
	}
	if (rx_sem != NULL) {
		(void)xSemaphoreTake(rx_sem, pdMS_TO_TICKS(timeout_ms));
	}
	return rx_tail != rx_head;
}

static size_t wr(const uint8_t *buf, size_t len)
{
	if (!started) {
		return 0;
	}
	USART_Type *u = cfg->usart;
	bool sleeping = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
	uint32_t burst = 0;
	for (size_t i = 0; i < len; i++) {
		if (sleeping && ++burst > TX_BURST && (u->STS & USART_STS_TDBE) == 0) {
			burst = 0;
			vTaskDelay(1);
		}
		uint32_t spins = TX_SPIN;
		while ((u->STS & USART_STS_TDBE) == 0) {
			if (--spins == 0) {
				return i;
			}
		}
		u->DT = buf[i];
	}
	return len;
}

const hal_stream_t at32_host_uart_stream = {
	.start = start,
	.connected = connected,
	.read = rd,
	.wait_rx = wait_rx,
	.write = wr,
};

uint32_t at32_host_uart_line_errors(void)
{
	return rx_line_errors;
}

uint32_t at32_host_uart_overruns(void)
{
	return rx_overruns;
}

bool at32_host_uart_last_rx(uint32_t *ms)
{
	*ms = rx_good_ms;
	return rx_good_seen;
}
