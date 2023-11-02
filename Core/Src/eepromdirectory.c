#include "eepromdirectory.h"
#include "general/include/m24c32.h"

void eepromInit()
{   
    // Initialize EEPROM addresses given data and length

    int i = 1;
    int offset = 0;

    /* initialize root offset to zero */
    eeprom_data[0].address = EEPROM_ROOT_ADDR;

    /* continue through array, setting offsets */
    while (eeprom_data[i].id != NULL)
    {
        offset += eeprom_data[i-1].size;
        eeprom_data[i].address = offset;
        i++;
    }
    //Initialize first byte of faults partition to contain the address of the end of the partition so that the 
    //logFault function logs the first fault to the first 4 bytes in the partition.
    eepromWriteData(const_cast<char*>("FAULTS"), eeprom_data[ eepromGetAddress(const_cast<char*>("FAULTS"))]
    + eeprom_data[ eepromGetIndex(const_cast<char*>("FAULTS"))].size - 3, 1);
}

uint16_t eepromGetAddress(char *key)
{
    /* find the index of the key in the eeprom_data array */
    int i = 0;
    while (eeprom_data[i].id != NULL)
    {
        if (eeprom_data[i].id == key)
        {
            return eeprom_data[i].address;
        }

        i++;
    }
    return -1;
}

uint16_t eepromGetIndex(char *key)
{
    /* find the index of the key in the eeprom_data array */
    int i = 0;
    while (eeprom_data[i].id != NULL)
    {
        if (eeprom_data[i].id == key)
        {
            return i;
        }

        i++;
    }
    return -1;
}

char* eepromGetKey(int index)
{
    /* find the key at the index in the eeprom_data array */
    int i = 0;
    while (eeprom_data[i].id != NULL)
    {
        if (eeprom_data[i].address == index)
        {
            return eeprom_data[i].id;
        }

        i++;
    }
    return NULL;
}

bool eepromReadData(char *key, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* read data from eeprom given key and size */
    int address = eepromGetAddress(key);
    eeprom_read(address, data, size);

}

bool eepromReadData(uint16_t address, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* read data from eeprom given index */
    eeprom_read(address, data, size);
    //EEPROM.get(index, data); // TODO will need update with new eeprom driver
}

bool eepromWriteData(char *key, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* write data to eeprom given key, offset, and size of data */
    int address = eepromGetAddress(key);
    eeprom_write(address, data, size);
}

bool eepromWriteData(uint16_t address, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* write data to eeprom given page, offset, and size of data */
    eeprom_write(address, data, size);
}

void logFault(uint32_t fault_code)
{
    uint32_t fault = fault_code;
    //The next address to write a fault to is located in the first byte of the FAULTS partition.
    uint8_t *address;
    eepromReadData(eepromGetAddress(const_cast<char*>("FAULTS")), address, 1);

    uint8_t startIndex =  eeprom_data[eepromGetIndex(const_cast<char*>("FAULTS"))].address;
    uint8_t size = eeprom_data[eepromGetIndex(const_cast<char*>("FAULTS"))].size;

    /* if the index is at the end of the partition, wrap around (currently store 5 faults, so max = 5 + offset) */
    if (*address == size + startIndex - 3)
    {
        /* first byte of partition is the index of the most recent fault, faults begin at second byte */
        *address = startIndex + 1;
    }
    else
    {
        *address += 4;
    }

    /* write the fault code*/
    eepromWriteData(*address, fault, 4);
    /* update first byte of faults partition*/
    eepromWriteData(startIndex, *address + 4, 1);
} 

void getFaults()
{
    uint8_t* address;
    eepromReadData("FAULTS", address, 1);

    uint8_t startAddress =  eeprom_data[eepromGetIndex(const_cast<char*>("FAULTS"))].address;
    uint8_t size = eeprom_data[eepromGetIndex(const_cast<char*>("FAULTS"))].size;

    /* read and store the faults */

    int currIter = 0;
    while (currIter < NUM_EEPROM_FAULTS)
    {
        eepromReadData(*index, &eeprom_faults[currIter], 4);
        currIter++;

        /* if the index is at the end of the partition, wrap around (5 faults * 4 bytes per fault + offset - 3  for start of fault) */
        if (*address == size + startAddress - 3)
        {                             
            /* first byte of partition is the index of the most recent fault, faults begin at second byte */
            *address = startAddress + 1;
        }
        
        else
        {
            /* 4 bytes per fault */
            *address += 4;
        }
    }
}