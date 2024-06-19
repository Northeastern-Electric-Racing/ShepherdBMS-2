#include "analyzer.h"
#include <stdlib.h>
#include <stdio.h>

acc_data_t *bmsdata;

acc_data_t *prevbmsdata;

// clang-format off
/**
 * @brief Mapping Cell temperature to the cell resistance based on the
 *      nominal cell resistance curve profile of the Samsung 186500 INR in the
 *      Orion BMS software utility app
 *
 * @note Units are in mOhms and indicies are in (degrees C)/5, stops at 65C
 * @note Resistance should be *interpolated* from these values (i.e. if we are
 *      at 27C, we should take the resistance that is halfway between 25C and 30C)
 */
const float TEMP_TO_CELL_RES[14] =
{
	5.52 * (7/5), 4.84 * (7/5), 4.27 * (7/5), 3.68 * (7/5), 3.16 * (7/5), 2.74 * (7/5), 2.4 * (7/5),
	2.12 * (7/5), 1.98 * (7/5), 1.92 * (7/5), 1.90 * (7/5), 1.90 * (7/5), 1.90 * (7/5), 1.90 * (7/5)
};

/**
 * @brief Mapping Cell temperatue to the discharge current limit based on the
 *      temperature discharge limit curve profile of the Samsung 186500 INR
 *      in the Orion BMS software utility app
 *
 * @note Units are in Amps and indicies are in (degrees C)/5, stops at 65C
 * @note Limit should be *interpolated* from these values (i.e. if we are
 *      at 27C, we should take the limit that is halfway between 25C and 30C)
 *
 */
const uint8_t TEMP_TO_DCL[14] =
{
	110, 125, 140, 140, 140, 140,
	140, 140, 140, 100, 60, 20, 0, 0
};

/**
 * @brief Mapping Cell temperatue to the charge current limit based on the
 *      temperature charge limit curve profile of the Samsung 186500 INR
 *      in the Orion BMS software utility app
 *
 * @note Units are in Amps and indicies are in (degrees C)/5, stops at 65C
 * @note Limit should be *interpolated* from these values (i.e. if we are
 *      at 27C, we should take the limit that is halfway between 25C and 30C)
 *
 */
const uint8_t TEMP_TO_CCL[14] =
{
	0, 25, 25, 25, 25, 25, 25, 25,
	20, 15, 10, 5, 1, 1
};

/**
 * @brief Lookup table for State of Charge
 *
 * @note each index covers 0.1V increase (voltage range is 2.9V - 4.2V, deltaV = 1.3V, 
 *      currently 13 data points)
 * @note values are unitless percentages that represent % charge
 *
 */
const uint8_t STATE_OF_CHARGE_CURVE[18] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 15, 24, 56, 74, 85, 95, 98, 100
};

/**
 * @brief Mapping desired fan speed PWM to the cell temperature
 *
 * @note Units are in PWM out of 255 and indicies are in (degrees C)/5, stops at 65C
 * @note Limit should be *interpolated* from these values (i.e. if we are
 *      at 27C, we should take the limit that is halfway between 25C and 30C)
 */
const uint8_t FAN_CURVE[16] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 32, 64,
	128, 255, 255, 255, 255, 255
};

const uint8_t NO_THERM = 0xFF;
const uint8_t MUX_OFFSET = 16;

/**
 * @brief Mapping the Relevant Thermistors for each cell based on cell #
 * @note 0xFF indicates the end of the relevant therms
 * @note High side
 */
const uint8_t RELEVANT_THERM_MAP_H[NUM_CELLS_PER_CHIP][NUM_RELEVANT_THERMS] =
{
	{5, 3, NO_THERM},
	{12 + MUX_OFFSET, 14 + MUX_OFFSET, NO_THERM},
	{2, 0, 1},
	{9 + MUX_OFFSET, 11 + MUX_OFFSET, NO_THERM},
	{8, 6, NO_THERM},
	{0 + MUX_OFFSET, 2 + MUX_OFFSET, NO_THERM},
	{12, 14, 13},
	{3 + MUX_OFFSET, 5 + MUX_OFFSET, NO_THERM},
	{11, 9, NO_THERM},
	{6 + MUX_OFFSET, 8 + MUX_OFFSET, NO_THERM},
};

