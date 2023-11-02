#include "compute.h"

uint8_t fan_speed;
bool is_charging_enabled;
enum { CHARGE_ENABLED, CHARGE_DISABLED };

ComputeInterface compute;

void compute_init()
{
	// TODO UPDATE DRIVER HERE
	pinMode(CURRENT_SENSOR_PIN_H, INPUT);
	pinMode(CURRENT_SENSOR_PIN_L, INPUT);
	pinMode(MEAS_5VREF_PIN, INPUT);
	pinMode(FAULT_PIN, OUTPUT);
	pinMode(CHARGE_DETECT, INPUT);
	initializeCAN(CANLINE_2, CHARGER_BAUD, &(this->compute_charger_callback));
	initializeCAN(CANLINE_1, MC_BAUD, &(this->compute_mc_callback));
}

void compute_enable_charging(bool enable_charging)
{
	is_charging_enabled_ = enable_charging;
}

FaultStatus_t compute_send_charging_message(uint16_t voltage_to_set, AccumulatorData_t* bms_data)
{
	struct __attribute__((packed)) {
		uint8_t charger_control;
		uint16_t charger_voltage; // Note the charger voltage sent over should be 10*desired voltage
		uint16_t charger_current; // Note the charge current sent over should be 10*desired current
								  // + 3200
		uint8_t charger_leds;
		uint16_t reserved2_3;
	} charger_msg;

	uint16_t current_to_set = bms_data->charge_limit;

	if (!is_charging_enabled_) {
		charger_msg.charger_control = 0b101;
		sendMessageCAN2(CANMSG_CHARGER, 8, charger_msg);
		// return isCharging() ? FAULTED : NOT_FAULTED; //return a fault if we DO detect a voltage
		// after we stop charging
		return NOT_FAULTED;
	}

	// equations taken from TSM2500 CAN protocol datasheet
	charger_msg.charger_control = 0xFC;
	charger_msg.charger_voltage = voltage_to_set * 10;
	if (current_to_set > 10) {
		current_to_set = 10;
	}
	charger_msg.charger_current = current_to_set * 10 + 3200;
	charger_msg.charger_leds	= calc_charger_led_state(bms_data);
	charger_msg.reserved2_3		= 0xFFFF;

	unit8_t buf[8] = { 0 };
	memcpy(buf, &charger_msg, sizeof(charger_msg));

	sendMessageCAN2(CANMSG_CHARGER, 8, buf);

	// return isCharging() ? NOT_FAULTED : FAULTED; //return a fault if we DON'T detect a voltage
	// after we begin charging
	return NOT_FAULTED;
}

bool compute_charger_connected()
{
	return !(digitalRead(CHARGE_DETECT) == HIGH);
}

void compute_charger_callback(const CAN_message_t& msg)
{
	return;
}

void compute_set_fan_speed(uint8_t new_fan_speed)
{
	fan_speed_ = new_fan_speed;
	// NERduino.setAMCDutyCycle(new_fan_speed);  Replace
}

void compute_set_fault(FaultStatus_t fault_state)
{
	digitalWrite(FAULT_PIN, !fault_state);
	if (FAULTED)
		digitalWrite(CHARGE_SAFETY_RELAY, HIGH);
}

