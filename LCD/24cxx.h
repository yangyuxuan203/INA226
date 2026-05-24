#ifndef __24CXX_H
#define __24CXX_H

#include "main.h"

void at24cxx_init(void);
void at24cxx_write(uint16_t addr, uint8_t *pbuf, uint16_t len);
void at24cxx_read(uint16_t addr, uint8_t *pbuf, uint16_t len);
void at24cxx_write_one_byte(uint16_t addr, uint8_t data);
uint8_t at24cxx_read_one_byte(uint16_t addr);

#endif