/**
 * @brief Mapping the Relevant Thermistors for each cell based on cell #
 * @note 0xFF indicates the end of the relevant therms
 * @note Low side
 */
const uint8_t RELEVANT_THERM_MAP_L[NUM_CELLS_PER_CHIP][NUM_RELEVANT_THERMS] =
{
	{5, 3, 4},
	{12 + MUX_OFFSET, 14 + MUX_OFFSET, NO_THERM},
	{2, 0, NO_THERM},
	{11 + MUX_OFFSET, 9 + MUX_OFFSET, NO_THERM},
	{6, 7, 8},
	{0 + MUX_OFFSET, 2 + MUX_OFFSET, NO_THERM},
	{14, 12, NO_THERM},
	{5 + MUX_OFFSET, 3 + MUX_OFFSET, NO_THERM},
	{9, 10, 11},
	{6 + MUX_OFFSET, 8 + MUX_OFFSET, NO_THERM},
};

uint8_t THERM_DISABLE[NUM_CHIPS][NUM_THERMS_PER_CHIP] =
{
	{1,0,1,0,0,1,0,1,1,1,0,1,0,0,1,0,0,0,1,0,0,0,0,1,1,1,0,0,0,0,1,0 },
	{1,0,1,0,0,1,0,0,1,0,1,1,0,1,1,0,0,1,1,1,0,1,0,1,1,1,1,1,0,1,1,1 },
	{1,0,1,0,0,1,0,0,1,0,0,1,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0 },
	{1,0,1,0,0,1,0,0,1,1,0,1,0,0,1,0,0,0,0,0,1,1,1,0,1,0,0,1,0,0,0,0 },
	{1,0,1,0,0,1,0,1,1,0,0,1,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,1,1,1 },
	{1,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,1,0,1,1,1,0,0,0 },
	{1,0,1,0,0,1,0,0,1,0,1,1,0,0,1,1,0,0,1,0,1,1,1,0,0,0,0,0,0,0,1,0 },
	{1,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,1,0,0,1,0,1,1,1,1,1,1 },
	{1,1,1,0,0,1,0,0,1,0,0,1,0,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0 },
	{1,0,1,0,0,0,0,0,1,0,0,1,0,0,1,0,1,0,0,1,0,1,0,0,1,0,0,1,0,0,0,0 },
	{1,1,1,0,0,1,0,0,1,1,0,1,0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,1,0 },
	{1,1,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,0,0,0,1,0,1,1,1,0,1,0,1,0,0 }
};

/*
 * List of therms that we actually read from, NOT reordered by cell
 */
const uint8_t POPULATED_THERM_LIST_H[NUM_THERMS_PER_CHIP] =
{
	true, false, true,
	true, true, true,
	true, true, true,
	true, true, true,
	true, false, true, false,
	true, false, true,
	true, false, true,
	true, false, true,
	true, false, true,
	true, false, true, false
};

const uint8_t POPULATED_THERM_LIST_L[NUM_THERMS_PER_CHIP] =
{
	true, true, true,
	true, false, true,
	true, false, true,
	true, false, true,
	true, true, true, false,
	true, false, true,
	true, false, true,
	true, false, true,
	true, false, true,
	true, false, true, false
};

/**
 * @brief Selecting thermistors to ignore
 *
 * @note True / 1 will disable the thermistor
 * @note disabling both unpopulaed (see above) and populated but bad cells ( not great permanent solution)
 */

// clang-format on

nertimer_t analysisTimer;
nertimer_t ocvTimer;

bool is_first_reading_ = true;

/* private function prototypes */
void disable_therms();
void high_curr_therm_check();
void diff_curr_therm_check();
void calc_state_of_charge();

/* we are not corrctly mapping each therm reading to the correct cell. So, we are taking the average of all good readings (not disabled) for a given chip, 
 and assigning that to be the cell val for every cell in the chip*/

