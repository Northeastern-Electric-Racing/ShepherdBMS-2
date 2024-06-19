#ifndef ANALYZER_H
#define ANALYZER_H

//#include <nerduino.h> Replace
#include "datastructs.h"
#include "segment.h"

// this is a simple empirical mapping of which therms are returning good data
// Only in use because we can not seem to correclty map incoming therms - this allows us to forcibley select those that we visually noticed were good
extern uint8_t THERM_DISABLE[NUM_CHIPS][NUM_THERMS_PER_CHIP];

extern const uint8_t NO_THERM;
extern const uint8_t MUX_OFFSET;

/**
 * @brief Mapping the Relevant Thermistors for each cell based on cell #
 * @note 0xFF indicates the end of the relevant therms
 * @note Low side
 */
extern const uint8_t RELEVANT_THERM_MAP_L[NUM_CELLS_PER_CHIP]
					 [NUM_RELEVANT_THERMS];

/**
 * @brief Mapping the Relevant Thermistors for each cell based on cell #
 * @note 0xFF indicates the end of the relevant therms
 * @note High side
 */
extern const uint8_t RELEVANT_THERM_MAP_H[NUM_CELLS_PER_CHIP]
					 [NUM_RELEVANT_THERMS];

/*
 * List of therms that we actually read from, NOT reordered by cell
 */
extern const uint8_t POPULATED_THERM_LIST_L[NUM_THERMS_PER_CHIP];

/*
 * List of therms that we actually read from, NOT reordered by cell
 */
extern const uint8_t POPULATED_THERM_LIST_H[NUM_THERMS_PER_CHIP];

/* We want to make sure we aren't doing useless analysis on the same set of data since we are
 * backfilling segment data */
#define ANALYSIS_INTERVAL VOLTAGE_WAIT_TIME

//#define MAX_SIZE_OF_HIST_QUEUE  300000U //bytes

/**
 * @brief Pushes in a new data point if we have waited long enough
 *
 * @param data
 */
void analyzer_push(acc_data_t *data);

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
extern acc_data_t *bmsdata;

extern acc_data_t *prevbmsdata;

#endif