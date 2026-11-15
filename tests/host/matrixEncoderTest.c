// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Exercise real matrix/encoder glue with delayed sense lines and edges between key scans.
#include <assert.h>
#include <stdio.h>
#include INPUT_SOURCE

mcu_gpio_t host_gpio[5];
static int held_row = -1, held_col = -1;
static unsigned settled_us;
static uint32_t edge_lines, enabled_lines;
static bool irq_enabled;
void mcu_pin_in(mcu_gpio_t *p, uint16_t pins, unsigned pull) { (void)p; (void)pins; (void)pull; }
void mcu_port_put(mcu_gpio_t *p, uint16_t set, uint16_t clear)
{
	p->OCTL = (uint16_t)((p->OCTL | set) & ~clear);
	settled_us = 0;
}
void hal_delay_us(uint32_t us) { settled_us += us; }
bool mcu_pin_read(mcu_gpio_t *p, uint16_t pin)
{
	for (unsigned c = 0; c < COLS; c++) {
		if (p == sense_port[c] && pin == sense_bit[c]) {
			assert(settled_us >= 5);
			return !(held_col == (int)c && held_row >= 0 &&
				 (GPIOE->OCTL & MCU_PIN(7 + held_row)) == 0);
		}
	}
	return (p->in & pin) != 0;
}
void mcu_exti_route(mcu_gpio_t *p, unsigned line) { assert(p == GPIOC && (line == 8 || line == 9)); }
void mcu_exti_both_edges(uint32_t lines) { edge_lines |= lines; }
void mcu_exti_clear(uint32_t lines) { assert(lines == (MCU_PIN(8) | MCU_PIN(9))); }
void mcu_exti_enable(uint32_t lines) { enabled_lines |= lines; }
void NVIC_SetPriority(unsigned irq, unsigned priority) { assert(irq == MCU_EXTI9_5_IRQn && priority >= 4); }
void NVIC_EnableIRQ(unsigned irq) { assert(irq == MCU_EXTI9_5_IRQn); irq_enabled = true; }
// A whole Gray-code cycle, four transitions, forward (the header's 00 -> 01 -> 11 -> 10 -> 00). The lines start
// at 11, both released. On this radio the encoder emits two transitions per physical click and rests where the
// lines agree, so one cycle is TWO detents, not one: a click took two turns of the knob before that was fixed
#define TRANSITIONS_PER_CYCLE	4	// a Gray code has four states
#define STEPS_PER_DETENT	2	// what input_d878uv2.c asks quad_init for
#define DETENTS_PER_CYCLE	(TRANSITIONS_PER_CYCLE / STEPS_PER_DETENT)
static void turn(void)
{
	static const uint8_t states[] = { 2, 0, 1, 3 };
	for (unsigned i = 0; i < sizeof(states); i++) {
		GPIOC->in = (uint16_t)((GPIOC->in & ~(MCU_PIN(8) | MCU_PIN(9))) |
			((states[i] & 1u) ? MCU_PIN(8) : 0) | ((states[i] & 2u) ? MCU_PIN(9) : 0));
		MCU_EXTI9_5_HANDLER();
	}
}
int main(void)
{
	for (unsigned i = 0; i < 5; i++) host_gpio[i].in = 0xFFFF;
	GPIOA->in &= ~MCU_PIN(13);
	init();
	assert(irq_enabled && edge_lines == KNOB_LINES && enabled_lines == KNOB_LINES);
	GPIOE->OCTL = 0x5A80;
	assert(scan() == 0);
	assert(GPIOE->OCTL == 0x5A80);
	for (unsigned r = 0; r < ROWS; r++) {
		for (unsigned c = 0; c < COLS; c++) {
			if (key_map[r * COLS + c] == KEYMATRIX_NONE) continue;
			held_row = (int)r;
			held_col = (int)c;
			assert(scan() == (1u << key_map[r * COLS + c]));
			assert(GPIOE->OCTL == 0x5A80);
		}
	}
	held_row = held_col = -1;
	assert(scan() == 0);
	turn();
	int first = encoder();
	assert(first == DETENTS_PER_CYCLE || first == -DETENTS_PER_CYCLE);
	assert(encoder() == 0);
	turn(); turn();
	assert(encoder() == 2 * first);
	assert(encoder() == 0);
	// and the counters agree: four valid transitions a cycle, none illegal
	hal_encoder_stats_t st;
	d878uv2_input.encoder_stats(&st);
	assert(st.transitions == 3u * TRANSITIONS_PER_CYCLE && st.illegal == 0u);
	assert(st.steps_per_detent == STEPS_PER_DETENT);
	puts("matrixEncoderTest PASSED");
	return 0;
}
