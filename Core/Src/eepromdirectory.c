#include "eepromdirectory.h"
#include "m24c32.h"

void eepromInit()
{   
    eeprom_data[0].id =  (char*)("ROOT");     
    eeprom_data[0].size = 1;

    eeprom_data[1].id = (char*)("FAULTS");
    eeprom_data[1].size = 21;                      

    // Initialize EEPROM addresses given data and length

    int i = 1;
    int offset = 0;

    /* initialize root offset to zero */
    eeprom_data[0].address = EEPROM_ROOT_ADDR;

    /* continue through array, setting offsets *//* private funciton prototypes */
    while (eeprom_data[i].id != NULL)
    {
        offset += eeprom_data[i-1].size;
        eeprom_data[i].address = offset;
        i++;
    }

    // TODO ADD THIS BACK AND FIGURE OUT WHAT THE FUCK ITS TRYING TO DO
    //Initialize first byte of faults partition to contain the address of the end of the partition so that the 
    //log_fault function logs the first fault to the first 4 bytes in the partition.
    // eeprom_write_data_key((char*)("FAULTS"), (eeprom_data[eeprom_get_index((char*)("FAULTS"))]
    // + eeprom_data[eeprom_get_index((char*)("FAULTS"))].size - 3), 1);
}

uint16_t eeprom_get_index(char *key)
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

char* eeprom_get_key(int index)
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

bool eeprom_read_data_key(char *key, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* read data from eeprom given key and size */
    int address = eeprom_get_index(key);
    eeprom_read(address, data, size);

    return true;

}

bool eeprom_read_data_address(uint16_t address, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* read data from eeprom given index */
    eeprom_read(address, data, size);
    return true;
}

bool eeprom_write_data_key(char *key, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* write data to eeprom given key, offset, and size of data */
    int address = eeprom_get_index(key);
    eeprom_write(address, data, size);
    return true;
}

bool eeprom_write_data_address(uint16_t address, void *data, uint16_t size)
{
    if(!data) {
        return false;
    }
    /* write data to eeprom given page, offset, and size of data */
    eeprom_write(address, data, size);
    return true;
}

void log_fault(uint32_t fault_code)
{
    uint32_t fault = fault_code;
    //The next address to write a fault to is located in the first byte of the FAULTS partition.
    uint8_t reg_to_write;
    eeprom_read_data_address(eeprom_get_index((char*)("FAULTS")), &reg_to_write, 1);

    uint8_t startIndex =  eeprom_data[eeprom_get_index((char*)("FAULTS"))].address;
    uint8_t size = eeprom_data[eeprom_get_index((char*)("FAULTS"))].size;

    /* if the index is at the end of the partition, wrap around (currently store 5 faults, so max = 5 + offset) */
    if (reg_to_write == size + startIndex - 3)
    {
        /* first byte of partition is the index of the most recent fault, faults begin at second byte */
        reg_to_write = startIndex + 1;
    }
    else
    {
        reg_to_write += 4;
    }

    /* write the fault code*/
    eeprom_write_data_address(reg_to_write, &fault, 4);
    /* update first byte of faults partition*/
    eeprom_write_data_address(startIndex, &reg_to_write, 1);
} 

void get_faults()
{
    uint8_t curr_reg;
    eeprom_read_data_key("FAULTS", &curr_reg, 1);

    uint8_t startAddress =  eeprom_data[eeprom_get_index((char*)("FAULTS"))].address;
    uint8_t size = eeprom_data[eeprom_get_index((char*)("FAULTS"))].size;

    /* read and store the faults */

    int currIter = 0;
    while (currIter < NUM_EEPROM_FAULTS)
    {
        eeprom_read_data_address(curr_reg, &eeprom_faults[currIter], 4);
        currIter++;

        /* if the index is at the end of the partition, wrap around (5 faults * 4 bytes per fault + offset - 3  for start of fault) */
        if (curr_reg == size + startAddress - 3)
        {                             
            /* first byte of partition is the index of the most recent fault, faults begin at second byte */
            curr_reg= startAddress + 1;
        }
        
        else
        {
            /* 4 bytes per fault */
            curr_reg += 4;
        }
    }
}

//TODO: Consolidate these into a single function after testing
bool test_EEPROM(){
    test_main_EEPROM();
    test_fault_EEPROM();
}

//TODO: Verify that this code works as intended in hardware
bool test_main_EEPROM(){
    uint8_t reg_to_write;
    uint8_t root_address = eeprom_get_index((char*)("ROOT"));
    uint8_t initial_data = eeprom_read_data_address(root_address, &reg_to_write, 1);

    // Write a known data value to a certain address in EEPROM and read it back
    uint8_t known_data = 22;
    uint8_t data_read;
    eeprom_write_data_address(root_address, &reg_to_write, 1);
    eeprom_read_data_address(root_address, &data_read, 1);

    if (data_read == known_data){
        printf("Data was successfully written and read from EEPROM");
    }
    else{
        printf("Data was not successfully written/read from EEPROM");
    }

    // Write data previously stored in EEPROM address back to EEPROM
    eeprom_write_data_address(root_address, &reg_to_write, 1);
}

//TODO: Verify that this code works as intended in hardware
bool test_fault_EEPROM(){
    uint8_t start_Address = eeprom_data[eeprom_get_index((char*)("FAULTS"))].address;
    uint8_t curr_Address = start_Address;
    uint8_t size = eeprom_data[eeprom_get_index((char*)("FAULTS"))].size;

    uint32_t read_Faults[NUM_EEPROM_FAULTS];

    // Loop to read the state of the entire fault log before doing any test reads/writes to it
    int curr_Iter = 0;
    while (curr_Iter < NUM_EEPROM_FAULTS){
        eeprom_read_data_address(curr_Address, &read_Faults[curr_Iter], 4);
        curr_Iter++;

        /* If the index is at the end of the partition, wrap around (5 faults * 4 bytes per fault + offset -3 for start of fault) */
        if (curr_Address == size + start_Address - 3){
            // First byte of partition is index of most recent fault, faults begin at the second byte
            curr_Address = start_Address + 1;
        }
        else{
            curr_Address += 4;
        }
    }

    // Write known data value to EEPROM fault register
    uint32_t sample_fault = 0xBEEF;
    uint32_t temp_Faults[NUM_EEPROM_FAULTS];
    uint32_t test_fault = 0xABBA;
    uint8_t index_reg;

    curr_Address = start_Address;
    curr_Iter = 0;
    while (curr_Iter < NUM_EEPROM_FAULTS){
        log_fault(sample_fault);
        eeprom_read_data_address(curr_Address, &temp_Faults[curr_Iter], 4);
        curr_Iter++;

        if (temp_Faults[curr_Iter] == sample_fault){
            printf("Data successfully written and read from EEPROM fault register");
        }
        else{
            printf("Data was not successfully written and read from EEPROM fault register");
        }

        if (curr_Address == size + start_Address - 3){
            curr_Address = start_Address + 1;
        }
        else{
            curr_Address += 4;
        }
    }

    // Write one more fault, which overwrites the first sample fault we put in the register
    eeprom_read_data_key("FAULTS", &index_reg, 1);
    log_fault(test_fault);
    eeprom_read_data_address(curr_Address, &temp_Faults[index_reg], 4);

    if (temp_Faults[index_reg] == test_fault){
        printf("Old fault successfully overwritten");
    }
    else{
        printf("Old fault was not overwritten");
    }

    // Re-write old faults back into register
    curr_Iter = 0;
    while (curr_Iter < NUM_EEPROM_FAULTS){
        log_fault(read_Faults[curr_Iter]);
        curr_Iter++;
    }
}