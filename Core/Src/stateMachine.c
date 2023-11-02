#include "stateMachine.h"

AccumulatorData_t* prevAccData;
uint32_t bms_fault = FAULTS_CLEAR;

BMSState_t current_state = BOOT_STATE;
uint32_t previousFault = 0;

tristate_timer over_curr_tmr;
tristate_timer over_chgcurr_tmr;
tristate_timer under_volt_tmr;
tristate_timer over_voltcharge_tmr;
tristate_timer overVolt_tmr;
tristate_timer lowCell_tmr;
tristate_timer highTemp_tmr;

tristate_timer prefaultOverCurr_tmr;
tristate_timer prefaultLowCell_tmr;

Timer charge_timeout;
tristate_timer charge_cut_off_time;

Timer prefaultCANDelay1; // low cell
Timer prefaultCANDelay2; // dcl

Timer can_msg_timer;

bool entered_faulted = false;

Timer charge_message_timer;
static const uint16_t CHARGE_MESSAGE_WAIT = 250; /* ms */

const bool valid_transition_from_to[NUM_STATES][NUM_STATES] = {
	/*   BOOT,     READY,      CHARGING,   FAULTED	*/
	{ true, true, false, true }, /* BOOT */
	{ false, true, true, true }, /* READY */
	{ false, true, true, true }, /* CHARGING */
	{ true, false, false, true } /* FAULTED */
};

typedef void (*HandlerFunction_t)(AccumulatorData_t* bmsdata);
typedef void (*InitFunction_t)();

const InitFunction_t init_LUT[NUM_STATES]
	= { &init_boot, &init_ready, &init_charging, &init_faulted };

const HandlerFunction_t handler_LUT[NUM_STATES]
	= { &handle_boot, &handle_ready, &handle_charging, &handle_faulted };

void init_boot() { return; }

void handle_boot(AccumulatorData_t* bmsdata)
{
	prevAccData = nullptr;
	segment.enableBalancing(false);
	compute.compute_enable_charging(false);

	// bmsdata->fault_code = FAULTS_CLEAR;

	request_transition(READY_STATE);
	return;
}

void init_ready()
{
	segment.enableBalancing(false);
	compute.compute_enable_charging(false);
	return;
}

void handle_ready(AccumulatorData_t* bmsdata)
{
	/* check for charger connection */
	if (compute.compute_charger_connected()) {
		request_transition(CHARGING_STATE);
	} else {
		sm_broadcast_current_limit(bmsdata);
		return;
	}
}

void init_charging()
{
	charge_timeout.cancelTimer();
	return;
}

void handle_charging(AccumulatorData_t* bmsdata)
{
	if (!compute.compute_charger_connected()) {
		request_transition(READY_STATE);
		return;
	} else {
		/* Check if we should charge */
		if (sm_charging_check(analyzer.bmsdata)) {
			digitalWrite(CHARGE_SAFETY_RELAY, HIGH);
			compute.compute_enable_charging(true);
		} else {
			digitalWrite(CHARGE_SAFETY_RELAY, LOW);
			compute.compute_enable_charging(false);
		}

		/* Check if we should balance */
		if (statemachine_balancing_check(analyzer.bmsdata)) {
			sm_balance_cells(analyzer.bmsdata);
		} else {
			segment.enableBalancing(false);
		}

		/* Send CAN message, but not too often */
		if (charge_message_timer.isTimerExpired()) {
			compute.compute_send_charging_message(
				(MAX_CHARGE_VOLT * NUM_CELLS_PER_CHIP * NUM_CHIPS), analyzer.bmsdata);
			charge_message_timer.startTimer(CHARGE_MESSAGE_WAIT);
		} else {
			digitalWrite(CHARGE_SAFETY_RELAY, LOW);
		}
	}
}

void init_faulted()
{
	segment.enableBalancing(false);
	compute.compute_enable_charging(false);
	entered_faulted = true;
	return;
}

void handle_faulted(AccumulatorData_t* bmsdata)
{
	if (entered_faulted) {
		entered_faulted = false;
		previousFault = sm_fault_return(bmsdata);
	}

	if (bmsdata->fault_code == FAULTS_CLEAR) {
		compute.compute_set_fault(NOT_FAULTED);
		request_transition(BOOT_STATE);
		return;
	}

	else {
		compute.compute_set_fault(FAULTED);
		digitalWrite(CHARGE_SAFETY_RELAY, LOW);
	}
	return;
}

