#include "main.h"
#include "config.h"

uint16_t rawCellVoltages[CHIPS][12];
float cellVoltages[CHIPS][12];
uint16_t rawTempVoltages[CHIPS][6];
int temps[CHIPS][32];
uint8_t chipConfigurations[CHIPS][6];
uint16_t cellTestIter = 0;
char keyPress = '0';
bool discharge = true;
bool balance = true;
char serialBuf[40];
uint8_t commRegData[CHIPS][6];
uint8_t i2cWriteData[CHIPS][3];
bool balancing[CHIPS][CELLS_S];
uint16_t dischargeCommand[CHIPS];

float minCellVal = 100;
int minCell = 0;
float maxCellVal = 0;
int maxCell = 0;
float deltaV = 0;
float packV = 0;

uint64_t currTime = 0;
uint64_t lastPrintTime = 0;
uint64_t lastVoltTime = 0;
uint64_t lastTempTime = 0;

const uint32_t VOLT_TEMP_CONV[106] = {
157300, 148800, 140300, 131800, 123300, 114800, 108772, 102744, 96716, 90688, 84660, 80328, 75996, 71664, 67332,
63000, 59860, 56720, 53580, 50440, 47300, 45004, 42708, 40412, 38116, 35820, 34124, 32428, 30732, 29036, 27340,
26076, 24812, 23548, 22284, 21020, 20074, 19128, 18182, 17236, 16290, 15576, 14862, 14148, 13434, 12720, 12176,
11632, 11088, 10544, 10000, 9584, 9168, 8753, 8337, 7921, 7600, 7279, 6957, 6636, 6315, 6065, 5816, 5566, 5317,
5067, 4872, 4676, 4481, 4285, 4090, 3936, 3782, 3627, 3473, 3319, 3197, 3075, 2953, 2831, 2709, 2612, 2514, 2417,
2319, 2222, 2144, 2066, 1988, 1910, 1832, 1769, 1706, 1644, 1581, 1518, 1467, 1416, 1366, 1315, 1264, 1223, 1181, 1140, 1098, 1057};


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(3000); // Allow time to connect and see boot up info
  Serial.println("Hello World!");
  LTC6804_initialize();

  int8_t i2c_write_data[CHIPS][3];
    // Set GPIO expander to output
  	for(int chip = 0; chip < NUM_CHIPS; chip++) {
    i2c_write_data[chip][0] = 0x40; // GPIO expander addr
    i2c_write_data[chip][1] = 0x00; // GPIO direction addr
    i2c_write_data[chip][2] = 0x00; // Set all to output
  }
  uint8_t comm_reg_data[CHIPS][6];

  serialize_i2c_msg(i2c_write_data, comm_reg_data);
  LTC6804_wrcomm(CHIPS, comm_reg_data);
  LTC6804_stcomm(24);
}

  // Turn OFF GPIO 1 & 2 pull downs
  GetChipConfigurations(chipConfigurations);
  for (int c = 0; c < CHIPS; c++)
  {
    chipConfigurations[c][0] |= 0x18;
    ConfigureDischarge(c, 0);
  }
  SetChipConfigurations(chipConfigurations);

  Serial.print("Chip CFG:\n");
  for (int c = 0; c < CHIPS; c++)
  {
    for (int byte = 0; byte < 6; byte++)
    {
      Serial.print(chipConfigurations[c][byte], HEX);
      Serial.print("\t");
    }
    Serial.println();
  }
  Serial.println();
}

