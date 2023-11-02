#include "segment.h"

#define THERM_WAIT_TIME		 500 /* ms */
#define VOLTAGE_WAIT_TIME	 100 /* ms */
#define THERM_AVG			 15	 /* Number of values to average */
#define MAX_VOLT_DELTA		 2500
#define MAX_VOLT_DELTA_COUNT 10

uint8_t therm_avg_counter = 0;

Timer therm_timer;
Timer voltage_reading_timer;
Timer variance_timer;

FaultStatus_t voltage_error = NOT_FAULTED;
FaultStatus_t therm_error = NOT_FAULTED;

uint16_t therm_settle_time_ = 0;

const uint32_t VOLT_TEMP_CONV[91] = { 44260, 43970, 43670, 43450, 43030, 42690, 42340, 41980, 41620,
	41240, 40890, 40460, 40040, 39580, 39130, 38660, 38210, 37710, 37210, 36190, 35670, 35160,
	34620, 34080, 33550, 32990, 32390, 31880, 31270, 30690, 30160, 29590, 28990, 28450, 27880,
	27270, 26740, 26080, 25610, 25000, 24440, 23880, 23330, 22780, 22240, 21700, 21180, 20660,
	20150, 19640, 19140, 18650, 18170, 17700, 17230, 16780, 16330, 15890, 15470, 15030, 14640,
	14230, 13850, 13450, 13070, 12710, 11490, 11170, 10850, 10550, 10250, 9960, 9670, 9400, 9130,
	8870, 8620, 8370, 8130, 7900, 0 };

const int32_t VOLT_TEMP_CALIB_OFFSET = 0;

void segment_init()
{
	printf("Initializing Segments...");

	LTC6804_initialize();

	// pull_chip_configuration();

	for (int c = 0; c < NUM_CHIPS; c++) {
		local_config[c][0] = 0xF8;
		local_config[c][1] = 0x19; /* VUV = 0x619 = 1561 -> 2.4992V */
		local_config[c][2] = 0x06; /* VOV = 0xA60 = 2656 -> 4.2496V */
		local_config[c][3] = 0xA6;
		local_config[c][4] = 0x00;
		local_config[c][5] = 0x00;
	}
	push_chip_configurations();
}

void segment_retrieve_data(ChipData_t databuf[NUM_CHIPS])
{
	segment_data = databuf;

	/* Pull voltages and thermistors and indiacte if there was a problem during
	 * retrieval */
	voltage_error = pull_voltages();
	therm_error = pull_thermistors();

	/* Save the contents of the reading so that we can use it to fill in missing
	 * data */
	memcpy(previous_data, segment_data, sizeof(ChipData_t) * NUM_CHIPS);

	segment_data = nullptr;
}

void configure_discharge(uint8_t chip, uint16_t cells)
{
	/*
	 * chipConfigurations[chip][4] == chipConfigurations[Literally what chip you
	 * want][register] 4 and 5 are registers to discharge chips
	 */
	local_config[chip][4] = uint8_t(cells & 0x00FF);

	/*
	 * Register 5 is split in half, so we maintain the upper half and add in the
	 * bottom half to discharge cells
	 */
	local_config[chip][5] = (local_config[chip][5] & 0xF0) + uint8_t(cells >> 8);
}

void segment_enable_balancing(bool balance_enable)
{
	/*
	 * Discharging all cells in series
	 * Making the discharge command all 1's for all cells per chip
	 */
	static const uint16_t DICHARGE_ALL_COMMAND = 0xFFFF >> (16 - NUM_CELLS_PER_CHIP);

	if (balance_enable) {
		for (int c = 0; c < NUM_CHIPS; c++) {
			configureDischarge(c, DICHARGE_ALL_COMMAND);
			discharge_commands[c] = DICHARGE_ALL_COMMAND;
		}
		push_chip_configuration();
	} else {
		for (int c = 0; c < NUM_CHIPS; c++) {
			configureDischarge(c, 0);
			discharge_commands[c] = 0;
		}
		push_chip_configuration();
	}
}

