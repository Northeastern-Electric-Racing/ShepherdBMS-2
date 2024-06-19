#ifndef BMS_CONFIG_H
#define BMS_CONFIG_H

// Hardware definition
#define NUM_SEGMENTS	    6
#define NUM_CHIPS	    NUM_SEGMENTS * 2
#define NUM_CELLS_PER_CHIP  10
#define NUM_THERMS_PER_CHIP 32
#define NUM_RELEVANT_THERMS 3

// Firmware limits
#define MAX_TEMP      65 //degrees C
#define MIN_TEMP      -25 // deg C
#define MAX_VOLT_MEAS 65535
#define MIN_VOLT_MEAS 0

// Boosting Parameters
#define BOOST_TIME	    5 // seconds
#define BOOST_RECHARGE_TIME 30 // seconds
#define CONTDCL_MULTIPLIER  3

//cell limits
#define MIN_VOLT	  2.5
#define MAX_VOLT	  4.2
#define MAX_CHARGE_VOLT	  4.21
#define MAX_DELTA_V	  0.015
#define BAL_MIN_V	  4.00
#define MAX_CELL_TEMP	  55
#define MIN_CELL_TEMP	  15
#define MAX_CELL_CURR	  500 // Amps per BMS cell
#define MAX_CELL_TEMP_BAL 45
#define MAX_CHG_CELL_CURR 20

// Algorithm settings
#define CHARGE_SETL_TIMEOUT 60000 // 1 minute, may need adjustment
#define CHARGE_SETL_TIMEUP  300000 // 5 minutes, may need adjustment
#define CHARGE_VOLT_TIMEOUT 300000 // 5 minutes, may need adjustment
#define VOLT_SAG_MARGIN \
	0.45 // Volts above the minimum cell voltage we would like to aim for
#define OCV_CURR_THRESH 1.5

#define OCV_AVG 3

#define MAX_STANDARD_DEV 3 // only used for standard deviation for therms calc

//Fault times
#define OVER_CURR_TIME \
	5000 //todo adjust these based on testing and/or counter values
#define PRE_OVER_CURR_TIME  1000
#define OVER_CHG_CURR_TIME  1000
#define UNDER_VOLT_TIME	    45000
#define PRE_UNDER_VOLT_TIME 12000
#define OVER_VOLT_TIME	    45000
#define LOW_CELL_TIME	    45000
#define HIGH_TEMP_TIME	    60000
#define CURR_ERR_MARG	    1.1 // scaling factor, ie 1.1 = 10% error

#define DCDC_CURRENT_DRAW \
	0 // in A, was used because our DCDC was drawing current

#define CAN_MESSAGE_WAIT 5

#endif