#ifndef ANALYZER_H
#define ANALYZER_H

//#include <nerduino.h> Replace
#include "datastructs.h"
#include "segment.h"
#include "stm32f4xx_hal.h"

/* We want to make sure we aren't doing useless analysis on the same set of data since we are
 * backfilling segment data */
#define ANALYSIS_INTERVAL VOLTAGE_WAIT_TIME

//#define MAX_SIZE_OF_HIST_QUEUE  300000U //bytes

/**
 * @brief Pushes in a new data point if we have waited long enough
 *
 * @param data
 */
void analyzer_push(acc_data_t* data);

/**
 * @brief Calculates the PWM required to drive the fans at the current moment in time
 *
 * @param bmsdata
 * @return uint8_t
 */
uint8_t analyzer_calc_fan_pwm();

/**
 * @brief Pointer to the address of the most recent data point
 */
extern acc_data_t* bmsdata;

extern acc_data_t* prevbmsdata;

#endif