void loop() {
  currTime = millis();
  // Keypress processing
  if (Serial.available()) { // Check for key presses
    keyPress = Serial.read(); // Read key
    if (keyPress == ' ') {
      discharge = !discharge; // Toggle discharging
      Serial.println("TOGGLE DISCHARGE");
    } else if (keyPress == 'b') {
      balance = !balance;
      Serial.println("TOGGLE DISCHARGE MODE");
    }
  }

  // MEASURE VOLTAGES
  if (lastVoltTime + VOLT_DELAY < currTime) {
    // Run ADC on cell taps
    LTC6804_adcv(); //this needs to be done before pulling from registers

    // Pull and print the cell voltages from registers
    LTC6804_rdcv(0, CHIPS, rawCellVoltages);
  }

  // MEASURE TEMPS
  if (lastTempTime + TEMP_DELAY < currTime) {
    // Ensuring GPIO 1 & 2 pull downs are OFF
    GetChipConfigurations(chipConfigurations);
    for (int c = 0; c < CHIPS; c++)
    {
      chipConfigurations[c][0] |= 0x18;
    }
    SetChipConfigurations(chipConfigurations);

    updateAllTherms(1, temps);
  }

  // PRINT VOLTAGES AND TEMPS
  if (lastPrintTime + PRINT_DELAY < currTime) {
    lastPrintTime = currTime;

    Serial.print("Voltage:\n");
    minCellVal = 100;
    maxCellVal = 0;
    packV = 0;
    for (int c = 0; c < CHIPS; c++)
    {
      for (int cell = 0; cell < CELLS_S; cell++)
      {
        cellVoltages[c][cell] = float(rawCellVoltages[c][cell]) / 10000;
        packV += cellVoltages[c][cell];
        if (cellVoltages[c][cell] < minCellVal) {
          minCellVal = cellVoltages[c][cell];
          minCell = c * CELLS_S + cell;
        }
        if (cellVoltages[c][cell] > maxCellVal) {
          maxCellVal = cellVoltages[c][cell];
          maxCell = c * CELLS_S + cell;
        }
        deltaV = maxCellVal - minCellVal;
        dtostrf(cellVoltages[c][cell], 6, 4, serialBuf);
        sprintf(serialBuf, "%s\t", serialBuf);
        if(balancing[c][cell] && discharge) {
          Serial.print("\033[31m");
          Serial.print(serialBuf);
          Serial.print("\033[37m");
        } else {
          Serial.print(serialBuf);
        }
        // This would work, but arduino stupidly does not support floats in formatting :/
        // Keeping here for when we move to teensy (which I think can do this )
        // sprintf(serialBuf, "%1.4fV\t", cellVoltages[c][cell]);
        // Serial.print(serialBuf);
      }
      // Serial.println();
    }
    Serial.println("\n");

    Serial.print("Pack Voltage: ");
    Serial.println(packV);

    Serial.print("Max Voltage: ");
    Serial.print(maxCellVal);
    Serial.print(", ");
    Serial.println(maxCell + 1);

    Serial.print("Min Voltage: ");
    Serial.print(minCellVal);
    Serial.print(", ");
    Serial.println(minCell + 1);

    Serial.print("Cell Delta: ");
    Serial.println(deltaV);
    Serial.println();
    
    Serial.print("Temperature:\n");
    for (int c = 0; c < 2; c++)
    {
      for (int i = 0; i < 31; i++)
      {
        if (i == 15) {
          Serial.println();
        }
        Serial.print(temps[c][i]);
        Serial.print("\t");
      }
      // Serial.println();
    }
    // for (int c = 1; c < 2; c++)
    // {
    //   for (int i = 0; i < THERMISTORS; i++)
    //   {
    //     Serial.print(temps[c][i]);
    //     Serial.print("\t");
    //   }
    //   Serial.println();
    
    Serial.println();
  }

  // DISCHARGE
  if (discharge) {
    if (balance) {
      delay(1000);
      // First attempt balancing algorithm
      for (int c = 0; c < CHIPS; c++) {
        dischargeCommand[c] = 0;
      }
      for (int c = 0; c < CHIPS; c++) {
        for (int cell = 0; cell < CELLS_S; cell ++) {
          balancing[c][cell] = (cellVoltages[c][cell] > minCellVal + MAX_DELTA_V) && cellVoltages[c][cell] > BAL_MIN_V;
          if (balancing[c][cell]) {
            dischargeCommand[c] |= 1 << cell;
          }
        }
      }
    } else {
      delay(200);
       // Just a counter for testing
      cellTestIter++;
      if (cellTestIter >= 512) {
        cellTestIter = 0;
      }
      for (int c = 0; c < CHIPS; c++) {
        dischargeCommand[c] = cellTestIter;
      }
    }

    GetChipConfigurations(chipConfigurations);
    for (int c = 0; c < CHIPS; c++) {
      ConfigureDischarge(c, dischargeCommand[c]);
    }
    SetChipConfigurations(chipConfigurations);
  } else {
    GetChipConfigurations(chipConfigurations);
    for (int c = 0; c < CHIPS; c++) {
      ConfigureDischarge(c, 0);
    }
    SetChipConfigurations(chipConfigurations);
  }
}



//last two bytes of recieved index are PEC and we want to dump them
void GetChipConfigurations(uint8_t localConfig[][6]) 
{ 
  uint8_t remoteConfig[CHIPS][8];
  LTC6804_rdcfg(CHIPS, remoteConfig);
  for (int chip = 0; chip < CHIPS; chip++)
  {
    for(int index = 0; index < 6; index++)
    {
      localConfig[chip][index] = remoteConfig[chip][index];
    }

  }

}

void SetChipConfigurations(uint8_t localConfig[][6]) 
{
  LTC6804_wrcfg(CHIPS, localConfig);
}