// @todo Revisit after testing
void segment_enable_balancing(uint8_t chip_num, uint8_t cell_num, bool balance_enable)
{
	pull_chip_configurations();

	if (balance_enable)
		discharge_commands[chip_num] |= (1 << cell_num);
	else
		discharge_commands[chip_num] &= ~(1 << cell_num);

	configure_discharge(chip_num, discharge_commands[chip_num]);

	push_chip_configurations();
}

void segment_configure_balancing(bool discharge_config[NUM_CHIPS][NUM_CELLS_PER_CHIP])
{
	for (int c = 0; c < NUM_CHIPS; c++) {
		for (int cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			if (discharge_config[c][cell])
				discharge_commands[c] |= 1 << cell;
			else
				discharge_commands[c] &= ~(1 << cell);
		}

		configure_discharge(c, discharge_commands[c]);
	}
	push_chip_configurations();
}

bool segment_is_balancing(uint8_t chip_num, uint8_t cell_num)
{
	/* If the cell is one of the first 8, check the 4th register */
	if (cell_num < 8) {
		return local_config[chip_num][4] & (1 << cell_num);
	}
	/* If the cell number is greater than 8, check the 5th register */
	else {
		return local_config[chip_num][5] & (1 << (cell_num - 8));
	}

	return false; /* default case */
}

bool segment_is_balancing()
{
	for (int c = 0; c < NUM_CHIPS; c++) {
		/* Reading from the 4th config register */
		for (int cell = 0; cell < 8; cell++) {
			if (local_config[c][4] & (1 << cell))
				return true;
		}

		/* Reading from the 5th config register */
		for (int cell = 0; cell < 4; cell++) {
			if (local_config[c][5] & (1 << (cell)))
				return true;
		}
	}

	return false;
}

void pull_chip_configurations()
{
	uint8_t remote_config[NUM_CHIPS][8];
	LTC6804_rdcfg(NUM_CHIPS, remote_config);

	for (int chip = 0; chip < NUM_CHIPS; chip++) {
		for (int index = 0; index < 6; index++) {
			local_config[chip][index] = remote_config[chip][index];
		}
	}
}

void push_chip_configurations() { LTC6804_wrcfg(NUM_CHIPS, local_config); }

FaultStatus_t pull_voltages()
{
	/**
	 * If we haven't waited long enough between pulling voltage data
	 * just copy over the contents of the last good reading and the fault status
	 * from the most recent attempt
	 */
	if (!voltage_reading_timer.isTimerExpired()) {
		for (uint8_t i = 0; i < NUM_CHIPS; i++) {
			memcpy(segment_data[i].voltage_reading, previous_data[i].voltage_reading,
				sizeof(segment_data[i].voltage_reading));
		}
		return voltage_error;
	}

	uint16_t segment_voltages[NUM_CHIPS][12];

	push_chip_configurations();
	LTC6804_adcv();

	/**
	 * If we received an incorrect PEC indicating a bad read
	 * copy over the data from the last good read and indicate an error
	 */
	if (LTC6804_rdcv(0, NUM_CHIPS, segment_voltages) == -1) {
		for (uint8_t i = 0; i < NUM_CHIPS; i++) {
			memcpy(segment_data[i].voltage_reading, previous_data[i].voltage_reading,
				sizeof(segment_data[i].voltage_reading));
		}
		return FAULTED;
	}

	/* If the read was successful, copy the voltage data */
	for (uint8_t i = 0; i < NUM_CHIPS; i++) {
		for (uint8_t j = 0; j < NUM_CELLS_PER_CHIP; j++) {
			if (abs(segment_voltages[i][j] - previous_data[i].voltage_reading[j])
				> MAX_VOLT_DELTA) {
				segment_data[i].voltage_reading[j] = previous_data[i].voltage_reading[j];
				segment_data[i].bad_volt_diff_count[j]++;

				if (segment_data[i].bad_volt_diff_count[j] > MAX_VOLT_DELTA_COUNT) {
					segment_data[i].bad_volt_diff_count[j] = 0;
					segment_data[i].voltage_reading[j] = segment_voltages[i][j];
				}
			} else {
				segment_data[i].bad_volt_diff_count[j] = 0;
				segment_data[i].voltage_reading[j] = segment_voltages[i][j];
			}
		}
	}

	/* Start the timer between readings if successful */
	voltage_reading_timer.startTimer(VOLTAGE_WAIT_TIME);
	return NOT_FAULTED;
}

