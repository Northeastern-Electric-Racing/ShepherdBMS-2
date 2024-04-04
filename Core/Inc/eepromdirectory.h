#ifndef EEPROMDIRECTORY_H
#define EEPROMDIRECTORY_H

#include "bmsConfig.h"
#include <stdint.h>
#include <stdbool.h>

#define NUM_EEPROM_FAULTS 5
#define NUM_EEPROM_ITEMS  2
#define EEPROM_ROOT_ADDR  0

/* index 0 = newest, index 4 = oldest */
static uint32_t eeprom_faults[NUM_EEPROM_FAULTS];

struct eeprom_partition 
{
    char *id;           /*  key  */
    uint16_t size;      /* bytes */
    uint16_t address;     /* start address */
};

struct eeprom_partition eeprom_data[NUM_EEPROM_ITEMS];
/*  ____________KEY________________         _BYTES_   */
  



/**
 * @brief partitions eeprom addresses given table of data and size
 * 
 * 
 */
void eepromInit();

/**
 * @brief returns the starting address of the passed key
 * 
 * @param key
 * @return int 
 */
uint16_t eeprom_get_index(char *key);

/**
 * @brief returns the key at the passed index
 *  
 * 
 */
char *eeprom_get_key(int index);



/**
 * @brief fills passed data pointer with data from eeprom
 * 
 * @note user is responsible for passing data of correct size
 * @param key
 * @param data
 */
bool eeprom_read_data_key(char *key, void *data, uint16_t size);

bool eeprom_read_data_address(uint16_t address, void *data, uint16_t size);

/**
 * @brief loads eeprom with data from passed pointer
 * 
 * @note user is responsible for passing data of correct size
 * @param key 
 * @param data 
 */
bool eeprom_write_data_key(char *key, void *data, uint16_t size);

bool eeprom_write_data_address(uint16_t address, void *data, uint16_t size);

/**
 * @brief logs fault code in eeprom
 * 
 * 
 * @param fault_code 
 */
void log_fault(uint32_t fault_code);
/**
 * @brief reads all stored faults from eeprom
 *
 * 
 * @note this updates a static array of fault codes, should be called before accessing the array
 * @note this function is blocking, and will take a few ms to complete. This is why it is kept seperate from log_fault(), 
 *      allwing the user more control as to when to use this
 */

void get_faults();

bool test_EEPROM();

#endif