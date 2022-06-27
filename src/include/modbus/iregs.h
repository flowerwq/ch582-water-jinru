#ifndef __MODBUS_IREGS_H__
#define __MODBUS_IREGS_H__
#include "stdint.h"
#include "liblightmodbus.h"
#include "appinfo.h"

typedef enum mb_ireg_addr{
#if APP_PID
	MB_IREG_ADDR_BASE = 0,
	MB_IREG_ADDR_MY18B20Z = MB_IREG_ADDR_BASE,
	MB_IREG_ADDR_MAX,
	
	MB_IREG_ADDR_WATER_BASE = 1,
	MB_IREG_ADDR_WATER_LEVEL = MB_IREG_ADDR_WATER_BASE,
	MB_IREG_ADDR_WATER_MAX
#endif
} mb_ireg_addr_t;

void modbus_ireg_update(mb_ireg_addr_t addr, uint16_t value);
void modbus_iregs_init();
ModbusError modbus_ireg_callback(void *ctx, 
	const ModbusRegisterCallbackArgs *args,
	ModbusRegisterCallbackResult *out);

#endif
