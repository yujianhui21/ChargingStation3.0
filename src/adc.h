#ifndef _ADC_H
#define _ADC_H

#define ADC0_PIN 1
#define ADC1_PIN 2
#define ADC2_PIN 3

#define R_NTC 100000   // NTC 25°C 标称阻值 (NTC100K B3950)
#define R_DIV 100000   // NTC 分压上拉电阻 (100kΩ，勿写成 10k)
#define NTC_REF_TEMP 25


#include <Arduino.h>
#include <esp_system.h>
#include <variables.h>
#include <gpio.h>
#include <lvgl.h>
#include "ui/ui.h"

void adc_init();
void adc_task(lv_timer_t *timer);
float adc_get_voltage0();
float adc_get_voltage1();
float adc_get_temperature();

#endif
