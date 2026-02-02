/** \file max30102.cpp ******************************************************
*
* Project: MAXREFDES117#
* Filename: max30102.cpp
* Description: This module is an embedded controller driver for the MAX30102
*
* Revision History:
*\n 1-18-2016 Rev 01.00 GL Initial release.
*\n 12-22-2017 Rev 02.00 Significantlly modified by Robert Fraczkiewicz
*\n to use Wire library instead of MAXIM's SoftI2C
*
* --------------------------------------------------------------------
*
* This code follows the following naming conventions:
*
* char              ch_pmod_value
* char (array)      s_pmod_s_string[16]
* float             f_pmod_value
* int32_t           n_pmod_value
* int32_t (array)   an_pmod_value[16]
* int16_t           w_pmod_value
* int16_t (array)   aw_pmod_value[16]
* uint16_t          uw_pmod_value
* uint16_t (array)  auw_pmod_value[16]
* uint8_t           uch_pmod_value
* uint8_t (array)   auch_pmod_buffer[16]
* uint32_t          un_pmod_value
* int32_t *         pn_pmod_value
*
* ------------------------------------------------------------------------- */
/*******************************************************************************
* Copyright (C) 2016 Maxim Integrated Products, Inc., All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL MAXIM INTEGRATED BE LIABLE FOR ANY CLAIM, DAMAGES
* OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
* Except as contained in this notice, the name of Maxim Integrated
* Products, Inc. shall not be used except as stated in the Maxim Integrated
* Products, Inc. Branding Policy.
*
* The mere transfer of this software does not imply any licenses
* of trade secrets, proprietary technology, copyrights, patents,
* trademarks, maskwork rights, or any other form of intellectual
* property whatsoever. Maxim Integrated Products, Inc. retains all
* ownership rights.
*******************************************************************************
*/
#include "max30102.h"
#include "algorithm_by_RF.h"

esp_err_t maxim_max30102_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(
        I2C_NUM_0,
        I2C_WRITE_ADDR,
        buf,
        sizeof(buf),
        pdMS_TO_TICKS(100)
    );
}
esp_err_t maxim_max30102_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t ret;

    ret = i2c_master_write_read_device(
        I2C_NUM_0,
        I2C_WRITE_ADDR,
        &reg,
        1,
        data,
        1,
        pdMS_TO_TICKS(100)
    );

    return ret;
}
bool maxim_max30102_init(void)
{
    uint8_t dummy;

    maxim_max30102_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));

    maxim_max30102_read_reg(REG_INTR_STATUS_1, &dummy);

    if (maxim_max30102_write_reg(REG_INTR_ENABLE_1, 0xC0) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_INTR_ENABLE_2, 0x00) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_FIFO_WR_PTR, 0x00) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_OVF_COUNTER, 0x00) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_FIFO_RD_PTR, 0x00) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_FIFO_CONFIG, 0x4F) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_MODE_CONFIG, 0x03) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_SPO2_CONFIG, 0x27) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_LED1_PA, 0x24) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_LED2_PA, 0x24) != ESP_OK) return false;
    if (maxim_max30102_write_reg(REG_PILOT_PA, 0x7F) != ESP_OK) return false;

    return true;
}

//#if defined(ARDUINO_AVR_UNO)
//Arduino Uno doesn't have enough SRAM to store 100 samples of IR led data and red led data in 32-bit format
//To solve this problem, 16-bit MSB of the sampled data will be truncated.  Samples become 16-bit data.
//bool maxim_max30102_read_fifo(uint16_t *pun_red_led, uint16_t *pun_ir_led)
//#else
esp_err_t maxim_max30102_read_fifo(uint32_t *red, uint32_t *ir)
{
    uint8_t reg = REG_FIFO_DATA;
    uint8_t data[6];

    esp_err_t ret = i2c_master_write_read_device(
        I2C_NUM_0,
        I2C_WRITE_ADDR,
        &reg,
        1,
        data,
        6,
        pdMS_TO_TICKS(100)
    );
    if (ret != ESP_OK) return ret;

    *red = ((uint32_t)data[0] << 16) |
           ((uint32_t)data[1] << 8)  |
            (uint32_t)data[2];
    *ir  = ((uint32_t)data[3] << 16) |
           ((uint32_t)data[4] << 8)  |
            (uint32_t)data[5];

    *red &= 0x03FFFF;
    *ir  &= 0x03FFFF;

    return ESP_OK;
}

bool maxim_max30102_reset()
/**
* \brief        Reset the MAX30102
* \par          Details
*               This function resets the MAX30102
*
* \param        None
*
* \retval       true on success
*/
{
    if(!maxim_max30102_write_reg(REG_MODE_CONFIG,0x40))
        return false;
    else
        return true;    
}

bool maxim_max30102_read_temperature(int8_t *integer_part, uint8_t *fractional_part)
{
    maxim_max30102_write_reg(REG_TEMP_CONFIG, 0x01);
    esp_rom_delay_us(30);


    uint8_t temp;
    maxim_max30102_read_reg(REG_TEMP_INTR, &temp);
    *integer_part = (int8_t)temp;

    maxim_max30102_read_reg(REG_TEMP_FRAC, fractional_part);
    return true;
}