void sm_handle_state(AccumulatorData_t* bmsdata)
{
	preFaultCheck(bmsdata);
	bmsdata->is_charger_connected = compute.compute_charger_connected();
	bmsdata->fault_code = sm_fault_return(bmsdata);

	if (bmsdata->fault_code != FAULTS_CLEAR) {
		bmsdata->discharge_limit = 0;
		request_transition(FAULTED_STATE);
	}

	// TODO needs testing
	handler_LUT[current_state](bmsdata);

	compute.compute_set_fan_speed(analyzer.calcFanPWM());
	sm_broadcast_current_limit(bmsdata);

	/* send relevant CAN msgs */
	// clang-format off
	if (can_msg_timer.isTimerExpired())
	{
		compute.compute_send_acc_status_message(analyzer.bmsdata);
		compute.compute_send_current_message(analyzer.bmsdata);
		compute.compute_send_bms_status_message(analyzer.bmsdata, current_state, segment.isBalancing());
		compute.compute_send_cell_temp_message(analyzer.bmsdata->max_temp, analyzer.bmsdata->min_temp, analyzer.bmsdata->avg_temp);
		compute.compute_send_cell_data_message(analyzer.bmsdata);
		compute.send_segment_temp_message(analyzer.bmsdata);
		can_msg_timer.startTimer(CAN_MESSAGE_WAIT);
	}
	// clang-format on
}

void request_transition(BMSState_t next_state)
{
	if (current_state == next_state)
		return;
	if (!valid_transition_from_to[current_state][next_state])
		return;

	init_LUT[next_state]();
	current_state = next_state;
}

uint32_t sm_fault_return(AccumulatorData_t* accData)
{
	/* FAULT CHECK (Check for fuckies) */

	static struct fault_eval fault_table[8] = {
		// clang-format off

          // ___________FAULT ID____________   __________TIMER___________   _____________DATA________________    __OPERATOR__   __________________________THRESHOLD____________________________  _______TIMER LENGTH_________  _____________FAULT CODE_________________    _______________DATA_____________________  ___OPERATOR___  ________THRESHOLD________
            {.id = "Discharge Current Limit", .timer =       over_curr_tmr, .data_1 =    accData->pack_current, .optype_1 = GT, .lim_1 = (accData->discharge_limit + DCDC_CURRENT_DRAW)*10*1.04, .timeout =      OVER_CURR_TIME,	.code = DISCHARGE_LIMIT_ENFORCEMENT_FAULT   /* -----------------------------------UNUSED---------------------------------*/ }, 
            {.id = "Charge Current Limit",    .timer =    over_chgcurr_tmr, .data_1 =    accData->pack_current, .optype_1 = GT, .lim_1 =                             (accData->charge_limit)*10, .timeout =  OVER_CHG_CURR_TIME, .code =    CHARGE_LIMIT_ENFORCEMENT_FAULT,  .data_2 =         accData->pack_current, .optype_2 = LT, .lim_2 =             0 },
            {.id = "Low Cell Voltage",        .timer =      under_volt_tmr, .data_1 = accData->min_voltage.val, .optype_1 = LT, .lim_1 =                                       MIN_VOLT * 10000, .timeout =     UNDER_VOLT_TIME,	.code =             CELL_VOLTAGE_TOO_LOW   /* -----------------------------------UNUSED---------------------------------*/  },
            {.id = "High Cell Voltage",       .timer = over_voltcharge_tmr, .data_1 = accData->max_voltage.val, .optype_1 = GT, .lim_1 =                                MAX_CHARGE_VOLT * 10000, .timeout =      OVER_VOLT_TIME, .code =            CELL_VOLTAGE_TOO_HIGH   /* -----------------------------------UNUSED---------------------------------*/  }, 
            {.id = "High Cell Voltage",       .timer =       overVolt_tmr, .data_1 = accData->max_voltage.val, .optype_1 = GT, .lim_1 =                                       MAX_VOLT * 10000, .timeout =      OVER_VOLT_TIME,	.code =            CELL_VOLTAGE_TOO_HIGH,   .data_2 = accData->is_charger_connected, .optype_2 = EQ, .lim_2 =         false }, 
            {.id = "High Temp",               .timer =       highTemp_tmr, .data_1 =    accData->max_temp.val, .optype_1 = GT, .lim_1 =                                          MAX_CELL_TEMP,	.timeout =       LOW_CELL_TIME,	.code =                     PACK_TOO_HOT   /* -----------------------------------UNUSED---------------------------------*/  }, 
            {.id = "Extremely Low Voltage",   .timer =        lowCell_tmr, .data_1 = accData->min_voltage.val, .optype_1 = LT, .lim_1 =                                                    900, .timeout =      HIGH_TEMP_TIME,	.code =                 LOW_CELL_VOLTAGE   /* -----------------------------------UNUSED---------------------------------*/  }, 

            NULL
		// clang-format on
	};

	uint32_t fault_status = 0;
	int incr = 0;

	while (&fault_table[incr] != NULL) {
		fault_status |= sm_fault_eval(fault_table[incr]);
		incr++;
	}

	return fault_status;
}