int16_t compute_get_pack_current()
{
	static const float CURRENT_LOWCHANNEL_MAX = 75.0;  // Amps
	static const float CURRENT_LOWCHANNEL_MIN = -75.0; // Amps
	// static const float CURRENT_SUPPLY_VOLTAGE = 5.038;
	static const float CURRENT_ADC_RESOLUTION = 5.0 / MAX_ADC_RESOLUTION;

	static const float CURRENT_LOWCHANNEL_OFFSET  = 2.517; // Calibrated with current = 0A
	static const float CURRENT_HIGHCHANNEL_OFFSET = 2.520; // Calibrated with current = 0A

	static const float HIGHCHANNEL_GAIN = 1 / 0.004; // Calibrated with  current = 5A, 10A, 20A
	static const float LOWCHANNEL_GAIN	= 1 / 0.0267;

	static const float REF5V_DIV  = 19.02 / (19.08 + 19.02); // Resistive divider in kOhm
	static const float REF5V_CONV = 1 / REF5V_DIV; // Converting from reading to real value

	float ref_5V = analogRead(MEAS_5VREF_PIN) * (3.3 / MAX_ADC_RESOLUTION) * REF5V_CONV;
	int16_t high_current
		= 10
		  * (((5 / ref_5V) * (analogRead(CURRENT_SENSOR_PIN_L) * CURRENT_ADC_RESOLUTION))
			 - CURRENT_HIGHCHANNEL_OFFSET)
		  * HIGHCHANNEL_GAIN; // Channel has a large range with low resolution
	int16_t low_current
		= 10
		  * (((5 / ref_5V) * (analogRead(CURRENT_SENSOR_PIN_H) * CURRENT_ADC_RESOLUTION))
			 - CURRENT_LOWCHANNEL_OFFSET)
		  * LOWCHANNEL_GAIN; // Channel has a small range with high resolution

	// Serial.print("High: ");
	// Serial.println(-high_current);
	// Serial.print("Low: ");
	// Serial.println(-low_current);
	// Serial.print("5V: ");
	// Serial.println(ref_5V);

	// If the current is scoped within the range of the low channel, use the low channel
	if (low_current < CURRENT_LOWCHANNEL_MAX - 5.0 || low_current > CURRENT_LOWCHANNEL_MIN + 5.0) {
		return -low_current;
	}

	return -high_current;
}

void compute_send_mc_message(uint16_t user_max_charge, uint16_t user_max_discharge)
{

	struct __attribute__((packed)) {
		uint16_t maxDischarge;
		uint16_t maxCharge;

	} mcMsg;

	mcMsg.maxCharge	   = user_max_charge;
	mcMsg.maxDischarge = user_max_discharge;

	unit8_t buf[4] = { 0 };
	memcpy(buf, &mcMsg, sizeof(mcMsg));

	sendMessageCAN1(CANMSG_BMSCURRENTLIMITS, 4, buf);
}

void compute_send_acc_status_message(AccumulatorData_t* bmsdata)
{

	struct __attribute__((packed)) {
		uint16_t packVolt;
		uint16_t pack_current;
		uint16_t pack_ah;
		uint8_t pack_soc;
		uint8_t pack_health;
	} acc_status_msg;

	acc_status_msg.cfg.packVolt		= __builtin_bswap16(bmsdata->pack_voltage);
	acc_status_msg.cfg.pack_current = __builtin_bswap16(
		static_cast<uint16_t>(bmsdata->pack_current)); // convert with 2s complement
	acc_status_msg.cfg.pack_ah	   = __builtin_bswap16(0);
	acc_status_msg.cfg.pack_soc	   = bmsdata->soc;
	acc_status_msg.cfg.pack_health = 0;

	unit8_t buf[8] = { 0 };
	memcpy(buf, &acc_status_msg, sizeof(acc_status_msg));
	sendMessageCAN1(CANMSG_BMSACCSTATUS, 8, buf);
}

void compute_send_bms_status_message(AccumulatorData_t* bmsdata, int bms_state, bool balance)
{

	struct __attribute__((packed)) {
		uint8_t state;
		uint32_t fault;
		int8_t temp_avg;
		uint8_t temp_internal;
		uint8_t balance;
	} bms_status_msg;

	bms_status_msg.temp_avg		 = static_cast<int8_t>(bms_data->avg_temp);
	bms_status_msg.state		 = static_cast<uint8_t>(bms_state);
	bms_status_msg.fault		 = bmsdata->fault_code;
	bms_status_msg.temp_internal = static_cast<uint8_t>(0);
	bms_status_msg.balance		 = static_cast<uint8_t>(balance);

	/* uint8_t msg[8] = {
						bms_status_msg.cfg.state,
					   (fault_status & 0xff000000),
					   (fault_status & 0x00ff0000),
					   (fault_status & 0x0000ff00),
					   (fault_status & 0x000000ff),
						bms_status_msg.cfg.temp_avg,
						bms_status_msg.cfg.balance
					};
   */
	unit8_t buf[8] = { 0 };
	memcpy(buf, &bms_status_msg, sizeof(bmsStatusMsg));
	sendMessageCAN1(CANMSG_BMSDTCSTATUS, 8, buf);
}