void calc_cell_temps()
{
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			const uint8_t(*therm_map)[NUM_RELEVANT_THERMS] =
				(c % 2 == 0) ? RELEVANT_THERM_MAP_L :
					       RELEVANT_THERM_MAP_H;
			uint8_t therm_count = 0;
			int temp_sum = 0;
			for (uint8_t therm = 0; therm < NUM_RELEVANT_THERMS;
			     therm++) {
				uint8_t thermNum = therm_map[cell][therm];

				if (thermNum != NO_THERM) {
					//printf("%d\t", bmsdata->chip_data[c].thermistor_value[therm]);
					temp_sum +=
						bmsdata->chip_data[c]
							.thermistor_value[therm];
					therm_count++;
				}
			}
			//printf("\r\n");
			/* Takes the average temperature of all the relevant thermistors */
			bmsdata->chip_data[c].cell_temp[cell] =
				temp_sum / therm_count;
			therm_count = 0;
		}
	}
}

void calc_pack_temps()
{
	bmsdata->max_temp.val = MIN_TEMP;
	bmsdata->max_temp.cellNum = 0;
	bmsdata->max_temp.chipIndex = 0;

	bmsdata->min_temp.val = MAX_TEMP;
	bmsdata->min_temp.cellNum = 0;
	bmsdata->min_temp.chipIndex = 0;
	int total_temp = 0;
	int total_seg_temp = 0;
	int total_accepted = 0;
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t therm = 0; therm < NUM_THERMS_PER_CHIP; therm++) {
			/* finds out the maximum cell temp and location */

			//if (THERM_DISABLE[c][therm]) continue;
			total_accepted++;
			//if (bmsdata->chip_data[c].thermistor_value[therm] > bmsdata->max_temp.val) {
			//	bmsdata->max_temp.val = bmsdata->chip_data[c].thermistor_value[therm];
			//	bmsdata->max_temp.cellNum = c;
			//	bmsdata->max_temp.chipIndex = therm;
			//}

			/* finds out the minimum cell temp and location */
			//if (bmsdata->chip_data[c].thermistor_value[therm] < bmsdata->min_temp.val) {
			//	bmsdata->min_temp.val = bmsdata->chip_data[c].thermistor_value[therm];
			//	bmsdata->min_temp.cellNum = c;
			//	bmsdata->min_temp.chipIndex = therm;
			//}

			total_temp +=
				bmsdata->chip_data[c].thermistor_value[therm];
			total_seg_temp +=
				bmsdata->chip_data[c].thermistor_value[therm];
		}

		/* only for NERO */
		if (c % 2 == 0) {
			bmsdata->segment_average_temps[c / 2] =
				total_seg_temp / 22;
			total_seg_temp = 0;
		}
	}

	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			if (bmsdata->chip_data[c].cell_temp[cell] >
			    bmsdata->max_temp.val) {
				bmsdata->max_temp.val =
					bmsdata->chip_data[c].cell_temp[cell];
				bmsdata->max_temp.cellNum = cell;
				bmsdata->max_temp.chipIndex = c;
			}

			/* finds out the minimum cell temp and location */
			if (bmsdata->chip_data[c].cell_temp[cell] <
			    bmsdata->min_temp.val) {
				bmsdata->min_temp.val =
					bmsdata->chip_data[c].cell_temp[cell];
				bmsdata->min_temp.cellNum = cell;
				bmsdata->min_temp.chipIndex = c;
			}
		}
	}

	/* takes the average of all the cell temperatures */
	bmsdata->avg_temp = total_temp / (total_accepted);
}

