#ifndef BMS_STATES_H
#define BMS_STATES_H

#include "datastructs.h"
#include "segment.h"
#include "compute.h"
#include "analyzer.h"

/**
* @brief Returns if we want to balance cells during a particular frame
*
* @param bmsdata
* @return true
* @return false
*/
bool sm_balancing_check(AccumulatorData_t *bmsdata);

/**
* @brief Returns if we want to charge cells during a particular frame
*
* @param bmsdata
* @return true
* @return false
*/
bool sm_charging_check(AccumulatorData_t *bmsdata);


/**
* @brief Returns any new faults or current faults that have come up
* @note Should be bitwise OR'ed with the current fault status
*
* @param accData
* @return uint32_t
*/
uint32_t sm_fault_return(AccumulatorData_t *accData);

 /**
 * @brief Used in parellel to faultReturn(), calculates each fault to append the fault status
 * 
 * @param index
 * @return fault_code
 */
uint32_t sm_fault_eval(fault_eval index);

/**
 * @brief Used to check for faults immedietly before reaching faulted state, allows for easier handling
 * 
 * @param bmsdata 
 */
void preFaultCheck(AccumulatorData_t *bmsdata);

/**
 * @brief handles the state machine, calls the appropriate handler function and runs every loop functions
 * 
 * @param bmsdata 
 */
void sm_handle_state(AccumulatorData_t *bmsdata);

/**
* @brief Algorithm behind determining which cells we want to balance
* @note Directly interfaces with the segments
*
* @param bms_data
*/
void sm_balance_cells(AccumulatorData_t *bms_data);
void sm_broadcast_current_limit(AccumulatorData_t *bmsdata);

#endif //BMS_STATES_H