uint32_t sm_fault_eval(fault_eval index)
{
	bool condition1;
	bool condition2;

	index.timer.eval_length = index.timeout;

	// clang-format off
    switch (index.optype_1)
    {
        case GT: condition1 = index.data_1 > index.lim_1; break;
        case LT: condition1 = index.data_1 < index.lim_1; break;
        case GE: condition1 = index.data_1 >= index.lim_1; break;
        case LE: condition1 = index.data_1 <= index.lim_1; break;
        case EQ: condition1 = index.data_1 == index.lim_1; break;
		case NEQ: condition1 = index.data_1 != index.lim_1; break;
        case NOP: condition1 = true; break;
    }

    switch (index.optype_2)
    {
        case GT: condition2 = index.data_2 > index.lim_2; break;
        case LT: condition2 = index.data_2 < index.lim_2; break;
        case GE: condition2 = index.data_2 >= index.lim_2; break;
        case LE: condition2 = index.data_2 <= index.lim_2; break;
        case EQ: condition2 = index.data_2 == index.lim_2; break;
		case NEQ: condition2 = index.data_2 != index.lim_2; break;
        case NOP: condition2 = true; break;
    }
	// clang-format on

	if (index.timer.eval_state == BEFORE_TIMER_START && condition1 && condition2) {
		index.timer.startTimer(index.timer.eval_length);
		index.timer.eval_state = DURING_EVAL;
	}

	else if (index.timer.eval_state == DURING_EVAL) {
		if (index.timer.isTimerExpired()) {
			return index.code;
		}
		if (!(condition1 && condition2)) {
			index.timer.cancelTimer();
			index.timer.eval_state = BEFORE_TIMER_START;
		}
	}

	return 0;
}

bool sm_charging_check(AccumulatorData_t* bmsdata)
{
	if (!compute.compute_charger_connected())
		return false;
	if (!charge_timeout.isTimerExpired())
		return false;

	if (bmsdata->max_voltage.val >= (MAX_CHARGE_VOLT * 10000)
		&& charge_cut_off_time.eval_state == BEFORE_TIMER_START) {
		charge_cut_off_time.startTimer(5000);
		charge_cut_off_time.eval_state = DURING_EVAL;
	} else if (charge_cut_off_time.eval_state == DURING_EVAL) {
		if (charge_cut_off_time.isTimerExpired()) {
			charge_timeout.startTimer(CHARGE_TIMEOUT);
			return false;
		}
		if (!(bmsdata->max_voltage.val >= (MAX_CHARGE_VOLT * 10000))) {
			charge_cut_off_time.cancelTimer();
			charge_cut_off_time.eval_state = BEFORE_TIMER_START;
		}
	}

	return true;
}

bool statemachine_balancing_check(AccumulatorData_t* bmsdata)
{
	if (!compute.compute_charger_connected())
		return false;
	if (bmsdata->max_temp.val > MAX_CELL_TEMP_BAL)
		return false;
	if (bmsdata->max_voltage.val <= (BAL_MIN_V * 10000))
		return false;
	if (bmsdata->delt_voltage <= (MAX_DELTA_V * 10000))
		return false;

	return true;
}

