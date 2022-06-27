#include <stdio.h>
#include <string.h>
#include "modbus.h"
#include "CH58x_common.h"
#include "worktime.h"
#include "configtool.h"
#include "utils.h"

#define TAG "MB"

static int8_t mb_timer_ref;
static mb_slave_ctx_t mb_slave_ctx;

#define MB_TIMER_IS_RUNNING() (R8_TMR0_CTRL_MOD & RB_TMR_COUNT_EN)
#define MB_TIMER_STOP()	TMR0_Disable()
#define MB_TIMER_RESUME()	TMR0_TimerInit(modbus_t05_cnt(mb_slave_ctx.baudrate))

static uint32_t modbus_t05_cnt(uint32_t baudrate){
	if (baudrate <= 19200){
		return (GetSysClock() * 5 / baudrate);
	}
	return GetSysClock() / 1000000 * 750;
}

static void modbus_timer_enable(){
	if (!MB_TIMER_IS_RUNNING()){
		TMR0_TimerInit(modbus_t05_cnt(mb_slave_ctx.baudrate));
	}
	mb_timer_ref ++;
}

static void modbus_timer_disable(){
	mb_timer_ref --;
	if (mb_timer_ref <= 0){
		TMR0_Disable();
	}
}
static void modbus_slave_set_idle(){
	mb_slave_ctx.flag_frame_err = 0;
	mb_slave_ctx.tcnt = 0;
	mb_slave_ctx.req_len = 0;
	mb_slave_ctx.status = MODBUS_S_IDLE;
}