void calc_pack_voltage_stats()
{
	bmsdata->max_voltage.val = MIN_VOLT_MEAS;
	bmsdata->max_voltage.cellNum = 0;
	bmsdata->max_voltage.chipIndex = 0;

	bmsdata->max_ocv.val = MIN_VOLT_MEAS;
	bmsdata->max_ocv.cellNum = 0;
	bmsdata->max_ocv.chipIndex = 0;

	bmsdata->min_voltage.val = MAX_VOLT_MEAS;
	bmsdata->min_voltage.cellNum = 0;
	bmsdata->min_voltage.chipIndex = 0;

	bmsdata->min_ocv.val = MAX_VOLT_MEAS;
	bmsdata->min_ocv.cellNum = 0;
	bmsdata->min_ocv.chipIndex = 0;

	uint32_t total_volt = 0;
	uint32_t total_ocv = 0;

	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			/* fings out the maximum cell voltage and location */
			if (bmsdata->chip_data[c].voltage[cell] >
			    bmsdata->max_voltage.val) {
				bmsdata->max_voltage.val =
					bmsdata->chip_data[c].voltage[cell];
				bmsdata->max_voltage.chipIndex = c;
				bmsdata->max_voltage.cellNum = cell;
			}

			if (bmsdata->chip_data[c].open_cell_voltage[cell] >
			    bmsdata->max_ocv.val) {
				bmsdata->max_ocv.val =
					bmsdata->chip_data[c]
						.open_cell_voltage[cell];
				bmsdata->max_ocv.chipIndex = c;
				bmsdata->max_ocv.cellNum = cell;
			}

			/* finds out the minimum cell voltage and location */
			if (bmsdata->chip_data[c].voltage[cell] <
			    bmsdata->min_voltage.val) {
				bmsdata->min_voltage.val =
					bmsdata->chip_data[c].voltage[cell];
				bmsdata->min_voltage.chipIndex = c;
				bmsdata->min_voltage.cellNum = cell;
			}

			if (bmsdata->chip_data[c].open_cell_voltage[cell] <
			    bmsdata->min_ocv.val) {
				bmsdata->min_ocv.val =
					bmsdata->chip_data[c]
						.open_cell_voltage[cell];
				bmsdata->min_ocv.chipIndex = c;
				bmsdata->min_ocv.cellNum = cell;
			}

			total_volt += bmsdata->chip_data[c].voltage[cell];
			total_ocv +=
				bmsdata->chip_data[c].open_cell_voltage[cell];
		}
	}

	/* calculate some voltage stats */
	bmsdata->avg_voltage = total_volt / (NUM_CELLS_PER_CHIP * NUM_CHIPS);
	bmsdata->pack_voltage = total_volt / 1000; /* convert to voltage * 10 */
	bmsdata->delt_voltage =
		bmsdata->max_voltage.val - bmsdata->min_voltage.val;

	bmsdata->avg_ocv = total_ocv / (NUM_CELLS_PER_CHIP * NUM_CHIPS);
	bmsdata->pack_ocv = total_ocv / 1000; /* convert to voltage * 10 */
	bmsdata->delt_ocv = bmsdata->max_ocv.val - bmsdata->min_ocv.val;
}

void calc_cell_resistances()
{
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			uint8_t cell_temp =
				bmsdata->chip_data[c].cell_temp[cell];

			/* resistance LUT increments by 5C for each index */
			uint8_t resIndex = (cell_temp - MIN_TEMP) / 5;

			bmsdata->chip_data[c].cell_resistance[cell] =
				TEMP_TO_CELL_RES[resIndex];

			/* Linear interpolation to more accurately represent cell resistances in between
			 * increments of 5C */
			if (cell_temp != MAX_TEMP) {
				float interpolation =
					(TEMP_TO_CELL_RES[resIndex + 1] -
					 TEMP_TO_CELL_RES[resIndex]) /
					5;
				bmsdata->chip_data[c].cell_resistance[cell] +=
					(interpolation * (cell_temp % 5));
			}
		}
	}
}

