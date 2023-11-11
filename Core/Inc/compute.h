#ifndef COMPUTE_H
#define COMPUTE_H

#include "datastructs.h"
#include "stateMachine.h"

#define CURRENT_SENSOR_PIN_L A1
#define CURRENT_SENSOR_PIN_H A0
#define MEAS_5VREF_PIN		 A7
#define FAULT_PIN			 2
#define CHARGE_SAFETY_RELAY	 4
#define CHARGE_DETECT		 5
#define CHARGER_BAUD		 250000U
#define MC_BAUD				 1000000U
#define MAX_ADC_RESOLUTION	 1023 // 13 bit ADC

/**
 * @brief inits the compute interface
 */
void compute_init();

/**
 * @brief sets safeguard bool to check whether charging is enabled or disabled
 *
 * @param is_enabled
 */
void compute_enable_charging(bool enable_charging);

/**
 * @brief sends charger message
 *
 * @param voltage_to_set
 * @param currentToSet
 *
 * @return Returns a fault if we are not able to communicate with charger
 */
int compute_send_charging_message(uint16_t voltage_to_set, acc_data_t* bms_data);

/**
 * @brief Returns if charger interlock is engaged, indicating charger LV connector is plugged in
 *
 * @return true
 * @return false
 */
bool compute_charger_connected();

/**
 * @brief Handle any messages received from the charger
 *
 * @param msg
 */
//static void compute_charger_callback(const CAN_message_t& msg);

//static void compute_mc_callback(const CAN_message_t& msg);

/**
 * @brief Sets the desired fan speed
 *
 * @param new_fan_speed
 */
void compute_set_fan_speed(uint8_t new_fan_speed);

/**
 * @brief Returns the pack current sensor reading
 *
 * @return int16_t
 */
int16_t compute_get_pack_current();

/**
 * @brief sends max charge/discharge current to Motor Controller
 *
 * @param max_charge
 * @param max_discharge
 */
void compute_send_mc_message(uint16_t max_charge, uint16_t max_discharge);

/**
 * @brief updates fault relay
 *
 * @param fault_state
 */
void compute_set_fault(int fault_state);

/**
 * @brief sends acc status message
 *
 * @param voltage
 * @param current
 * @param ah
 * @param soc
 * @param health
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_acc_status_message(acc_data_t* bmsdata);

/**
 * @brief sends BMS status message
 *
 * @param bms_state
 * @param fault_status
 * @param tempAvg
 * @param tempInternal
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_bms_status_message(acc_data_t* bmsdata, int bms_state, bool balance);

/**
 * @brief sends shutdown control message
 *
 * @param mpe_state
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_shutdown_ctrl_message(uint8_t mpe_state);

/**
 * @brief sends cell data message
 *
 * @param high_voltage
 * @param low_voltage
 * @param avg_voltage
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_cell_data_message(acc_data_t* bmsdata);

/**
 * @brief sends cell voltage message
 *
 * @param cell_id
 * @param instant_volt
 * @param internal_res
 * @param shunted
 * @param open_voltage
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_cell_voltage_message(uint8_t cell_id, uint16_t instant_volt,
									   uint16_t internal_res, uint8_t shunted,
									   uint16_t open_voltage);

/**
 * @brief sends out the calculated values of currents
 *
 * @param discharge
 * @param charge
 * @param current
 */
void compute_send_current_message(acc_data_t* bmsdata);

/**
 * @brief sends cell temperature message
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_cell_temp_message(acc_data_t* bmsdata);

/**
 * @brief sends the average segment temperatures
 *
 *
 *
 * @return Returns a fault if we are not able to send
 */
void compute_send_segment_temp_message(acc_data_t* bmsdata);

#endif // COMPUTE_H