void compute_send_shutdown_ctrl_message(uint8_t mpe_state)
{

	struct __attribute__((packed)) {
		uint8_t mpeState;

	} shutdownControlMsg;

	shutdownControlMsg.mpeState = mpe_state;

	unit8_t buf[1] = { 0 };
	memcpy(buf, &compute_send_shutdown_ctrl_message, sizeof(compute_send_shutdown_ctrl_message));
	sendMessageCAN1(0x03, 1, buff);
}

void compute_send_cell_data_message(AccumulatorData_t* bmsdata)
{
	struct __attribute__((packed)) {
		uint16_t high_cell_voltage;
		uint8_t high_cell_id;
		uint16_t low_cell_voltage;
		uint8_t low_cell_id;
		uint16_t volt_avg;
	} cell_data_msg;

	cell_data_msg.high_cell_voltage = bmsdata->max_voltage.val;
	cell_data_msg.high_cell_id		= bmsdata->max_voltage.chipIndex;
	cell_data_msg.low_cell_voltage	= bmsdata->min_voltage.val;
	// Chip number                           Cell number
	cell_data_msg.low_cell_id
		= (bmsdata->min_voltage.chipIndex << 4) | bmsdata->min_voltage.cellNum;
	cell_data_msg.volt_avg = bmsdata->avg_voltage;

	/* uint8_t msg[8] = {
						( cell_data_msg.cfg.high_cell_voltage & 0x00ff),
						((cell_data_msg.cfg.high_cell_voltage & 0xff00)>>8),
						  cell_data_msg.cfg.high_cell_id,
						( cell_data_msg.cfg.low_cell_voltage & 0x00ff),
						((cell_data_msg.cfg.low_cell_voltage & 0xff00)>>8),
						  cell_data_msg.cfg.low_cell_id,
						( cell_data_msg.cfg.volt_avg & 0x00ff),
						((cell_data_msg.cfg.volt_avg & 0xff00)>>8)
					 };
	*/
	unit8_t buf[8] = { 0 };
	memcpy(buf, &cell_data_msg, sizeof(cell_data_msg));
	sendMessageCAN1(CANMSG_BMSCELLDATA, 8, buf);
}

void compute_send_cell_voltage_message(uint8_t cell_id, uint16_t instant_voltage,
									   uint16_t internal_Res, uint8_t shunted,
									   uint16_t open_voltage)
{
	struct __attribute__((packed)) {
		uint8_t cellID;
		uint16_t instantVoltage;
		uint16_t internalResistance;
		uint8_t shunted;
		uint16_t openVoltage;
	} cellVoltageMsg;

	cellVoltageMsg.cellID			  = cell_id;
	cellVoltageMsg.instantVoltage	  = __builtin_bswap16(instant_voltage);
	cellVoltageMsg.internalResistance = __builtin_bswap16(internal_Res);
	cellVoltageMsg.shunted			  = shunted;
	cellVoltageMsg.openVoltage		  = __builtin_bswap16(open_voltage);

	unit8_t buf[8] = { 0 };
	memcpy(0x07, &cellVoltageMsg, sizeof(cellVoltageMsg));

	sendMessageCAN1(0x07, 8, buf);
}

void compute_send_current_message(AccumulatorData_t* bmsdata)
{
	struct __attribute__((packed)) {
		uint16_t dcl;
		uint16_t ccl;
		uint16_t pack_curr;
	} current_status_msg;

	current_status_msg.dcl		 = bmsdata->discharge_limit;
	current_status_msg.ccl		 = bmsdata->charge_limit;
	current_status_msg.pack_curr = bmsdata->pack_current;

	uint8_t buf[8] = { 0 };
	memcpy(buf, &current_status_msg, sizeof(current_status_msg));

	sendMessageCAN1(CANMSG_BMSCURRENTS, 8, buf);
}

void compute_mc_callback(const CAN_message_t& currentStatusMsg)
{
	return;
}