void calc_dcl()
{
	static nertimer_t dcl_timer;

	int16_t current_limit = 0x7FFF;

	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			/* Apply equation */
			/* Multiplying resistance by 10 to convert from mOhm to Ohm and then to Ohm * 10000 to
			 * account for the voltage units */
			uint16_t tmpDCL =
				(bmsdata->chip_data[c].open_cell_voltage[cell] -
				 ((MIN_VOLT + VOLT_SAG_MARGIN) * 10000)) /
				(bmsdata->chip_data[c].cell_resistance[cell] *
				 10);

			/* Taking the minimum DCL of all the cells */
			if (tmpDCL < current_limit)
				current_limit = tmpDCL;
		}
	}

	/* ceiling for current limit */
	if (current_limit > MAX_CELL_CURR) {
		bmsdata->discharge_limit = MAX_CELL_CURR;
	}

	/* protection against being init to a high value */
	if (bmsdata->discharge_limit > MAX_CELL_CURR) {
		bmsdata->discharge_limit = 0;
	}

	/* State machine to prevent DCL from plummeting, copy over last DCL for 500ms */
	else if (!is_timer_active(&dcl_timer) && current_limit < 5) {
		if (prevbmsdata == NULL) {
			bmsdata->discharge_limit = current_limit;
			return;
		}

		bmsdata->discharge_limit = prevbmsdata->discharge_limit;
		start_timer(&dcl_timer, 500);
	}

	else if (is_timer_active(&dcl_timer)) {
		if (is_timer_expired(&dcl_timer)) {
			bmsdata->discharge_limit = current_limit;
		}
		if (current_limit > 5) {
			bmsdata->discharge_limit = current_limit;
			cancel_timer(&dcl_timer);
		}

		else {
			bmsdata->discharge_limit = prevbmsdata->discharge_limit;
		}
	} else {
		bmsdata->discharge_limit = current_limit;
	}

	if (bmsdata->discharge_limit > DCDC_CURRENT_DRAW) {
		bmsdata->discharge_limit -= DCDC_CURRENT_DRAW;
	}
}

void calc_cont_dcl()
{
	uint8_t min_res_index =
		(bmsdata->min_temp.val - MIN_TEMP) /
		5; /* resistance LUT increments by 5C for each index */
	uint8_t max_res_index = (bmsdata->max_temp.val - MIN_TEMP) / 5;

	if (TEMP_TO_DCL[min_res_index] < TEMP_TO_DCL[max_res_index]) {
		bmsdata->cont_DCL = TEMP_TO_DCL[min_res_index];
	} else {
		bmsdata->cont_DCL = TEMP_TO_DCL[max_res_index];
	}
}

void calcCCL()
{
	int16_t currentLimit = 0x7FFF;

	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			/* Apply equation */
			uint16_t tmpCCL =
				((MAX_VOLT * 10000) -
				 (bmsdata->chip_data[c].open_cell_voltage[cell] +
				  (VOLT_SAG_MARGIN * 10000))) /
				(bmsdata->chip_data[c].cell_resistance[cell] *
				 10);
			/* Multiplying resistance by 10 to convert from mOhm to Ohm and then to Ohm * 10000 to
			 * account for the voltage units */

			/* Taking the minimum CCL of all the cells */
			if (tmpCCL < currentLimit)
				currentLimit = tmpCCL;
		}
	}

	/* ceiling for current limit */
	if (currentLimit > MAX_CHG_CELL_CURR) {
		bmsdata->charge_limit = MAX_CHG_CELL_CURR;
	} else {
		bmsdata->charge_limit = currentLimit;
	}
}

void calc_cont_ccl()
{
	uint8_t min_res_index =
		(bmsdata->min_temp.val - MIN_TEMP) /
		5; /* resistance LUT increments by 5C for each index */
	uint8_t max_res_index = (bmsdata->max_temp.val - MIN_TEMP) / 5;

	if (TEMP_TO_CCL[min_res_index] < TEMP_TO_CCL[max_res_index]) {
		bmsdata->cont_CCL = TEMP_TO_CCL[min_res_index];
	} else {
		bmsdata->cont_CCL = TEMP_TO_CCL[max_res_index];
	}

	if (bmsdata->cont_CCL > MAX_CHG_CELL_CURR) {
		bmsdata->cont_CCL = MAX_CHG_CELL_CURR;
	}
}