FaultStatus_t pull_thermistors()
{
	/* If polled too soon, just copy existing values from memory */
	if (!therm_timer.isTimerExpired()) {
		for (uint8_t i = 0; i < NUM_CHIPS; i++) {
			memcpy(segment_data[i].thermistor_reading, previous_data[i].thermistor_reading,
				sizeof(segment_data[i].thermistor_reading));
			memcpy(segment_data[i].thermistor_value, previous_data[i].thermistor_value,
				sizeof(segment_data[i].thermistor_value));
		}
		return voltage_error;
	}

	uint16_t raw_temp_voltages[NUM_CHIPS][6];

	/* Rotate through all thermistor pairs (we can poll two at once) */
	for (int therm = 1; therm <= 16; therm++) {
		/* Sets multiplexors to select thermistors */
		select_therm(therm);
		select_therm(therm + 16);

		push_chip_configurations();
		LTC6804_adax();									/* Run ADC for AUX (GPIOs and refs) */
		LTC6804_rdaux(0, NUM_CHIPS, raw_temp_voltages); /* Fetch ADC results from AUX registers */

		for (uint8_t c = 0; c < NUM_CHIPS; c++) {
			/*
			 * Get current temperature LUT. Voltage is adjusted to account for 5V reg
			 * fluctuations (index 2 is a reading of the ADC 5V ref)
			 */
			segment_data[c].thermistor_reading[therm - 1]
				= steinhart_est(raw_temp_voltages[c][0] * (float(raw_temp_voltages[c][2]) / 50000)
					+ VOLT_TEMP_CALIB_OFFSET);
			segment_data[c].thermistor_reading[therm + 15]
				= steinhart_est(raw_temp_voltages[c][1] * (float(raw_temp_voltages[c][2]) / 50000)
					+ VOLT_TEMP_CALIB_OFFSET);

			/* Directly update for a set time from start up due to therm voltages
			 * needing to settle */
			segment_data[c].thermistor_value[therm - 1]
				= segment_data[c].thermistor_reading[therm - 1];
			segment_data[c].thermistor_value[therm + 15]
				= segment_data[c].thermistor_reading[therm + 15];

			if (raw_temp_voltages[c][0] == LTC_BAD_READ
				|| raw_temp_voltages[c][1] == LTC_BAD_READ) {
				memcpy(segment_data[c].thermistor_reading, previous_data[c].thermistor_reading,
					sizeof(segment_data[c].thermistor_reading));
				memcpy(segment_data[c].thermistor_value, previous_data[c].thermistor_value,
					sizeof(segment_data[c].thermistor_value));
			}
		}
	}
	therm_timer.startTimer(THERM_WAIT_TIME); /* Set timeout for reading therms */
	variance_therm_check();
	// standard_dev_therm_check();
	// averaging_therm_check();
	discard_neutrals();

	return NOT_FAULTED; /* Read successfully */
}

