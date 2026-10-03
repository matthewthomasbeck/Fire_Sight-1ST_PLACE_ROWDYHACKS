/**
 * @copyright (C) 2017 Melexis N.V.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Linux I2C backend (Pimoroni port).
 * Raspberry Pi physical pins 3 (SDA) + 5 (SCL) == /dev/i2c-1.
 */
#include "MLX90640_I2C_Driver.h"

#if defined(__linux__)

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef I2C_FUNC_I2C
#include <linux/i2c.h>
#define I2C_MSG_FMT __u8
#endif

static int i2c_fd = -1;
static const char* i2c_device = "/dev/i2c-1";

static int ensure_i2c_open(void) {
    if (i2c_fd >= 0) {
        return 0;
    }

    i2c_fd = open(i2c_device, O_RDWR);
    if (i2c_fd < 0) {
        return -1;
    }
    return 0;
}

void MLX90640_I2CInit(void) {
    (void)ensure_i2c_open();
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress,
                     uint16_t nMemAddressRead, uint16_t* data) {
    if (ensure_i2c_open() < 0) {
        return -1;
    }

    char cmd[2] = {(char)(startAddress >> 8), (char)(startAddress & 0xFF)};
    char buf[1664];
    uint16_t* p = data;
    struct i2c_msg i2c_messages[2];
    struct i2c_rdwr_ioctl_data i2c_messageset[1];

    i2c_messages[0].addr = slaveAddr;
    i2c_messages[0].flags = 0;
    i2c_messages[0].len = 2;
    i2c_messages[0].buf = (I2C_MSG_FMT*)cmd;

    i2c_messages[1].addr = slaveAddr;
    i2c_messages[1].flags = I2C_M_RD | I2C_M_NOSTART;
    i2c_messages[1].len = nMemAddressRead * 2;
    i2c_messages[1].buf = (I2C_MSG_FMT*)buf;

    i2c_messageset[0].msgs = i2c_messages;
    i2c_messageset[0].nmsgs = 2;

    memset(buf, 0, nMemAddressRead * 2);

    if (ioctl(i2c_fd, I2C_RDWR, &i2c_messageset) < 0) {
        return -1;
    }

    for (int count = 0; count < nMemAddressRead; count++) {
        int i = count << 1;
        *p++ = ((uint16_t)buf[i] << 8) | buf[i + 1];
    }

    return 0;
}

void MLX90640_I2CFreqSet(int freq) {
    (void)freq;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    if (ensure_i2c_open() < 0) {
        return -1;
    }

    char cmd[4] = {
        (char)(writeAddress >> 8),
        (char)(writeAddress & 0x00FF),
        (char)(data >> 8),
        (char)(data & 0x00FF)};

    struct i2c_msg i2c_messages[1];
    struct i2c_rdwr_ioctl_data i2c_messageset[1];

    i2c_messages[0].addr = slaveAddr;
    i2c_messages[0].flags = 0;
    i2c_messages[0].len = 4;
    i2c_messages[0].buf = (I2C_MSG_FMT*)cmd;

    i2c_messageset[0].msgs = i2c_messages;
    i2c_messageset[0].nmsgs = 1;

    if (ioctl(i2c_fd, I2C_RDWR, &i2c_messageset) < 0) {
        return -1;
    }

    return 0;
}

#else

void MLX90640_I2CInit(void) {}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress,
                     uint16_t nMemAddressRead, uint16_t* data) {
    (void)slaveAddr;
    (void)startAddress;
    (void)nMemAddressRead;
    (void)data;
    return -1;
}

void MLX90640_I2CFreqSet(int freq) {
    (void)freq;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data) {
    (void)slaveAddr;
    (void)writeAddress;
    (void)data;
    return -1;
}

#endif
