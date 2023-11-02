#ifndef SEGMENT_H
#define SEGMENT_H

#include <LTC68041.h>
//#include <nerduino.h> <---------- REPLACE W/ NEW DRIVER
#include "bmsConfig.h"
#include "datastructs.h"

/**
 * @brief Initializes the segments
 */
void segment_init();

/**
 * @brief Pulls all cell data from the segments and returns all cell data
 *
 * @todo make sure that retrieving cell data doesn't block code too much
 *
 * @return int*
 */
void segment_retrieve_segment_data(ChipData_t databuf[NUM_CHIPS]);

/**
 * @brief Enables/disables balancing for all cells
 *
 * @param balance_enable
 */
void segment_enable_balancing(bool balance_enable);

/**
 * @brief Enables/disables balancing for a specific cell
 *
 * @param chip_num
 * @param cell_num
 * @param balance_enable
 */
void enable_balancing(uint8_t chip_num, uint8_t cell_num, bool balance_enable);

/**
 * @brief Sets each cell to whatever state is passed in the boolean config area
 *
 * @param discharge_config
 */
void segment_configure_balancing(bool discharge_config[NUM_CHIPS][NUM_CELLS_PER_CHIP]);

/**
 * @brief Returns if a specific cell is balancing
 *
 * @param chip_num
 * @return true
 * @return false
 */
bool segment_is_balancing(uint8_t chip_num, uint8_t cell_num);

/**
 * @brief Returns if any cells are balancing
 *
 * @return true
 * @return false
 */
bool segment_is_balancing();

#endif