void compute_send_cell_temp_message(AccumulatorData_t* bmsdata)
{

	struct __attribute__((packed)) {
		uint16_t max_cell_temp;
		uint8_t max_cell_id;
		uint16_t min_cell_temp;
		uint8_t min_cell_id;
		uint16_t average_temp;
	} cell_temp_msg;

	cell_temp_msg.max_cell_temp = bmsdata->max_temp.val;
	cell_temp_msg.max_cell_id
		= (bmsdata->max_temp.chipIndex << 4) | (bmsdata->max_temp.cellNum - 17);
	cell_temp_msg.min_cell_temp = bmsdata->min_temp.val;
	cell_temp_msg.min_cell_id
		= (bmsdata->min_temp.chipIndex << 4) | (bmsdata->min_temp.cellNum - 17);
	cell_temp_msg.average_temp = bmsdata->avg_temp;

	/*
	uint8_t msg[8] = {
						( cell_temp_msg.cfg.max_cell_temp & 0x00ff),
						((cell_temp_msg.cfg.max_cell_temp & 0xff00)>>8),
						  cell_temp_msg.cfg.max_cell_id,
						( cell_temp_msg.cfg.min_cell_temp & 0x00ff),
						((cell_temp_msg.cfg.min_cell_temp & 0xff00)>>8),
						  cell_temp_msg.cfg.min_cell_id,
						( cell_temp_msg.cfg.average_temp & 0x00ff),
						((cell_temp_msg.cfg.average_temp & 0xff00)>>8)
					 };
	*/
	unit8_t buf[8] = { 0 };
	memcpy(buf, &cell_temp_msg, sizeof(cell_temp_msg));
	sendMessageCAN1(0x08, 8, buf);
}

void send_segment_temp_message(AccumulatorData_t* bmsdata)
{

	struct __attribute__((packed)) {
		int8_t segment1_average_temp;
		int8_t segment2_average_temp;
		int8_t segment3_average_temp;
		int8_t segment4_average_temp;
	} segment_temp_msg;

	segment_temp_msg.segment1_average_temp = bmsdata->segment_average_temps[0];
	segment_temp_msg.segment2_average_temp = bmsdata->segment_average_temps[1];
	segment_temp_msg.segment3_average_temp = bmsdata->segment_average_temps[2];
	segment_temp_msg.segment4_average_temp = bmsdata->segment_average_temps[3];
	unit8_t buff[4]						   = { 0 };
	memcpy(buff & segment_temp_msg, sizeof(segment_temp_msg));
	// Ask about these
	sendMessageCAN1(0x09, 4, buff);
}

uint8_t calc_charger_led_state(AccumulatorData_t* bms_data)
{
	enum LED_state {
		RED_BLINKING	   = 0x00,
		RED_CONSTANT	   = 0x01,
		YELLOW_BLINKING	   = 0x02,
		YELLOW_CONSTANT	   = 0x03,
		GREEN_BLINKING	   = 0x04,
		GREEN_CONSTANT	   = 0x05,
		RED_GREEN_BLINKING = 0x06
	};

	if ((bms_data->soc < 80) && (bms_data->pack_current > .5 * 10)) {
		return RED_BLINKING;
	} else if ((bms_data->soc < 80) && (bms_data->pack_current <= .5 * 10)) {
		return RED_CONSTANT;
	} else if ((bms_data->soc >= 80 && bms_data->soc < 95) && (bms_data->pack_current > .5 * 10)) {
		return YELLOW_BLINKING;
	} else if ((bms_data->soc >= 80 && bms_data->soc < 95) && (bms_data->pack_current <= .5 * 10)) {
		return YELLOW_CONSTANT;
	} else if ((bms_data->soc >= 95) && (bms_data->pack_current > .5 * 10)) {
		return GREEN_BLINKING;
	} else if ((bms_data->soc >= 95) && (bms_data->pack_current <= .5 * 10)) {
		return GREEN_CONSTANT;
	} else {
		return RED_GREEN_BLINKING;
	}
}

void compute_send_dcl_prefault_message(bool prefault)
{
	uint8_t msg[1] = { prefault };
	sendMessageCAN1(0x500, 1, msg);
}
