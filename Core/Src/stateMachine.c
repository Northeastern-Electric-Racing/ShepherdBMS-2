#include "stateMachine.h"
#include <stdlib.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
extern UART_HandleTypeDef huart4;

acc_data_t* prevAccData;
uint32_t bms_fault = FAULTS_CLEAR;
uint32_t bms_nc_fault = FAULTS_CLEAR;

BMSState_t current_state = BOOT_STATE;
uint32_t previousFault = 0;

nertimer_t charge_timeout = { .active = false };
nertimer_t charge_cut_off_timer = { .active = false };

nertimer_t can_msg_timer = { .active = false };

bool entered_faulted = false;

nertimer_t charger_message_timer;
static const uint16_t CHARGE_MESSAGE_WAIT = 250; /* ms */

const bool valid_transition_from_to[NUM_STATES][NUM_STATES] = {
	/*   BOOT,     READY,      CHARGING,   FAULTED	*/
	{ true, true, false, true }, /* BOOT */
	{ false, true, true, true }, /* READY */
	{ false, true, true, true }, /* CHARGING */
	{ true, false, false, true } /* FAULTED */
};

/* private function prototypes */
void init_boot(void);
void init_ready(void);
void init_charging(void);
void init_faulted(void);
void handle_boot(acc_data_t* bmsdata);
void handle_ready(acc_data_t* bmsdata);
void handle_charging(acc_data_t* bmsdata);
void handle_faulted(acc_data_t* bmsdata);
void request_transition(BMSState_t next_state);


typedef void (*HandlerFunction_t)(acc_data_t* bmsdata);
typedef void (*InitFunction_t)();

const InitFunction_t init_LUT[NUM_STATES]
	= { &init_boot, &init_ready, &init_charging, &init_faulted };

const HandlerFunction_t handler_LUT[NUM_STATES]
	= { &handle_boot, &handle_ready, &handle_charging, &handle_faulted };

void init_boot() { return; }

void handle_boot(acc_data_t* bmsdata)
{
	prevAccData = NULL;
	segment_enable_balancing(false);
	compute_enable_charging(false);
	

	// bmsdata->fault_code = FAULTS_CLEAR;

	request_transition(READY_STATE);
	return;
}

void init_ready()
{
	segment_enable_balancing(false);
	compute_enable_charging(false);
	return;
}

void handle_ready(acc_data_t* bmsdata)
{
	/* check for charger connection */
	if (compute_charger_connected()) {
		request_transition(CHARGING_STATE);
	} else {
		sm_broadcast_current_limit(bmsdata);
		return;
	}
}

void init_charging()
{
	cancel_timer(&charge_timeout);
	return;
}

void handle_charging(acc_data_t* bmsdata)
{
	if (!compute_charger_connected()) {
		request_transition(READY_STATE);
		return;
	} else {
		/* Check if we should charge */
		if (sm_charging_check(bmsdata)) {
			//TODO update to HAL 
			//digitalWrite(CHARGE_SAFETY_RELAY, 1);
			compute_enable_charging(true);
		} else {
			//TODO update to HAL
			//digitalWrite(CHARGE_SAFETY_RELAY, 0);
			compute_enable_charging(false);
		}

		/* Check if we should balance */
		if (sm_balancing_check(bmsdata)) {
			sm_balance_cells(bmsdata);
		} else {
			segment_enable_balancing(false);
		}

		/* Send CAN message, but not too often */
		if (is_timer_expired(&charger_message_timer) || !is_timer_active(&charger_message_timer)) {
			compute_send_charging_message(
				(MAX_CHARGE_VOLT * NUM_CELLS_PER_CHIP * NUM_CHIPS), bmsdata);
			start_timer(&charger_message_timer, CHARGE_MESSAGE_WAIT);
		} else {
			//TODO update to HAL
			//digitalWrite(CHARGE_SAFETY_RELAY, 0);
		}
	}
}

void init_faulted()
{
	segment_enable_balancing(false);
	compute_enable_charging(false);
	entered_faulted = true;
	return;
}

void handle_faulted(acc_data_t* bmsdata)
{
	if (entered_faulted) {
		entered_faulted = false;
		previousFault = sm_fault_return(bmsdata);
	}

	if (bmsdata->fault_code == FAULTS_CLEAR) {
		compute_set_fault(0);
		request_transition(BOOT_STATE);
		return;
	}

	else {
		compute_set_fault(1);

		//TODO update to HAL
		//digitalWrite(CHARGE_SAFETY_RELAY, 0);
	}
	return;
}