void preFaultCheck(AccumulatorData_t* bmsdata)
{
	// prefault for Low Cell Voltage
	if (prefaultLowCell_tmr.eval_state == BEFORE_TIMER_START
		&& bmsdata->min_voltage.val < MIN_VOLT * 10000) {
		prefaultLowCell_tmr.startTimer(PRE_UNDER_VOLT_TIME);
		prefaultLowCell_tmr.eval_state = DURING_EVAL;
	} else if (prefaultLowCell_tmr.eval_state == DURING_EVAL) {
		if (prefaultLowCell_tmr.isTimerExpired()) {
			if (prefaultCANDelay1.isTimerExpired()) {
				compute.sendDclPreFault(true);
				prefaultCANDelay1.startTimer(CAN_MESSAGE_WAIT);
			}
		}
		if (!(bmsdata->min_voltage.val < MIN_VOLT * 10000)) {
			prefaultLowCell_tmr.cancelTimer();
			prefaultLowCell_tmr.eval_state = BEFORE_TIMER_START;
		}
	}

	/* prefault for DCL */
	if (prefaultOverCurr_tmr.eval_state == BEFORE_TIMER_START
		&& (bmsdata->pack_current) > ((bmsdata->discharge_limit + DCDC_CURRENT_DRAW) * 10
			   * 1.04)) // *104% to account for current sensor +/-A
	{
		prefaultOverCurr_tmr.startTimer(PRE_OVER_CURR_TIME);
		prefaultOverCurr_tmr.eval_state = DURING_EVAL;
	} else if (prefaultOverCurr_tmr.eval_state == DURING_EVAL) {
		if (prefaultOverCurr_tmr.isTimerExpired()) {
			if (prefaultCANDelay2.isTimerExpired()) {
				compute.sendDclPreFault(true);
				prefaultCANDelay2.startTimer(CAN_MESSAGE_WAIT);
			}
		}
		if (!((bmsdata->pack_current)
				> ((bmsdata->discharge_limit + DCDC_CURRENT_DRAW) * 10 * 1.04))) {
			prefaultOverCurr_tmr.cancelTimer();
			prefaultOverCurr_tmr.eval_state = BEFORE_TIMER_START;
		}
	}
}

void sm_broadcast_current_limit(AccumulatorData_t* bmsdata)
{
	// States for Boosting State Machine
	static enum { BOOST_STANDBY, BOOSTING, BOOST_RECHARGE } BoostState;

	static Timer boostTimer;
	static Timer boostRechargeTimer;

	/* Transitioning out of boost */
	if (boostTimer.isTimerExpired() && BoostState == BOOSTING) {
		BoostState = BOOST_RECHARGE;
		boostRechargeTimer.startTimer(BOOST_RECHARGE_TIME);
	}
	/* Transition out of boost recharge */
	if (boostRechargeTimer.isTimerExpired() && BoostState == BOOST_RECHARGE) {
		BoostState = BOOST_STANDBY;
	}
	/* Transition to boosting */
	if ((bmsdata->pack_current) > ((bmsdata->cont_DCL) * 10) && BoostState == BOOST_STANDBY) {
		BoostState = BOOSTING;
		boostTimer.startTimer(BOOST_TIME);
	}

	/* Currently boosting */
	if (BoostState == BOOSTING || BoostState == BOOST_STANDBY) {
		bmsdata->boost_setting
			= min(bmsdata->discharge_limit, bmsdata->cont_DCL * CONTDCL_MULTIPLIER);
	}

	/* Currently recharging boost */
	else {
		bmsdata->boost_setting = min(bmsdata->cont_DCL, bmsdata->discharge_limit);
	}
}

void sm_balance_cells(AccumulatorData_t* bms_data)
{
	bool balanceConfig[NUM_CHIPS][NUM_CELLS_PER_CHIP];

	/* For all cells of all the chips, figure out if we need to balance by comparing the difference
	 * in voltages */
	for (uint8_t chip = 0; chip < NUM_CHIPS; chip++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			uint16_t delta = bms_data->chip_data[chip].voltage_reading[cell]
				- (uint16_t)bms_data->min_voltage.val;
			if (delta > MAX_DELTA_V * 10000)
				balanceConfig[chip][cell] = true;
			else
				balanceConfig[chip][cell] = false;
		}
	}

#ifdef DEBUG_CHARGING
	printf("Cell Balancing:");
	for (uint8_t c = 0; c < NUM_CHIPS; c++) {
		for (uint8_t cell = 0; cell < NUM_CELLS_PER_CHIP; cell++) {
			printf(balanceConfig[c][cell]);
			printf("\t");
		}
		printf("\n");
	}
#endif

	segment.configureBalancing(balanceConfig);
}