void select_therm(uint8_t therm)
{
	/* Exit if out of range values */
	if (therm < 0 || therm > 32) {
		return;
	}
	uint8_t i2c_write_data[NUM_CHIPS][3];
	uint8_t comm_reg_data[NUM_CHIPS][6];
	if (therm <= 8) {
		/* Turn off competing multiplexor (therms 9-16) */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x92;
			i2c_write_data[chip][1] = 0x00;
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configurations();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);

		/* Turn on desired thermistor */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x90;
			i2c_write_data[chip][1] = 0x08 + (therm - 1);
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configurations();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);
	} else if (therm <= 16) {
		/* Turn off competing multiplexor (therms 1-8) */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x90;
			i2c_write_data[chip][1] = 0x00;
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configurations();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);

		/* Turn on desired thermistor */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x92;
			i2c_write_data[chip][1] = 0x08 + (therm - 9);
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configuration();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);
	} else if (therm <= 24) {
		/* Turn off competing multiplexor (therms 25-32) */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x96;
			i2c_write_data[chip][1] = 0x00;
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configuration();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);

		/* Turn on desired thermistor */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x94;
			i2c_write_data[chip][1] = 0x08 + (therm - 17);
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configuration();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);
	} else {
		/* Turn off competing multiplexor (therms 17-24) */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x94;
			i2c_write_data[chip][1] = 0x00;
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configuration();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);

		/* Turn on desired thermistor */
		for (int chip = 0; chip < NUM_CHIPS; chip++) {
			i2c_write_data[chip][0] = 0x96;
			i2c_write_data[chip][1] = 0x08 + (therm - 25);
			i2c_write_data[chip][2] = 0x00;
		}
		serialize_i2c_msg(i2c_write_data, comm_reg_data);
		push_chip_configuration();
		LTC6804_wrcomm(NUM_CHIPS, comm_reg_data);
		LTC6804_stcomm(24);
	}
}

int8_t steinhart_est(uint16_t V)
{
	int i = 0;
	while (V < VOLT_TEMP_CONV[i])
		i++;
	return i + MIN_TEMP;
}

void disable_gpio_pulldowns()
{
	delay(1000);
	/* Turn OFF GPIO 1 & 2 pull downs */
	pull_chip_configuration();
	for (int c = 0; c < NUM_CHIPS; c++) {
		local_config[c][0] |= 0x18;
	}
	push_chip_configuration();

	pull_chip_configuration();
	printf("Chip CFG:\n");
	for (int c = 0; c < NUM_CHIPS; c++) {
		for (int byte = 0; byte < 6; byte++) {
			printf(local_config[c][byte], HEX);
			printf("\t");
		}
		printf("\n");
	}
	printf("Done\n");
}

void serialize_i2c_msg(uint8_t data_to_write[][3], uint8_t comm_output[][6])
{
	for (int chip = 0; chip < NUM_CHIPS; chip++) {
		comm_output[chip][0] = 0x60 | (data_to_write[chip][0] >> 4); /* START + high side of B0 */
		comm_output[chip][1] = (data_to_write[chip][0] << 4) | 0x00; /* low side of B0 + ACK */
		comm_output[chip][2] = 0x00 | (data_to_write[chip][1] >> 4); /* BLANK + high side of B1 */
		comm_output[chip][3] = (data_to_write[chip][1] << 4) | 0x00; /* low side of B1 + ACK */
		comm_output[chip][4] = 0x00 | (data_to_write[chip][2] >> 4); /* BLANK + high side of B2 */
		comm_output[chip][5]
			= (data_to_write[chip][2] << 4) | 0x09; /* low side of B2 + STOP & NACK */
	}
}