void sm_handle_state(acc_data_t* bmsdata)
{
	bmsdata->is_charger_connected = compute_charger_connected();
	bmsdata->max_temp.val = 75;
	bmsdata->fault_code = sm_fault_return(bmsdata);
	

	if (bmsdata->fault_code != FAULTS_CLEAR) {
		bmsdata->discharge_limit = 0;
		request_transition(FAULTED_STATE);
	}
	// TODO needs testing - (update, seems to work fine)
	handler_LUT[current_state](bmsdata);

	compute_set_fan_speed(analyzer_calc_fan_pwm());
	sm_broadcast_current_limit(bmsdata);
	char state_test[1] = "0";
	state_test[0] = current_state + '0';


	/* send relevant CAN msgs */
	// clang-format off
	if (is_timer_expired(&can_msg_timer) || !is_timer_active(&can_msg_timer))
	{
		compute_send_acc_status_message(bmsdata);
		compute_send_current_message(bmsdata);
		compute_send_bms_status_message(bmsdata, current_state, segment_is_balancing());
		compute_send_cell_temp_message(bmsdata);
		compute_send_cell_data_message(bmsdata);
		compute_send_segment_temp_message(bmsdata);
		start_timer(&can_msg_timer, CAN_MESSAGE_WAIT);
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

uint32_t sm_fault_return(acc_data_t* accData)
{
	/* FAULT CHECK (Check for fuckies) */

	static nertimer_t ovr_curr_timer 	= {0};
	static nertimer_t ovr_chgcurr_timer = {0};
	static nertimer_t undr_volt_timer = {0};
	static nertimer_t ovr_chgvolt_timer = {0};
	static nertimer_t ovr_volt_timer = {0};
	static nertimer_t low_cell_timer = {0};
	static nertimer_t high_temp_timer = {0};
	static fault_eval_t* fault_table = NULL;
	static acc_data_t* fault_data = NULL;

	fault_data = accData;

	if (!fault_table)
	{
		/* Note that we are only allocating this table once at runtime, so there is no need to free it */
		fault_table = (fault_eval_t*) malloc(NUM_FAULTS * sizeof(fault_eval_t));
		// clang-format off
    											// ___________FAULT ID____________   __________TIMER___________   _____________DATA________________    __OPERATOR__   __________________________THRESHOLD____________________________  _______TIMER LENGTH_________  _____________FAULT CODE_________________    	___OPERATOR 2__ _______________DATA 2______________     __THRESHOLD 2__
        fault_table[0]  = (fault_eval_t) {.id = "Discharge Current Limit", .timer =       ovr_curr_timer, .data_1 =    fault_data->pack_current, .optype_1 = GT, .lim_1 = (fault_data->discharge_limit + DCDC_CURRENT_DRAW)*10*1.04, .timeout =      OVER_CURR_TIME, .code = DISCHARGE_LIMIT_ENFORCEMENT_FAULT,  .optype_2 = NOP/* ---------------------------UNUSED------------------- */ };
        fault_table[1]  = (fault_eval_t) {.id = "Charge Current Limit",    .timer =    ovr_chgcurr_timer, .data_1 =    fault_data->pack_current, .optype_1 = GT, .lim_1 =                             (fault_data->charge_limit)*10, .timeout =  OVER_CHG_CURR_TIME, .code =    CHARGE_LIMIT_ENFORCEMENT_FAULT,  .optype_2 = LT,  .data_2 =         fault_data->pack_current,  .lim_2 =    0  };
        fault_table[2]  = (fault_eval_t) {.id = "Low Cell Voltage",        .timer =      undr_volt_timer, .data_1 = fault_data->min_voltage.val, .optype_1 = LT, .lim_1 =                                       MIN_VOLT * 10000, .timeout =     UNDER_VOLT_TIME, .code =              CELL_VOLTAGE_TOO_LOW,  .optype_2 = NOP/* ---------------------------UNUSED-------------------*/  };
        fault_table[3]  = (fault_eval_t) {.id = "High Cell Voltage",       .timer =    ovr_chgvolt_timer, .data_1 = fault_data->max_voltage.val, .optype_1 = GT, .lim_1 =                                MAX_CHARGE_VOLT * 10000, .timeout =      OVER_VOLT_TIME, .code =             CELL_VOLTAGE_TOO_HIGH,  .optype_2 = NOP/* ---------------------------UNUSED-------------------*/  };
        fault_table[4]  = (fault_eval_t) {.id = "High Cell Voltage",       .timer =       ovr_volt_timer, .data_1 = fault_data->max_voltage.val, .optype_1 = GT, .lim_1 =                                       MAX_VOLT * 10000, .timeout =      OVER_VOLT_TIME, .code =             CELL_VOLTAGE_TOO_HIGH,  .optype_2 = EQ,  .data_2 = fault_data->is_charger_connected,  .lim_2 = false };
        fault_table[5]  = (fault_eval_t) {.id = "High Temp",               .timer =      high_temp_timer, .data_1 =    fault_data->max_temp.val, .optype_1 = GT, .lim_1 =                                          MAX_CELL_TEMP, .timeout =       LOW_CELL_TIME, .code =                      PACK_TOO_HOT,  .optype_2 = NOP/* ----------------------------------------------------*/  };
        fault_table[6]  = (fault_eval_t) {.id = "Extremely Low Voltage",   .timer =       low_cell_timer, .data_1 = fault_data->min_voltage.val, .optype_1 = LT, .lim_1 =                                                    900, .timeout =      HIGH_TEMP_TIME, .code =                  LOW_CELL_VOLTAGE,  .optype_2 = NOP/* --------------------------UNUSED--------------------*/  };
		fault_table[7]  = (fault_eval_t) {.id = NULL};
		// clang-format on
	}
	static uint32_t fault_status = 0;
	int incr = 0;
	while (fault_table[incr].id != NULL) {
		fault_status |= sm_fault_eval(&fault_table[incr]);
		incr++;
	}

	return fault_status;
}

uint32_t sm_fault_eval(fault_eval_t* index)
{
	bool condition1;
	bool condition2;

	// clang-format off
    switch (index->optype_1)
    {
        case GT: condition1 = index->data_1 > index->lim_1; break;
        case LT: condition1 = index->data_1 < index->lim_1; break;
        case GE: condition1 = index->data_1 >= index->lim_1; break;
        case LE: condition1 = index->data_1 <= index->lim_1; break;
        case EQ: condition1 = index->data_1 == index->lim_1; break;
		case NEQ: condition1 = index->data_1 != index->lim_1; break;
        case NOP:
		default: condition1 = true;
    }

    switch (index->optype_2)
    {
        case GT: condition2 = index->data_2 > index->lim_2; break;
        case LT: condition2 = index->data_2 < index->lim_2; break;
        case GE: condition2 = index->data_2 >= index->lim_2; break;
        case LE: condition2 = index->data_2 <= index->lim_2; break;
        case EQ: condition2 = index->data_2 == index->lim_2; break;
		case NEQ: condition2 = index->data_2 != index->lim_2; break;
        case NOP: 
		default: condition2 = true;
    }
	// clang-format on

	if (!is_timer_active(&index->timer) && condition1 && condition2) 
	{
		start_timer(&index->timer, index->timeout);
	}

	else if (is_timer_active(&index->timer) && condition1 && condition2) {
		if (is_timer_expired(&index->timer)) {
			return index->code;
		}
		if (!(condition1 && condition2)) {
			cancel_timer(&index->timer);
		}
	}

	return 0;
}

void sm_nc_fault_collect(nc_fault_collection_t *nc_fault_data) {
	switch (nc_fault_data->fault_code) {
		case FAILED_CAN_RECEIVAL: nc_fault_data->can_fault += 1; break;
		default: break;
	}
}

bool sm_charging_check(acc_data_t* bmsdata)
{
	if (!compute_charger_connected())
		return false;
	if (!is_timer_expired(&charge_timeout) && is_timer_active(&charge_timeout))
		return false;

	if (bmsdata->max_voltage.val >= (MAX_CHARGE_VOLT * 10000)
		&& !charge_cut_off_timer.active){
		start_timer(&charge_cut_off_timer, 5000);
	} else if (charge_cut_off_timer.active) {
		if (is_timer_expired(&charge_cut_off_timer)) {
			start_timer(&charge_timeout, CHARGE_TIMEOUT);
			return false;
		}
		if (!(bmsdata->max_voltage.val >= (MAX_CHARGE_VOLT * 10000))) {
			cancel_timer(&charge_cut_off_timer);
		}
	}

	return true;
}

bool sm_balancing_check(acc_data_t* bmsdata)
{
	if (!compute_charger_connected())
		return false;
	if (bmsdata->max_temp.val > MAX_CELL_TEMP_BAL)
		return false;
	if (bmsdata->max_voltage.val <= (BAL_MIN_V * 10000))
		return false;
	if (bmsdata->delt_voltage <= (MAX_DELTA_V * 10000))
		return false;

	return true;
}

void sm_broadcast_current_limit(acc_data_t* bmsdata)
{
	// States for Boosting State Machine
	static enum { BOOST_STANDBY, BOOSTING, BOOST_RECHARGE } BoostState;

	static nertimer_t boost_timer;
	static nertimer_t boost_recharge_timer;

	/* Transitioning out of boost */
	if (is_timer_expired(&boost_timer) && BoostState == BOOSTING) {
		BoostState = BOOST_RECHARGE;
		start_timer(&boost_recharge_timer, BOOST_RECHARGE_TIME);
	}
	/* Transition out of boost recharge */
	if (is_timer_expired(&boost_recharge_timer) && BoostState == BOOST_RECHARGE) {
		BoostState = BOOST_STANDBY;
	}
	/* Transition to boosting */
	if ((bmsdata->pack_current) > ((bmsdata->cont_DCL) * 10) && BoostState == BOOST_STANDBY) {
		BoostState = BOOSTING;
		start_timer(&boost_timer, BOOST_TIME);
	}

	/* Currently boosting */
	if (BoostState == BOOSTING || BoostState == BOOST_STANDBY) {
		bmsdata->boost_setting
			= MIN(bmsdata->discharge_limit, bmsdata->cont_DCL * CONTDCL_MULTIPLIER);
	}

	/* Currently recharging boost */
	else {
		bmsdata->boost_setting = MIN(bmsdata->cont_DCL, bmsdata->discharge_limit);
	}
}

void sm_balance_cells(acc_data_t* bms_data)
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

	segment_configure_balancing(balanceConfig);
}