void calc_open_cell_voltage()
{
	/* if there is no previous data point, set inital open cell voltage to current reading */
	if (is_first_reading_) {
		for (uint8_t chip = 0; chip < NUM_CHIPS; chip++) {
			for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP;
			     cell++) {
				bmsdata->chip_data[chip]
					.open_cell_voltage[cell] =
					bmsdata->chip_data[chip].voltage[cell];
			}
		}
		return;
	}
	/* If we are within the current threshold for open voltage measurments */
	else if (bmsdata->pack_current < (OCV_CURR_THRESH * 10) &&
		 bmsdata->pack_current > (-OCV_CURR_THRESH * 10)) {
		if (is_timer_expired(&ocvTimer) ||
		    !is_timer_active(&ocvTimer)) {
			for (uint8_t chip = 0; chip < NUM_CHIPS; chip++) {
				for (uint8_t cell = 0;
				     cell < NUM_CELLS_PER_CHIP; cell++) {
					/* Sets open cell voltage to a moving average of OCV_AVG values */
					bmsdata->chip_data[chip]
						.open_cell_voltage[cell] =
						((uint32_t)(bmsdata->chip_data[chip]
								    .voltage[cell]) +
						 ((uint32_t)(prevbmsdata
								     ->chip_data
									     [chip]
								     .open_cell_voltage
									     [cell]) *
						  (OCV_AVG - 1))) /
						OCV_AVG;
					bmsdata->chip_data[chip]
						.open_cell_voltage[cell] =
						bmsdata->chip_data[chip]
							.voltage[cell];

					if (bmsdata->chip_data[chip]
						    .open_cell_voltage[cell] >
					    MAX_VOLT * 10000) {
						bmsdata->chip_data[chip]
							.open_cell_voltage[cell] =
							prevbmsdata
								->chip_data[chip]
								.open_cell_voltage
									[cell];
					} else if (bmsdata->chip_data[chip]
							   .open_cell_voltage
								   [cell] <
						   MIN_VOLT * 10000) {
						bmsdata->chip_data[chip]
							.open_cell_voltage[cell] =
							prevbmsdata
								->chip_data[chip]
								.open_cell_voltage
									[cell];
					}
				}
			}
			return;
		}
	} else {
		start_timer(&ocvTimer, 1000);
	}
	for (uint8_t chip = 0; chip < NUM_CHIPS; chip++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			/* Set OCV to the previous/existing OCV */
			bmsdata->chip_data[chip].open_cell_voltage[cell] =
				prevbmsdata->chip_data[chip]
					.open_cell_voltage[cell];
		}
	}
}

uint8_t analyzer_calc_fan_pwm()
{
	/* Resistance LUT increments by 5C for each index, plus we account for negative minimum */
	uint8_t min_res_index = (bmsdata->max_temp.val - MIN_TEMP) / 5;
	/* Ints are roounded down, so this would be the value if rounded up */
	uint8_t max_res_index = (bmsdata->max_temp.val - MIN_TEMP) / 5 + 1;
	/* Determine how far into the 5C interval the temp is */
	uint8_t part_of_index = (bmsdata->max_temp.val - MIN_TEMP) % 5;

	/* Uses fan LUT and finds low and upper end. Then takes average, weighted to how far into the
	 * interval the exact temp is */
	return ((FAN_CURVE[max_res_index] * part_of_index) +
		(FAN_CURVE[min_res_index] * (5 - part_of_index))) /
	       (2 * 5);
}

void analyzer_push(acc_data_t *data)
{
	if (prevbmsdata != NULL)
		free(bmsdata);

	prevbmsdata = bmsdata;
	bmsdata = data;

	disable_therms();

	//high_curr_therm_check(); /* = prev if curr > 50 */
	// diff_curr_therm_check();     /* = prev if curr - prevcurr > 10 */
	// variance_therm_check();      /* = prev if val > 5 deg difference */
	// standard_dev_therm_check();  /* = prev if std dev > 3 */
	// averaging_therm_check();     /* matt shitty incrementing */

	calc_cell_temps();
	calc_pack_temps();
	calc_pack_voltage_stats();
	calc_open_cell_voltage();
	calc_cell_resistances();
	calc_dcl();
	calc_cont_dcl();
	//calcCCL();
	calc_cont_ccl();
	calc_state_of_charge();
	calc_noise_volt_percent();

	data->charge_limit = data->cont_CCL;

	is_first_reading_ = false;
}