void averaging_therm_check()
{
	for (int therm = 1; therm <= 16; therm++) {
		for (int c = 0; c < NUM_CHIPS; c++) {
			/* Directly update for a set time from start up due to therm voltages
			 * needing to settle */
			if (therm_avg_counter < THERM_AVG * 10) {
				segment_data[c].thermistor_value[therm - 1]
					= segment_data[c].thermistor_reading[therm - 1];
				segment_data[c].thermistor_value[therm + 15]
					= segment_data[c].thermistor_reading[therm + 15];
				therm_avg_counter++;
			} else {
				/* We need to investigate this. Very sloppy */
				/* Discard if reading is 33C */
				if (segment_data[c].thermistor_reading[therm - 1] != 33) {
					/* If measured value is larger than current "averaged" value,
					 * increment value */
					if (segment_data[c].thermistor_reading[therm - 1]
						> segment_data[c].thermistor_value[therm - 1]) {
						segment_data[c].thermistor_value[therm - 1]++;
						/* If measured value is smaller than current "averaged" value,
						 * decrement value */
					} else if (segment_data[c].thermistor_reading[therm - 1]
						< segment_data[c].thermistor_value[therm - 1]) {
						segment_data[c].thermistor_value[therm - 1]--;
					}
				}

				/* See comments above. Identical but for the upper 16 therms */
				if (segment_data[c].thermistor_reading[therm + 15] != 33) {
					if (segment_data[c].thermistor_reading[therm + 15]
						> segment_data[c].thermistor_value[therm + 15]) {
						segment_data[c].thermistor_value[therm + 15]++;
					} else if (segment_data[c].thermistor_reading[therm + 15]
						< segment_data[c].thermistor_value[therm + 15]) {
						segment_data[c].thermistor_value[therm + 15]--;
					}
				}
			}
		}
	}
}

void standard_dev_therm_check()
{
	if (previous_data == nullptr)
		return;
	int16_t avg_temp = calc_average();
	uint8_t standard_dev = calc_therm_standard_dev(avg_temp);
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t therm = 17; therm < 28; therm++) {
			/*
			 * If difference between thermistor and average is more than
			 * MAX_STANDARD_DEV set the therm to pack average
			 */
			if (abs(segment_data[c].thermistor_value[therm] - avg_temp)
				> (MAX_STANDARD_DEV * standard_dev)) {
				/* Nullify thermistor by setting to pack average */
				segment_data[c].thermistor_value[therm] = previous_data[c].thermistor_value[therm];
			}
		}
	}
}

int8_t calc_therm_standard_dev(int16_t avg_temp)
{
	uint16_t sum_diff_sqrd = 0;
	for (uint8_t chip = 0; chip < NUM_CHIPS; chip++) {
		for (uint8_t therm = 17; therm < 28; therm++) {
			uint16_t sum_diff = abs(segment_data[chip].thermistor_value[therm] - avg_temp);
			sum_diff_sqrd += sum_diff * sum_diff;
		}
	}

	uint8_t standard_dev = sqrt(sum_diff_sqrd / 88);
	if (standard_dev < 8) {
		standard_dev = 8;
	}
	return standard_dev;
}

int16_t calc_average()
{
	int16_t avg = 0;
	for (int chip = 0; chip < NUM_CHIPS; chip++) {
		for (int therm = 17; therm < 28; therm++) {
			avg += segment_data[chip].thermistor_value[therm];
		}
	}

	avg = avg / (NUM_CHIPS * 11);
	return avg;
}

void variance_therm_check()
{
	if (previous_data == nullptr) {
		variance_timer.startTimer(1000);
		return;
	}

	if (variance_timer.isTimerExpired()) {
		for (uint8_t c = 0; c < NUM_CHIPS; c++) {
			for (uint8_t therm = 17; therm < 28; therm++) {
				if (abs(segment_data[c].thermistor_reading[therm]
						- previous_data[c].thermistor_reading[therm])
						> 5
					&& (segment_data[c].thermistor_reading[therm] < 10
						|| segment_data[c].thermistor_reading[therm] > 30)) {
					segment_data[c].thermistor_reading[therm]
						= previous_data[c].thermistor_reading[therm];
					segment_data[c].thermistor_value[therm]
						= previous_data[c].thermistor_value[therm];
				}
			}
		}
	}
}

void discard_neutrals()
{
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t therm = 17; therm < 28; therm++) {
			if (segment_data[c].thermistor_reading[therm] == 33) {
				segment_data[c].thermistor_reading[therm] = 25;
				segment_data[c].thermistor_value[therm] = 25;
			}
		}
	}
}