/*********************************************************************
 * @fn      TMR0_IRQHandler
 *
 * @brief   TMR0中断函数
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void) // TMR0 定时中断
{
	if(TMR0_GetITFlag(TMR0_3_IT_CYC_END)){
		TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
	}
	mb_slave_ctx.tcnt ++;
	switch(mb_slave_ctx.status){
		case MODBUS_S_INIT:
			if (mb_slave_ctx.tcnt >= 7){
				mb_slave_ctx.tcnt = 0;
				mb_slave_ctx.status = MODBUS_S_IDLE;
			}
			break;
		case MODBUS_S_IDLE:
		case MODBUS_S_RECV_FINISH:
			mb_slave_ctx.tcnt = 0;
			break;
		case MODBUS_S_RECV:
			if (mb_slave_ctx.tcnt >= 3){
				mb_slave_ctx.status = MODBUS_S_CTRL_AND_WAITING;
				break;
			}
			break;
		case MODBUS_S_CTRL_AND_WAITING:
			if (mb_slave_ctx.tcnt >= 7){
				if (mb_slave_ctx.flag_frame_err){
					modbus_slave_set_idle();
				}else{
					mb_slave_ctx.status = MODBUS_S_RECV_FINISH;
				}
				modbus_timer_disable();
			}
			break;
		default:
			break;
	}
}

//init timer
static void modbus_timer_init(){

    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END); // 开启中断
    PFIC_EnableIRQ(TMR0_IRQn);
	TMR0_Disable();
}

void slave_recv(uint8_t d){
	int framelen, stoped;
	stoped = 0;
	if (MB_TIMER_IS_RUNNING()){
		MB_TIMER_STOP();
		stoped = 1;
	}
	framelen = sizeof(mb_slave_ctx.req_buf);
	switch(mb_slave_ctx.status){
		case MODBUS_S_INIT:
		case MODBUS_S_IDLE:
			mb_slave_ctx.tcnt = 0;
			mb_slave_ctx.status = MODBUS_S_RECV;
			mb_slave_ctx.req_buf[mb_slave_ctx.req_len] = d;
			mb_slave_ctx.req_len ++;
			break;
		case MODBUS_S_RECV:
			mb_slave_ctx.tcnt = 0;
			if (mb_slave_ctx.req_len < framelen){
				mb_slave_ctx.req_buf[mb_slave_ctx.req_len] = d;
				mb_slave_ctx.req_len ++;
			}
			break;
		case MODBUS_S_CTRL_AND_WAITING:
			mb_slave_ctx.tcnt = 0;
			mb_slave_ctx.flag_frame_err = 1;
			break;
		default:
			break;
	}
out:
	if (stoped){
		MB_TIMER_RESUME();

	}
	if (1 == mb_slave_ctx.req_len){
		modbus_timer_enable();
	}
}

ModbusError register_callback( const ModbusSlave *status, 
	const ModbusRegisterCallbackArgs *args,
	ModbusRegisterCallbackResult *out)
{	
	out->exceptionCode = MODBUS_EXCEP_NONE;
	void *ctx = modbusSlaveGetUserPointer(status);
	switch(args->type){
		case MODBUS_HOLDING_REGISTER:
			return modbus_reg_callback(ctx, args, out);
		break;
		case MODBUS_INPUT_REGISTER:
			return modbus_ireg_callback(ctx, args, out);
			break;
		case MODBUS_COIL:
			return modbus_coil_callback(ctx, args, out);
			break;
		case MODBUS_DISCRETE_INPUT:
			return modbus_di_callback(ctx, args, out);
			break;
	}
	return 0;
}
static LIGHTMODBUS_WARN_UNUSED ModbusError modbus_slave_allocator(
	ModbusBuffer *buffer, uint16_t size, void *context)
{
    mb_slave_ctx_t *ctx = (mb_slave_ctx_t *)context;
    if (!size)
    {
        // Pretend we're freeing the buffer
        buffer->data = NULL;
        return MODBUS_OK;
    }
    else
    {
        if (size > 256)
        {
            // Requested size is too big, return allocation error
            buffer->data = NULL;
            return MODBUS_ERROR_ALLOC;
        }
        else
        {
            // Return a pointer to our buffer
            buffer->data = ctx->resp_buf;
            return MODBUS_OK;
        }
    }
}


/*********************************************************************
 * @fn      UART3_IRQHandler
 *
 * @brief   UART3中断函数
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void UART3_IRQHandler(void)
{
    volatile uint8_t d;

    switch(UART3_GetITFlag())
    {
        case UART_II_LINE_STAT: // 线路状态错误
        {
            UART3_GetLinSTA();
            break;
        }

        case UART_II_RECV_RDY: // 数据达到设置触发点
            while(R8_UART3_RFC)
            {
                slave_recv(UART3_RecvByte());
            }
            break;

        case UART_II_RECV_TOUT: // 接收超时，暂时一帧数据接收完成
            while(R8_UART3_RFC)
            {
                slave_recv(UART3_RecvByte());
            }
            break;

        case UART_II_THR_EMPTY: // 发送缓存区空，可继续发送
            break;

        case UART_II_MODEM_CHG: // 只支持串口0
            break;

        default:
            break;
    }
}

int modbus_slave_uart_init(mb_slave_ctx_t *slave_ctx){
	GPIOA_ModeCfg(GPIO_Pin_5,GPIO_ModeOut_PP_5mA);
	GPIOA_SetBits(GPIO_Pin_5);
	GPIOA_ModeCfg(GPIO_Pin_4,GPIO_ModeIN_PU);
	UART3_DefInit();
	
	UART3_BaudRateCfg(slave_ctx->baudrate);
	UART3_ByteTrigCfg(UART_1BYTE_TRIG);
	UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
	PFIC_EnableIRQ(UART3_IRQn);
	return 0;
}
int modbus_slave_uart_send(const uint8_t *buf, int len){
	if (!buf || len <= 0){
		return -1;
	}
	GPIOB_ResetBits(GPIO_Pin_18);
	UART3_SendString((uint8_t *)buf, len);
	while(R8_UART3_TFC || !(R8_UART3_LSR & RB_LSR_TX_ALL_EMP));
	
	PRINT("%s:uart_send\r\n",__FUNCTION__);
	GPIOB_SetBits(GPIO_Pin_18);
	return 0;
}

int modbus_slave_init(mb_callback_t *callback){
	cfg_uart_t cfg_uart = {0};
	uint8_t addr = 20;
	cfg_get_mb_uart(&cfg_uart);
	cfg_get_mb_addr(&addr);
	if (!addr){
		addr = 20;
	}
	LOG_DEBUG(TAG, "slave init, addr:%d, baudrate:%d", addr, cfg_uart.baudrate);
	memset(&mb_slave_ctx, 0, sizeof(mb_slave_ctx_t));
	mb_slave_ctx.address = addr;
	mb_slave_ctx.baudrate = cfg_uart.baudrate;
	mb_slave_ctx.slave.registerCallback = register_callback;
	if(callback){
		memcpy(&mb_slave_ctx.callback, callback, sizeof(mb_callback_t));
	}
	ModbusErrorInfo err = modbusSlaveInit(&mb_slave_ctx.slave, 
		register_callback, NULL, modbus_slave_allocator, 
		modbusSlaveDefaultFunctions, modbusSlaveDefaultFunctionCount);
	if (!modbusIsOk(err)){
		return -1;
	}
	modbusSlaveSetUserPointer(&mb_slave_ctx.slave, &mb_slave_ctx);
	modbus_slave_uart_init(&mb_slave_ctx);
	return 0;
}

int modbus_sa_ctrl(int enable){
	mb_slave_ctx.flag_safe_access = enable ? 1 : 0;
	if (enable){
		mb_slave_ctx.lasttime_sa = worktime_get();
	}
	return 0;
}
void modbus_init(mb_callback_t *callback){
	mb_timer_ref = 0;
	modbus_regs_init();
	modbus_iregs_init();
	mb_di_init();
	if (modbus_slave_init(callback) < 0){
		LOG_ERROR(TAG, "slave init failed.");
		return;
	}
	modbus_timer_init();
}

void modbus_deinit(){
	UART3_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
	MB_TIMER_STOP();
	modbusSlaveDestroy(&mb_slave_ctx.slave);
	memset(&mb_slave_ctx, 0, sizeof(mb_slave_ctx_t));
}

uint32_t modbus_lasttime_recv(){
	return mb_slave_ctx.lasttime_recv;
}

void modbus_frame_check(){
	ModbusErrorInfo err;
	if (MODBUS_S_RECV_FINISH == mb_slave_ctx.status){
		mb_slave_ctx.lasttime_recv = worktime_get()/1000;
		if (!mb_slave_ctx.flag_frame_err){
			err = modbusParseRequestRTU( &mb_slave_ctx.slave, 
				mb_slave_ctx.address, mb_slave_ctx.req_buf, mb_slave_ctx.req_len);
			if (MODBUS_OK != modbusGetErrorCode(err)){
				LOG_ERROR(TAG, "modbus parse err(%02x:%s)", 
					modbusGetErrorCode(err), modbusErrorStr(modbusGetErrorCode(err)));
				LOG_DEBUG(TAG, "rx:");
				log_buffer_hex(TAG, mb_slave_ctx.req_buf, mb_slave_ctx.req_len, 
					LOG_LEVEL_DEBUG);
			}
			if (MODBUS_ERROR_ADDRESS != modbusGetErrorCode(err)){
				if (modbusSlaveGetResponseLength(&mb_slave_ctx.slave) > 0){
					
					modbus_slave_uart_send(modbusSlaveGetResponse(&mb_slave_ctx.slave), 
						modbusSlaveGetResponseLength(&mb_slave_ctx.slave));
					
				}
			}
		}else{
			PRINT("modbus frame err");
		}
		modbus_slave_set_idle();
	}
}

void modbus_run(){
	if (mb_slave_ctx.flag_safe_access && 
		worktime_since(mb_slave_ctx.lasttime_sa) >= 3000)
	{
		mb_slave_ctx.flag_safe_access = 0;
		modbus_reg_update(MB_REG_ADDR_SAFE_ACCESS_CTRL, 0);
	}
	modbus_frame_check();
}
int modbus_is_receiving(){
	return MODBUS_S_RECV == mb_slave_ctx.status;
}