void disable_therms()
{
	int8_t tmp_temp =
		25; /* Iniitalize to room temp (necessary to stabilize when the BMS first boots up/has null values) */
	if (!is_first_reading_)
		tmp_temp =
			prevbmsdata
				->avg_temp; /* Set to actual average temp of the pack */

	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t therm = 0; therm < NUM_THERMS_PER_CHIP; therm++) {
			/* If 2D LUT shows therm should be disable */
			if (THERM_DISABLE[c][therm]) {
				/* Nullify thermistor by setting to pack average */
				bmsdata->chip_data[c].thermistor_value[therm] =
					tmp_temp;
			} else {
				bmsdata->chip_data[c].thermistor_value[therm] =
					bmsdata->chip_data[c]
						.thermistor_reading[therm];
			}
		}
	}
}

void calc_state_of_charge()
{
	/* Spltting the delta voltage into 18 increments */
	const uint16_t increments =
		((uint16_t)(MAX_VOLT * 10000 - MIN_VOLT * 10000) /
		 ((MAX_VOLT - MIN_VOLT) * 10));

	/* Retrieving a index of 0-18 */
	uint8_t index =
		((bmsdata->min_ocv.val) - MIN_VOLT * 10000) / increments;

	bmsdata->soc = STATE_OF_CHARGE_CURVE[index];

	if (bmsdata->soc != 100) {
		float interpolation = (float)(STATE_OF_CHARGE_CURVE[index + 1] -
					      STATE_OF_CHARGE_CURVE[index]) /
				      increments;
		bmsdata->soc += (uint8_t)(interpolation *
					  (((bmsdata->min_ocv.val) -
					    (int32_t)(MIN_VOLT * 10000)) %
					   increments));
	}

	if (bmsdata->soc < 0) {
		bmsdata->soc = 0;
	}
}

void calc_noise_volt_percent()
{
	int i = 0;
	for (uint8_t seg = 0; seg < NUM_SEGMENTS; seg++) {
		uint8_t count = 0;
		/* merge results from each of the two chips ona  given segment */
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			count = bmsdata->chip_data[seg + i].noise_reading[cell];
			count += bmsdata->chip_data[seg + i + 1]
					 .noise_reading[cell];
		}
		i++;

		/* turn into percentage */
		//printf("count: %d\r\n", count);
		bmsdata->segment_noise_percentage[seg] =
			(uint8_t)(100 * (count) / (NUM_CELLS_PER_CHIP * 2.0f));
	}
}

void high_curr_therm_check()
{
	if (prevbmsdata == NULL)
		return;

	if (bmsdata->pack_current > 500) {
		for (uint8_t c = 0; c < NUM_CHIPS; c++) {
			for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP;
			     cell++) {
				bmsdata->chip_data[c].thermistor_reading[cell] =
					prevbmsdata->chip_data[c]
						.thermistor_reading[cell];
				bmsdata->chip_data[c].thermistor_value[cell] =
					prevbmsdata->chip_data[c]
						.thermistor_value[cell];
			}
		}
	}
}

void diff_curr_therm_check()
{
	if (prevbmsdata == NULL)
		return;

	if (abs(bmsdata->pack_current - prevbmsdata->pack_current) > 100) {
		for (uint8_t c = 0; c < NUM_CHIPS; c++) {
			for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP;
			     cell++) {
				bmsdata->chip_data[c].thermistor_reading[cell] =
					prevbmsdata->chip_data[c]
						.thermistor_reading[cell];
				bmsdata->chip_data[c].thermistor_value[cell] =
					prevbmsdata->chip_data[c]
						.thermistor_value[cell];
			}
		}
	}
}
