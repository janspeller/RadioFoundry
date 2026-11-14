// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// ADC1, one software-triggered conversion at a time (RM 18.4.2): power-up with XTEST, calibration, then a one-channel
// ordinary sequence per read. For slow readings (the battery): no DMA, no interrupt. Callers serialize (the board's
// power task)
#include "platform.h"

#define ADC_DIV_CODE	2u		// ADCCLK = HCLK / 4 = 18 MHz at 72 MHz (at most 80 MHz and PCLK2, RM 18.5.22)
#define XTEST_ON	0x9u		// RM 18.4.2.1: before ADCEN
#define SPT_247_5	6u		// 247.5 cycles: a high-impedance divider charges the sample capacitor
#define SPINS		200000u

static bool ready;

void at32_adc_init(void)
{
	CRM->APB2EN |= CRM_APB2EN_ADCEN;
	(void)CRM->APB2EN;
	ADCCOM_CCTRL = (ADCCOM_CCTRL & ~(0xFu << ADC_CCTRL_ADCDIV_Pos)) | (ADC_DIV_CODE << ADC_CCTRL_ADCDIV_Pos);
	ADC1->MISC = (ADC1->MISC & ~0x3Fu) | XTEST_ON;
	ADC1->CTRL2 = ADC_CTRL2_ADCEN;
	for (uint32_t i = 0; i < SPINS && (ADC1->STS & ADC_STS_RDY) == 0; i++) {
	}
	ADC1->CTRL2 |= ADC_CTRL2_ADCALINIT;
	for (uint32_t i = 0; i < SPINS && (ADC1->CTRL2 & ADC_CTRL2_ADCALINIT) != 0; i++) {
	}
	ADC1->CTRL2 |= ADC_CTRL2_ADCAL;
	for (uint32_t i = 0; i < SPINS && (ADC1->CTRL2 & ADC_CTRL2_ADCAL) != 0; i++) {
	}
	ready = (ADC1->STS & ADC_STS_RDY) != 0;
}

static void sample_time(uint8_t ch)
{
	if (ch < 10u) {
		ADC1->SPT2 = (ADC1->SPT2 & ~(7u << (3u * ch))) | (SPT_247_5 << (3u * ch));
	} else if (ch < 20u) {
		uint32_t s = 3u * (ch - 10u);
		ADC1->SPT1 = (ADC1->SPT1 & ~(7u << s)) | (SPT_247_5 << s);
	} else {
		uint32_t s = 3u * (ch - 20u);
		ADC1->SPT3 = (ADC1->SPT3 & ~(7u << s)) | (SPT_247_5 << s);
	}
}

int32_t at32_adc_read(uint8_t channel)
{
	if (!ready || channel > 27u || (channel > 17u && channel < 20u)) {
		return -1;
	}
	sample_time(channel);
	ADC1->OSQ1 = 0;			// OCLEN 0: one conversion
	ADC1->OSQ3 = channel;		// OSN1
	ADC1->STS = ~ADC_STS_OCCE;	// rw0c
	ADC1->CTRL2 |= ADC_CTRL2_OCSWTRG;
	for (uint32_t i = 0; i < SPINS; i++) {
		if (ADC1->STS & ADC_STS_OCCE) {
			return (int32_t)(ADC1->ODT & 0xFFFu);
		}
	}
	return -1;
}