void ConfigureDischarge(uint8_t chip, uint16_t cells) 
{
  chipConfigurations[chip][4] = uint8_t(cells & 0x00FF);
  chipConfigurations[chip][5] = (chipConfigurations[chip][5] & 0xF0) + uint8_t(cells >> 8);
}


/**
 * @brief This takes the desired I2C command and serializes it to the 6 COMM registers of the LTC6804, might need to double check calculations in the future
 * 
 * @param CHIPS 
 * @param dataToWrite 
 * @param commOutput 
 */
void ConfigureCOMMRegisters(uint8_t numChips, uint8_t dataToWrite[][3], uint8_t commOutput [][6])
{
  for (int chip = 0; chip < numChips; chip++)
  {
    commOutput[chip][0] = 0x60 | (dataToWrite[chip][0] >> 4); // START + high side of B0
    commOutput[chip][1] = (dataToWrite[chip][0] << 4) | 0x00; // low side of B0 + ACK
    commOutput[chip][2] = 0x00 | (dataToWrite[chip][1] >> 4); // BLANK + high side of B1
    commOutput[chip][3] = (dataToWrite[chip][1] << 4) | 0x00; // low side of B1 + ACK
    commOutput[chip][4] = 0x00 | (dataToWrite[chip][2] >> 4); // BLANK + high side of B2
    commOutput[chip][5] = (dataToWrite[chip][2] << 4) | 0x09; // low side of B2 + STOP & NACK
  }
}

/**
 * @brief Configures multiplexors to expose the desired therrmistor for measurment
 * 
 * @param therm Number thermistor requested
 */
void SelectTherm(uint8_t therm) {
  // Exit if out of range values
  if (therm < 1 || therm > 16) {
    return;
  }
    // select 0-16 on GPIO expander
    for(int chip = 0; chip < CHIPS; chip++) {
      i2cWriteData[chip][0] = 0x40; // GPIO expander addr
      i2cWriteData[chip][1] = 0x09; // GPIO state addr
      i2cWriteData[chip][2] = therm; // 0-15, will change multiplexor to select thermistor
    }
    ConfigureCOMMRegisters(CHIPS, i2cWriteData, commRegData);
    LTC6804_wrcomm(CHIPS, commRegData);
    LTC6804_stcomm(24);
  
}

void updateAllTherms(uint8_t numChips, int out[][32]) {
  for (int therm = 1; therm <= 16; therm++) {
    SelectTherm(therm);
    delay(5);
    //SelectTherm(therm + 16); not needed, setting GPIO exapnder will read both
    delay(10);
    LTC6804_adax(); // Run ADC for AUX (GPIOs and refs)
    delay(10);
    LTC6804_rdaux(0, numChips, rawTempVoltages); // Fetch ADC results from AUX registers
    for (int c = 0; c < numChips; c++) {

      uint16_t steinhart_input_low = 10000 * (float)( (rawTempVoltages[c][2])/ (rawTempVoltages[c][0]) - 1 );
			uint16_t steinhart_input_high = 10000 * (float)( (rawTempVoltages[c][2])/ (rawTempVoltages[c][1]) - 1 );
      out[c][therm-1] = steinhart_est(steinhart_input_high);
      out[c][therm + 15] = steinhart_est(steinhart_input_high);
    }
  }
}

int8_t steinhart_est(uint16_t V)
{
	/* min temp - max temp with buffer on both */
	for (int i = -25; i < 80; i++) {
		if (V > VOLT_TEMP_CONV[i + 25]) {
			return i;
		}
	}

	return 80;
	
}

int8_t steinhartEq(int8_t R) {
  return int8_t(1 / (0.001125308852122 + (0.000234711863267 * log(R)) + (0.000000085663516 * (pow(log(R), 3)))));
}

/* Cause im lazy: https://www.lasercalculator.com/ntc-thermistor-calculator/
Currently only plots to 0-50. All values outside are binned to 0 or 50
*/
uint8_t steinhartEst(uint32_t R) {
  int i = 0;
  while (R < TEMP_CONV[i]) i++;
  return i;
}

int voltToTemp(uint32_t V) {
  int i = 0;
  while (V < VOLT_TEMP_CONV[i]) i++;
  return i - 5;
}

// should be default, so may not need to call this
int gpioExanderInit(){
  // Set GPIO expander to output
  for(int chip = 0; chip < CHIPS; chip++) {
    i2cWriteData[chip][0] = 0x20; // GPIO expander addr
    i2cWriteData[chip][1] = 0x00; // GPIO direction addr
    i2cWriteData[chip][2] = 0x00; // Set all to output
  }
  ConfigureCOMMRegisters(CHIPS, i2cWriteData, commRegData);
  LTC6804_wrcomm(CHIPS, commRegData);
}
