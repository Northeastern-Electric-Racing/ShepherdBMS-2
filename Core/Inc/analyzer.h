#ifndef ANALYZER_H
#define ANALYZER_H

//#include <nerduino.h> Replace
#include "datastructs.h"
#include "segment.h"
#include "Core/Drivers/Embedded-Base/general/include/sht30.h"

/* We want to make sure we aren't doing useless analysis on the same set of data since we are
 * backfilling segment data */
#define ANALYSIS_INTERVAL VOLTAGE_WAIT_TIME

//#define MAX_SIZE_OF_HIST_QUEUE  300000U //bytes

/**
 * @brief Pushes in a new data point if we have waited long enough
 *
 * @param data
 */
void analyzer_push(AccumulatorData_t* data);

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
AccumulatorData_t* bmsdata;

AccumulatorData_t* prevbmsdata;

/**
 * @brief Create a new object to store the most recent data point for the temp sensor
 */
sht30_t* sht30data;

/**
 * @brief Create a new object to store the most recent data point for the temp sensor
 */
void sht30_init();

#endif