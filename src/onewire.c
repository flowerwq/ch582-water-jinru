#include <CH58x_common.h>
#include <stdbool.h>
#include <math.h>
#include "onewire.h"

#include <CH58x_gpio.h>
#include  <CH58x_i2c.h>
#include <stdint.h>
#include <stdio.h>
#include "oled.h"
#include "modbus.h"
#include "utils.h"
#include "appinfo.h"
//#include "discrete_input.h"

//typedef struct{
//
//	uint8_t buffer;
//
//	int LastDiscrepancy;
//	int LastFamilyDiscrepancy;
//	int LastDeviceFlag;
//	unsigned char crc8;
//	/****全局变量：保存和电容配置寄存器对应的偏置电容和量程电容数值****/
//	float CapCfg_offset, CapCfg_range;
//	uint8_t CapCfg_ChanMap, CapCfg_Chan;
////	unsigned char ROM_NO[8];
//}onewire;
//uint16_t ROMID[MAXNUM_SWITCH_DEVICE_OUT][8];
unsigned char ROM_NO[8];
struct dev_slave{
	device_type_t type;
	uint8_t ROMID[MAXNUM_SWITCH_DEVICE_OUT][8];
};


struct dev_slave device_t;

/****偏置电容和反馈电容阵列权系数****/
static const float COS_Factor[8] = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 40.0};
/*Cos= (40.0*q[7]+32.0*q[6]+16.0*q[5]+8.0*q[4]+4.0*q[3]+2.0*q[2]+1.0*q[1]+0.5*q[0])*/
static const struct  {float Cfb0; float Factor[6];} CFB = { 2.0, 2.0, 4.0, 8.0, 16.0, 32.0, 46.0};
/*Cfb =(46*p[5]+32*p[4]+16*p[3]+8*p[2]+4*p[1]+2*p[0]+2)*/

static volatile int ow_flag_timerout = 0;

onewire onewire_dev;
/*********************************************************************
 * @fn      TMR0_IRQHandler
 *
 * @brief   TMR0中断函数
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void TMR1_IRQHandler(void) // TMR1 定时中断
{
    if(TMR1_GetITFlag(TMR0_3_IT_CYC_END))
    {
    	TMR1_Disable();
        TMR1_ClearITFlag(TMR0_3_IT_CYC_END); // 清除中断标志
        ow_flag_timerout = 1;
    }
}


static int ow_timer_init(){
	TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END); // 开启中断
	PFIC_EnableIRQ(TMR1_IRQn);
	return 0;
}

static int ow_timer_start(uint32_t timeout_us){
	ow_flag_timerout = 0;
	TMR1_TimerInit(GetSysClock() / 1000000  * timeout_us);
	return 0;
}

static int ow_timer_stop(){
	TMR1_Disable();
	return 0;
}

static int ow_delay_us(uint32_t us){
	ow_timer_start(us);
	while(!ow_flag_timerout);
	ow_timer_stop();
	return 0;
}

void onewire_init(){
	ow_timer_init();
	GPIOB_SetBits(GPIO_Pin_12);
	ow_delay_us(100);
	GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeOut_PP_5mA);
	onewire_resetcheck();	
}

int onewire_resetcheck(){
	uint8_t flag=0;

	onewire_reset();
	flag=check_ack();
	if(flag != 0){
		PRINT("error\r\n");
	}
	ow_delay_us(240);
	return flag;
}
int check_ack(){
	uint8_t ack_flag,i;
	int cnt = 0;
	while(GPIOB_ReadPortPin(GPIO_Pin_12))	
	{
		ow_delay_us(1);
		cnt ++;
		if (cnt >= 60){
			PRINT("wait falling edge timeout\r\n");
			goto fail;
		}
	}
	cnt = 0;
	while(0 == GPIOB_ReadPortPin(GPIO_Pin_12)){
		ow_delay_us(10);
		cnt +=10;
		if (cnt >= 240){
			PRINT("err: slave not release wire\r\n");	//？
			goto fail;
		}
	}
	if (cnt < 60){
		PRINT("err: low time < 60\r\n");
		goto fail;
	}
	return 0;
fail:
	return 1;
}

void onewire_reset(){
	GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeOut_PP_5mA);
	GPIOB_ResetBits(GPIO_Pin_12);
	ow_delay_us(500);
	GPIOB_SetBits(GPIO_Pin_12);
	GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeIN_PU);
}




void  onewire_writebit(int bit){
	GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeOut_PP_5mA);
	GPIOB_ResetBits(GPIO_Pin_12);
	if (bit)
	{
		// Write '1' to DQ
		ow_delay_us(15);//15s 内释放总线
		GPIOB_SetBits(GPIO_Pin_12);
		ow_delay_us(50); 
	}
	else
	{
		// Write '0' to DQ
		ow_delay_us(60);
	}
	GPIOB_SetBits(GPIO_Pin_12);
	ow_delay_us(2); //恢复时间
	
}
void onewire_writebyte(char data){
	uint8_t i; 
	for(i = 0;i < 8;i++){
		onewire_writebit(data & 0x01);//data&0x01：将data数据的最低位赋值给data
		data >>=1;
	}
}


unsigned char onewire_read(){
	uint8_t i;
	uint8_t read_data=0;
	for(i=0;i<8;i++){
		
		read_data >>= 1;
		
		if(onewire_readbit()){
			read_data |= 0x80;
		}	
	}
	return read_data;
}
int onewire_readbit(){
	int j=0;
	GPIOB_ModeCfg(GPIO_Pin_12,GPIO_ModeOut_PP_5mA);
	GPIOB_ResetBits(GPIO_Pin_12);
	
	ow_delay_us(2);//启动读时隙
	GPIOB_SetBits(GPIO_Pin_12);
	GPIOB_ModeCfg(GPIO_Pin_12, GPIO_ModeIN_PU);

	ow_delay_us(5);
	
	j=GPIOB_ReadPortPin(GPIO_Pin_12);//因为“0”和“1”是通过电平的高低表示的
	if(j == 0){
		j=0;
		ow_delay_us(4);
		if (!GPIOB_ReadPortPin(GPIO_Pin_12)){
			goto fail;
		}
	}else{
		j=1;
	}
	ow_delay_us(60);
	return j;
fail:
	return -1;
}

int onewire_read2bits()
{
	uint8_t i, dq,data = 0;
	for(i=0; i<2; i++)
	{
		dq = onewire_readbit();
		data = (data) | (dq<<i);
	}

		return data;
}

void read_function(){
	uint8_t b=0;
	uint8_t c=0;
	unsigned char a=0;
	onewire_reset();
	check_ack();
	
	PRINT("%02x\r\n",c);
	
}



//
////
/////********    convert CAP       ********/////////////

/**
  * @brief  读电容配置
  * @param  Coffset：配置的偏置电容。
  * @param  Crange：配置的量程电容。
  * @retval 无
*/

bool readcapconfigure(float *Coffset, float *Crange,int num)
{
	getcfg_capoffset(Coffset,num);
	getcfg_caprange(Crange,num);

	return TRUE;
}
////
/////**
////  * @brief  获取配置的量程电容数值（pF）
////  * @param  Crange：返回量程电容数值
////  * @retval 无
////*/
void getcfg_caprange(float *Crange,int num)
{
	uint8_t Cfb_cfg;

	readcfbconfig(&Cfb_cfg,num);
	*Crange = cfbcfgtocaprange(Cfb_cfg);
//	PRINT("cfbcfg to capcfb:%.4f\r\n",*Crange);
}
//
///* @brief  获取配置的偏置电容数值（pF）
//  * @param  Coffset：偏置电容配置
//  * @retval 无
//*/
void getcfg_capoffset(float *Coffset,int num)
{	uint8_t Cos_cfg;

	readcosconfig(&Cos_cfg,num);
	*Coffset = coscfgtocapoffset(Cos_cfg);
//	PRINT("coscfg to capoffset:%.2f\r\n",*Coffset);
}
//
////
/////**
////  * @brief  将量程电容配置转换为对应的量程电容数值（pF）
////  * @param  fbCfg：量程电容配置
////  * @retval 对应量程电容的数值
////*/
float cfbcfgtocaprange(uint8_t fbCfg)
{
	uint8_t i;
	float Crange = CFB.Cfb0;

	for(i = 0; i <= 5; i++)
	{
		if(fbCfg & 0x01){
			Crange += CFB.Factor[i];
		}
		fbCfg >>= 1;
	}
	return (0.507/3.6) * Crange;
}
//
/////**
////  * @brief  读量程电容配置寄存器内容
////  * @param  Cfb：量程配置寄存器低6位的内容
////  * @retval 状态
////*/
bool readcfbconfig(uint8_t *Cfb,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRPARAMETERS)];
	MDC02_SCRPARAMETERS *scr = (MDC02_SCRPARAMETERS *) scrb;

	/*读15个字节。第5字节是偏置电容配置寄存器，第10字节是量程电容配置寄存器，最后字节是前14个的校验和--CRC。*/
	if(mdc02_readparameters_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}

	/*计算接收的前14个字节的校验和，并与接收的第15个CRC字节比较。*/
	if(scrb[sizeof(MDC02_SCRPARAMETERS)-1] != onewire_crc8(&scrb[0], sizeof(MDC02_SCRPARAMETERS)-1))
	{
		return FALSE;   /*CRC验证未通过*/
	}

	*Cfb = scr->Cfb & MDC02_CFEED_CFB_MASK;
//	PRINT("read register cfb:%d\r\n",*Cfb);
	return TRUE;;
}
//
//
///**
//  * @brief  将偏置电容配置转换为对应的偏置电容数值（pF）
//  * @param  osCfg：偏置电容配置
//  * @retval 对应偏置电容的数值
//*/
float coscfgtocapoffset(uint8_t osCfg)
{
	uint8_t i;
	float Coffset = 0.0;

	for(i = 0; i < 8; i++)
	{
		if(osCfg & 0x01) Coffset += COS_Factor[i];{
			osCfg >>= 1;
		}
	}

	return Coffset;
}

//
////
/**
  * @brief  读偏置电容配置寄存器内容
  * @param  Coffset：偏置配置寄存器有效位的内容
  * @retval 无
*/

bool readcosconfig(uint8_t *Coscfg,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRPARAMETERS)];
	MDC02_SCRPARAMETERS *scr = (MDC02_SCRPARAMETERS *) scrb;

	/*读15个字节。第5字节是偏置电容配置寄存器，第10字节是量程电容配置寄存器，最后字节是前14个的校验和--CRC。*/
	if(mdc02_readparameters_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}

	/*计算接收的前14个字节的校验和，并与接收的第15个CRC字节比较。*/
  	if(scrb[sizeof(MDC02_SCRPARAMETERS)-1] != onewire_crc8(&scrb[0], sizeof(MDC02_SCRPARAMETERS)-1))
  	{
  		return FALSE;  /*CRC验证未通过*/
  	}

	*Coscfg = scr->Cos & (0xFF >> (3 - (scr->Cfb >> 6))); //屏蔽掉无效位，根据CFB寄存器的高2位
//	PRINT("read register cos:%d\r\n",*Coscfg);

  	return TRUE;
}
////
bool mdc02_writeparameters_skiprom(uint8_t *scr, int num)
{
    int16_t i;

//    if(onewire_resetcheck() != 0){
//		return FALSE;
//	}
//
//    read_command();
	send_matchrom(device_t.ROMID,num);

    onewire_writebyte(WRITE_PARAMETERS);

	for(i=0; i < sizeof(MDC02_SCRPARAMETERS); i++)
    {
    	onewire_writebyte(*scr++);
	}

    return TRUE;
}
bool mdc02_readparameters_skiprom(uint8_t *scr,int num)
{
    int16_t i;
//
//    if(onewire_resetcheck() != 0){
//		return FALSE;
//	}
//
//    read_command();
	send_matchrom(device_t.ROMID,num);
    onewire_writebyte(READ_PARAMETERS );

	for(i=0; i < sizeof(MDC02_SCRPARAMETERS); i++)
    {
    	*scr++ = onewire_read();
	}

    return TRUE;
}

///**
//  * @brief  读状态和配置
//  * @param  status 返回的状态寄存器值
//  * @param  cfg 返回的配置寄存器值
//  * @retval 状态
//*/
bool readstatusconfig(uint8_t *status, uint8_t *cfg,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRATCHPAD_READ)];
	MDC02_SCRATCHPAD_READ *scr = (MDC02_SCRATCHPAD_READ *) scrb;

	/*读9个字节。第7字节是系统配置寄存器，第8字节是系统状态寄存器。最后字节是前8个的校验和--CRC。*/
	if(mdc02_readscratchpad_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*CRC验证未通过*/
	}

	/*计算接收的前8个字节的校验和，并与接收的第9个CRC字节比较。*/
  	if(scrb[8] != onewire_crc8(&scrb[0], 8))
  	{
  		return FALSE;  /*CRC验证未通过*/
  	}

	*status = scr->Status;
	*cfg = scr->Cfg;

	return TRUE;
}
//
////
/////**
////  * @brief  读芯片寄存器的暂存器组
////  * @param  scr：字节数组指针， 长度为 @sizeof（MDC04_SCRATCHPAD_READ）
////  * @retval 读状态
////*/

bool mdc02_writescratchpadext_skiprom(uint8_t *scr)
{
    int16_t i;

    if(onewire_resetcheck() != 0)
			return FALSE;
	read_command();
    onewire_writebyte(0x77);

	for(i=0; i<sizeof(MDC02_SCRATCHPADEXT)-1; i++)
	{
		onewire_writebyte(*scr++);
	}

    return TRUE;
}
/**
  * @brief  写芯片寄存器的暂存器组
  * @param  scr：字节数组指针， 长度为 @sizeof（MDC04_SCRATCHPAD_WRITE）
  * @retval 写状态
**/
bool mdc02_writescratchpad_skiprom(uint8_t *scr,int num)
{
    int16_t i;

  	send_matchrom(device_t.ROMID, num);
    onewire_writebyte(WRITE_SCRATCHPAD);

	for(i=0; i < sizeof(MDC02_SCRATCHPAD_WRITE); i++)
	{
		onewire_writebyte(*scr++);
	}

    return TRUE;
}


/**
  * @brief  设置周期测量频率和重复性
  * @param  mps 要设置的周期测量频率（每秒测量次数），可能为下列其一
	*				@arg CFG_MPS_Single		：每执行ConvertTemp一次，启动一次温度测量
	*				@arg CFG_MPS_Half			：每执行ConvertTemp一次，启动每秒0.5次重复测量
	*				@arg CFG_MPS_1				：每执行ConvertTemp一次，启动每秒1次重复测量
	*				@arg CFG_MPS_2				：每执行ConvertTemp一次，启动每秒2次重复测量
	*				@arg CFG_MPS_4				：每执行ConvertTemp一次，启动每秒4次重复测量
	*				@arg CFG_MPS_10				：每执行ConvertTemp一次，启动每秒10次重复测量
  * @param  repeatability：要设置的重复性值，可能为下列其一
	*				@arg CFG_Repeatbility_Low				：设置低重复性
	*				@arg CFG_Repeatbility_Medium		：设置中重复性
	*				@arg CFG_Repeatbility_High			：设置高重复性
  * @retval 无
*/
bool setconfig(uint8_t mps, uint8_t repeatability,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRATCHPAD_READ)];
	MDC02_SCRATCHPAD_READ *scr = (MDC02_SCRATCHPAD_READ *) scrb;

	/*读9个字节。第7字节是系统配置寄存器，第8字节是系统状态寄存器。最后字节是前8个的校验和--CRC。*/
	if(mdc02_readscratchpad_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读暂存器组水平*/
	}

	/*计算接收的前8个字节的校验和，并与接收的第9个CRC字节比较。*/
  if(scrb[8] != onewire_crc8(&scrb[0], 8))
  {
		return FALSE;  /*CRC验证未通过*/
  }

	scr->Cfg &= ~CFG_Repeatbility_Mask;
	scr->Cfg |= repeatability;
	scr->Cfg &= ~CFG_MPS_Mask;
	scr->Cfg |= mps;

	mdc02_writescratchpad_skiprom(scrb+4,num);

	return TRUE;
}

////
////
bool setcapchannel(uint8_t channel,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRPARAMETERS)];
	MDC02_SCRPARAMETERS *scr = (MDC02_SCRPARAMETERS *) scrb;

	/*读15个字节。第4字节是通道选择寄存器，最后字节是前14个的校验和--CRC。*/
	if(mdc02_readparameters_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}

	/*计算接收的前14个字节的校验和，并与接收的第15个CRC字节比较。*/
	if(scrb[sizeof(MDC02_SCRPARAMETERS)-1] != onewire_crc8(&scrb[0], sizeof(MDC02_SCRPARAMETERS)-1))
	{
		return FALSE;  /*CRC验证未通过*/
	}

	scr->Ch_Sel = (scr->Ch_Sel & ~CCS_CHANNEL_MASK) | (channel & CCS_CHANNEL_MASK);

	mdc02_writeparameters_skiprom(scrb,num);

	return TRUE;
}

bool convertcap(int num)
{
//	if(onewire_resetcheck() !=  0){
//		return FALSE;
//    }
//
//    read_command();
	send_matchrom(device_t.ROMID,num);
    WWDG_SetCounter(0 );

    onewire_writebyte(CONVERT_C);//启动电容转换
    return TRUE;
}
/**
  * @brief  启动温度测量
  * @param  无
  * @retval 单总线发送状态
*/
bool converttemp(int num)
{
	send_matchrom(device_t.ROMID,num);
	onewire_writebyte(CONVERT_T);

  return TRUE;
}

/**
  * @brief  启动温度和电容通道1同时测量
  * @param  无
  * @retval 单总线发送状态
*/
bool convert_tempcap1(int num)
{
//	if(onewire_resetcheck() != 0){
//		return FALSE;
//	}
	
//	read_command();
	send_matchrom(device_t.ROMID,num);
    WWDG_SetCounter(0 );

	onewire_writebyte(CONVERT_TC1);

  return TRUE;
}


bool readtempcap1(uint16_t *iTemp, uint16_t *iCap1,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRATCHPAD_READ)];
	MDC02_SCRATCHPAD_READ *scr = (MDC02_SCRATCHPAD_READ *) scrb;

	/*读9个字节。前两个是温度转换结果，最后字节是前8个的校验和--CRC。*/
	if(mdc02_readscratchpad_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}
    WWDG_SetCounter(0 );
	/*计算接收的前8个字节的校验和，并与接收的第9个CRC字节比较。*/
	if(scrb[8] != onewire_crc8(&scrb[0], 8))
	{
	    return FALSE;  /*CRC验证未通过*/
	}
	
	*iTemp=(uint16_t)scr->T_msb<<8 | scr->T_lsb;
	*iCap1=(uint16_t)scr->C1_msb<<8 | scr->C1_lsb;
	return TRUE;
}
////
/////**
////  * @brief  读电容通道2，1
////  * @param  scr：字节数组指针， 长度为 @sizeof（MDC04_C2C3C4）
////  * @retval 写状态
////**/

bool  mdc02_readscratchpad_skiprom(uint8_t *scr,int num)
{
    int16_t i;

	/*size < sizeof(MDC04_SCRATCHPAD_READ)*/
//    if(onewire_resetcheck() != 0){
//		return FALSE;
//	}
//
//    read_command();
	send_matchrom(device_t.ROMID,num);
    WWDG_SetCounter(0 );
    onewire_writebyte(READ_SCRATCHPAD);//读取包含配置寄存器在内的所有暂存器内容命令

	for(i=0; i < sizeof(MDC02_SCRATCHPAD_READ); i++)
    {
		*scr++ = onewire_read();
	}

    return TRUE;
}



bool mdc02_readc2c3c4_skiprom(uint8_t *scr,int num)
{
    int16_t i;

//    if(onewire_resetcheck() != 0){
//			return FALSE;
//	}
//
//    read_command();
	send_matchrom(device_t.ROMID,num);

    onewire_writebyte(READ_C2C3C4);//读取电容通道 2、 3 和 4 命令

	for(i=0; i < sizeof(MDC02_C2C3C4); i++)
   {
	    *scr++ = onewire_read();
	}

    return TRUE;
}
////
////
////
bool mdc02_capconfigureoffset(float Coffset,int num)
{
	uint8_t CosCfg, Cosbits;
	float b=Coffset+0.25;
	CosCfg = captocoscfg(b);

	if(!(CosCfg & ~0x1F)) {
		Cosbits = COS_RANGE_5BIT;
	}
	else if(!(CosCfg & ~0x3F)) {
		Cosbits = COS_RANGE_6BIT;
	}
	else if(!(CosCfg & ~0x7F)){
		Cosbits = COS_RANGE_7BIT;
	}
	else{
		Cosbits = COS_RANGE_8BIT;
	}
//	PRINT("%d  %d\r\n",CosCfg,Cosbits);
	writecosconfig(CosCfg, Cosbits,num);

	return TRUE;
}

/**
  * @brief  写偏置电容配置寄存器和有效位宽设置
  * @param  Coffset：偏置配置寄存器的数值
  * @param  Cosbits：偏置配置寄存器有效位宽，可能为：
	*		@COS_RANGE_5BIT
	*		@COS_RANGE_6BIT
	*		@COS_RANGE_7BIT
	*		@COS_RANGE_8BIT
  * @retval 状态
*/
bool writecosconfig(uint8_t Coffset, uint8_t Cosbits,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRPARAMETERS)];
	MDC02_SCRPARAMETERS *scr = (MDC02_SCRPARAMETERS *) scrb;

	/*读15个字节。第5字节是偏置电容配置寄存器，第10字节是量程电容配置寄存器，最后字节是前14个的校验和--CRC。*/
	if(mdc02_readparameters_skiprom(scrb,num) == FALSE)
	{
		return FALSE;   /*读寄存器失败*/
	}

	/*计算接收的前14个字节的校验和，并与接收的第15个CRC字节比较。*/
  	if(scrb[sizeof(MDC02_SCRPARAMETERS)-1] != onewire_crc8(&scrb[0], sizeof(MDC02_SCRPARAMETERS)-1))
  	{
		return FALSE;  /*CRC验证未通过*/
  	}

	scr->Cos = Coffset;
	scr->Cfb = (scr->Cfb & ~CFB_COSRANGE_Mask) | Cosbits;
//	PRINT("write cosconfig:%d %f\r\n",scr->Cos,scr->Cfb);

	mdc02_writeparameters_skiprom(scrb,num);

  	return TRUE;
}

/**
  * @brief  将偏置电容数值（pF）转换为对应的偏置电容配置
  * @param  osCap：偏置电容的数值
  * @retval 对应偏置配置寄存器的数值
*/
uint8_t captocoscfg(float osCap)
{
	int i; 
	uint8_t CosCfg = 0x00;

	for(i = 7; i >= 0; i--)
	{
		if(osCap >= COS_Factor[i])
		{
			CosCfg |= (0x01 << i);
			osCap -= COS_Factor[i];
		}
	}
	return CosCfg;
}

/**
  * @brief  配置电容测量范围
  * @param  Cmin：要配置测量范围的低端。
  * @param  Cmax：要配置测量范围的高端。
  * @retval 状态
*/
bool mdc02_capconfigurerange(float Cmin, float Cmax,int num)
{ 
	float Cfs, Cos;

//	if(!((Cmax <= 119.0) && (Cmax > Cmin) && (Cmin >= 0.0) && ((Cmax-Cmin) <= 31.0)))
//	return FALSE;	//The input value is out of range.

	Cos = (Cmin + Cmax)/2.0;
	Cfs = (Cmax - Cmin)/2.0;

	mdc02_capconfigureoffset(Cos,num);
	mdc02_capconfigurefs(Cfs,num);

	return TRUE;
}

/**
  * @brief  配置量程电容
  * @param  Cfs：要配置的量程电容数值。范围+/-（0.281~15.49） pF。
  * @retval 状态
*/
bool mdc02_capconfigurefs(float Cfs,int num)
{
	uint8_t Cfbcfg;

	Cfs = (Cfs + 0.1408);
	Cfbcfg = caprangetocfbcfg(Cfs);
//	PRINT("before write cfbcfg: %d\r\n",Cfbcfg);

	writecfbconfig(Cfbcfg,num);

	return TRUE;
}

/**
  * @brief  将量程电容数值（pF）转换为对应的量程电容配置
  * @param  fsCap：量程电容的数值
  * @retval 对应量程配置的数值
*/
uint8_t caprangetocfbcfg(float fsCap)
{
	int8_t i; 
	uint8_t CfbCfg = 0x00;

	fsCap = fsCap * (3.6/0.507);

	fsCap -= CFB.Cfb0;

	for(i = 5; i >= 0; i--)
	{
		if(fsCap >= CFB.Factor[i])
		{
			fsCap -= CFB.Factor[i];
			CfbCfg |= (0x01 << i);
		}
	}

	return CfbCfg;
}
/**
  * @brief  写量程电容配置寄存器
  * @param  Cfb：量程配置寄存器低6位的内容
  * @retval 状态
*/
bool writecfbconfig(uint8_t Cfb,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRPARAMETERS)];
	MDC02_SCRPARAMETERS *scr = (MDC02_SCRPARAMETERS *) scrb;

	/*读15个字节。第5字节是偏置电容配置寄存器，第10字节是量程电容配置寄存器，最后字节是前14个的校验和--CRC。*/
	if(mdc02_readparameters_skiprom(scrb,num) == FALSE)
	{
		return FALSE;   /*读寄存器失败*/
	}

	/*计算接收的前14个字节的校验和，并与接收的第15个CRC字节比较。*/
  	if(scrb[sizeof(MDC02_SCRPARAMETERS)-1] != onewire_crc8(&scrb[0], sizeof(MDC02_SCRPARAMETERS)-1))
  	{
  		return FALSE;  /*CRC验证未通过*/
	}

	scr->Cfb &= ~CFB_CFBSEL_Mask;
	scr->Cfb |= Cfb;
//	PRINT("write cfb:%d\r\n",scr->Cfb);

	mdc02_writeparameters_skiprom(scrb,num);
	return TRUE;
}


//
//
///**
//  * @brief  读电容通道2，3和4的测量结果。和 @ConvertCap联合使用
//  * @param  icap：数组指针
//  * @retval 读结果状态
//*/
bool readcapc2c3c4(uint16_t *iCap,int num)
{
	uint8_t scrb[sizeof(MDC02_C2C3C4)];
	MDC02_C2C3C4 *scr = (MDC02_C2C3C4 *) scrb;

	/*读6个字节。每两个字节依序分别为通道2、3和4的测量结果，最后字节是前两个的校验和--CRC。*/
	if(mdc02_readc2c3c4_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}
    WWDG_SetCounter(0 );
	/*计算接收的前两个字节的校验和，并与接收的第3个CRC字节比较。*/
//  if(scrb[3] != onewire_crc8(scrb, 2))
//  {
//		return FALSE;  /*CRC验证未通过*/
//  }

	*iCap= (uint16_t)scr->C2_msb<<8 | scr->C2_lsb;
	PRINT("%.2x\r\n",*iCap);
//	iCap[1] = (uint16_t)scr->C3_msb<<8 | scr->C3_lsb;
//	iCap[2] = (uint16_t)scr->C4_msb<<8 | scr->C4_lsb;
	return TRUE;
}

//
/**
  * @brief  把16位二进制补码表示的温度输出转换为以摄氏度为单位的温度读数
  * @param  out：有符号的16位二进制温度输出
  * @retval 以摄氏度为单位的浮点温度
*/
float mdc02_outputtotemp(int16_t out)
{
	return ((float)out/256.0 + 40.0);
}



float mdc02_outputtocap(uint16_t out, float Co, float Cr)
{
	return (2.0*(out/65535.0-0.5)*Cr+Co);
}

//////////////////////////////////////////*********/////////////////////////
//**********   research rom   **********//
//********** 查找从机的rom id   **********//


//读取rom id
bool readrom(uint8_t *scr)
{
    int16_t i;
	onewire_reset();
    if(check_ack() == 1)
			return FALSE;
		
    onewire_writebyte(0x55);//匹配从机rom指令，让总线主机在多点或单点总线上寻址一个特定的 MDC02

		for(i=0; i < sizeof(MDC02_ROMCODE); i++)
    {
			*scr++ = onewire_read();
		}

    return TRUE;
}

int onewire_read_rom(void)
{ 
	uint8_t rom_id[8];
	readrom(rom_id);
	PRINT("\r\n MDC02 ROMID :");				
	for(int i = 0;i < 8;i++)
	{
		PRINT("%2x ", rom_id[i]);				
	}		
	return 1;
}

int onewire_first()
{
   // reset the search state
   onewire_dev.LastDiscrepancy = 0;
   onewire_dev.LastDeviceFlag = FALSE;
   onewire_dev.LastFamilyDiscrepancy = 0;
   memset(ROM_NO,0,sizeof(char)*8);
   return onewire_search();
}


//--------------------------------------------------------------------------
// Find the 'next' devices on the 1-Wire bus
// Return TRUE  : device found, ROM number in ROM_NO buffer
//        FALSE : device not found, end of search
//
int onewire_next()
{
   // leave the search state alone
   return onewire_search();
}

void onewirefamilyskipsetup()
{
   // set the Last discrepancy to last family discrepancy
   onewire_dev.LastDiscrepancy = onewire_dev.LastFamilyDiscrepancy;
   onewire_dev.LastFamilyDiscrepancy = 0;

   // check for end of list
   if (onewire_dev.LastDiscrepancy == 0){
      onewire_dev.LastDeviceFlag = TRUE;
   }
}



//--------------------------------------------------------------------------
// Perform the 1-Wire Search Algorithm on the 1-Wire bus using the existing
// search state.
// Return TRUE  : device found, ROM number in ROM_NO buffer
//        FALSE : device not found, end of search
//
int onewire_search()
{
   int id_bit_number;
   int last_zero, rom_byte_number, search_result;
   int id_bit, cmp_id_bit;
   unsigned char rom_byte_mask, search_direction;

   // initialize for search
   id_bit_number = 1;
   last_zero = 0;
   rom_byte_number = 0;
   rom_byte_mask = 1;
   search_result = 0;
   onewire_dev.crc8 = 0;

   // if the last call was not the last one
   if (!onewire_dev.LastDeviceFlag){
   	  onewire_reset();
      // 1-Wire reset
      if (check_ack()){
         // reset the search
         onewire_dev.LastDiscrepancy = 0;
         onewire_dev.LastDeviceFlag = FALSE;
         onewire_dev.LastFamilyDiscrepancy = 0;
         return FALSE;
      }

      // issue the search command 
      onewire_writebyte(0xF0);  //搜寻总线上的所有从机设备

      // loop to do the search
      do
      {
         // read a bit and its complement
         id_bit = onewire_readbit();
         cmp_id_bit = onewire_readbit();

         // check for no devices on 1-wire
         if ((id_bit == 1) && (cmp_id_bit == 1)){
		 	PRINT("no device exist\r\n");
            break;
		 }
         else
         {
            // all devices coupled have 0 or 1
            if (id_bit != cmp_id_bit){
               search_direction = id_bit;  // bit write value for search
            }
            else
            {
               // if this discrepancy if before the Last Discrepancy
               // on a previous next then pick the same as last time
               if (id_bit_number < onewire_dev.LastDiscrepancy){
                  search_direction = ((ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
			   }
               else{
                  // if equal to last pick 1, if not then pick 0
                  search_direction = (id_bit_number == onewire_dev.LastDiscrepancy);
			   }

               // if 0 was picked then record its position in LastZero
               if (search_direction == 0)
               {
                  last_zero = id_bit_number;

                  // check for Last discrepancy in family
                  if (last_zero < 9){
                     onewire_dev.LastFamilyDiscrepancy = last_zero;
				  }
               }
            }

            // set or clear the bit in the ROM byte rom_byte_number
            // with mask rom_byte_mask
            if (search_direction == 1)
              ROM_NO[rom_byte_number] |= rom_byte_mask;
            else
              ROM_NO[rom_byte_number] &= ~rom_byte_mask;

            // serial number search direction write bit
            onewire_writebit(search_direction);

            // increment the byte counter id_bit_number
            // and shift the mask rom_byte_mask
            id_bit_number++;
            rom_byte_mask <<= 1;

            // if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask
            if (rom_byte_mask == 0)
            {
                //crc8 = onewire_crc8(ROM_NO[rom_byte_number],sizeof(ROM_NO[rom_byte_number]));  // accumulate the CRC
                rom_byte_number++;
                rom_byte_mask = 1;
            }
         }
      }
      while(rom_byte_number < 8);  // loop until through all ROM bytes 0-7

      // if the search was successful then
      if (!((id_bit_number < 65) || (onewire_dev.crc8 != 0)))	//id_bit_number  >= 65 && crc == 0
      {
         // search successful so set LastDiscrepancy,LastDeviceFlag,search_result
         onewire_dev.LastDiscrepancy = last_zero;

         // check for last device
         if (onewire_dev.LastDiscrepancy == 0){
            onewire_dev.LastDeviceFlag = TRUE;
		 }
         
         search_result = TRUE;
      }
   }

   // if no device found then reset counters so next 'search' will be like a first
   if (!search_result || !ROM_NO[0])
   {
      onewire_dev.LastDiscrepancy = 0;
      onewire_dev.LastDeviceFlag = FALSE;
      onewire_dev.LastFamilyDiscrepancy = 0;
      search_result = FALSE;
	  PRINT("search finish.\r\n");
	  __nop();
   }

   return search_result;
}



//多个字节的crc8算法
uint8_t onewire_crc8(uint8_t *serial, uint8_t length)
{
    uint8_t crc8 = 0x00;
    uint8_t pDataBuf;
    uint8_t i;

    while(length--) {
//        pDataBuf = *serial++;
		crc8 ^= *serial++;
        for(i=0; i<8; i++) {
            if((crc8^(pDataBuf))&0x01) {
                crc8 ^= 0x18;
                crc8 >>= 1;
                crc8 |= 0x80;
            }
            else {
                crc8 >>= 1;
            }
            pDataBuf >>= 1;
    }
  }
    return crc8;
}
////////////////////////*****************************************////////////////////////////
//search one-wire all device
void onewire_finddevic_fun(){ 
    volatile int result,i,cnt;
	PRINT("FIND ALL \r\n");
   	cnt = 0;
	result = onewire_first();
	while (result)
	{
		// print device found
	    for (i = 0; i < 8 ; i++){
	       PRINT("%.2X\t", ROM_NO[i]);
		}
	    PRINT("  %d\r\n",++cnt);

	    result = onewire_next();
	}
}
////////////////////////*****************************************////////////////////////////

//////////////////////////////////////////**************///////////////////////////////////

void onewire_find_alldevice(){
//	device device_t;
	uint8_t device_num;
	
	device_num=Search_ROM(device_t.ROMID);
	PRINT("总线上设备个数为：%d\r\n",device_num);
	for(int j=0; j<device_num;j++){
//		PRINT(" ROMID[%d]:\r\n ",j+1);
		PRINT(" ROMID[%d]: ",j);

		for(int i=0; i<8;i++){
			PRINT(" %2X", device_t.ROMID[j][i]);
		}
		PRINT("\r\n");
	}
}




uint8_t Search_ROM(uint8_t (*romID)[8]) //search algorithm : 2
{
	unsigned char k,l = 0,ConflictBit,m,n;
	unsigned char BUFFER[MAXNUM_SWITCH_DEVICE_OUT-1]={0} ;
	unsigned char id[64];
	unsigned char s = 0;
	int num = 0;
	uint8_t tempp;
	do{
		if(onewire_resetcheck() != 0){//检查总线上从机是否存在
			return FALSE;
		}
		onewire_writebyte(0xF0);//搜索 ROM 序列号指令
		for(m=0; m<8; m++){
			for(n=0; n<8; n++){
					
				k = onewire_read2bits(); //主机从 DQ 总线上读两位数据
				k = k&0x03;
				s = s>>1;

				if(k == 0x02) { //此时读到总线上当前数据位为 0
					onewire_writebit(0); //主机写 0, 使总线上数据位为 0 的设备响应
					id[(m*8+n)]=0;
				}
				else if(k == 0x01) { //此时读到总线上当前数据位为 1
					s = s|0x80;
					onewire_writebit(1); //主机写 1, 使总线上数据位为 1 的设备响应
					id[(m*8+n)] = 1;
				}
				else if(k == 0x00){ //如果读到 00, 则此位数据有冲突，需选择
					ConflictBit = m*8+n+1;
					if(ConflictBit > BUFFER[l]){ //凡遇到新的差异位，选择 0
						onewire_writebit (0);
						id[(m*8+n)] = 0;
						BUFFER[++l] = ConflictBit;
					}
					else if(ConflictBit < BUFFER[l]){//凡上次遍历时最后一个走 0 的差异位之前的差异位仍按上次遍历的老路走
						s = s|((id[(m*8+n)]&0x01)<<7);
						onewire_writebit(id[(m*8+n)]);
					}
					else if(ConflictBit == BUFFER[l]) {	//凡上次遍历时最后一个走 0 的差异位本次应走 1
						s = s|0x80;
						onewire_writebit(1);
						id[(m*8+n)] = 1; 
						l = l-1; 
					}
				}
				else { //如果读到 11，说明总线上不存在设备
					
					 //搜索完成
				} 
			}	
				
				romID[num][m] = s ;
				
			}
									
			num = num+1; 
			
		}
		while(BUFFER[l] != 0&&(num < MAXNUM_SWITCH_DEVICE_OUT)); 
		return num; //返回搜索到的从设备个数
}
/////////////////////////////////////////***********************************///////////////////////////////////


//--------------------------------------------------------------------------
// Verify the device with the ROM number in ROM_NO buffer is present.
// Return TRUE  : device verified present
//        FALSE : device not present
//
int onewire_verify()
{
   unsigned char rom_backup[8];
   int i,result,ld_backup,ldf_backup,lfd_backup;

   // keep a backup copy of the current state
   for (i = 0; i < 8; i++){
      rom_backup[i] = ROM_NO[i];
   }
   ld_backup = onewire_dev.LastDiscrepancy;
   ldf_backup = onewire_dev.LastDeviceFlag;
   lfd_backup = onewire_dev.LastFamilyDiscrepancy;

   // set search to find the same device
   onewire_dev.LastDiscrepancy = 64;
   onewire_dev.LastDeviceFlag = FALSE;

   if (onewire_search()){
   	// check if same device found
   		result = TRUE;
    	for(i = 0; i < 8; i++){
			if (rom_backup[i] != ROM_NO[i]){
				result = FALSE;
	            break;
	        }
		}
   }
   else{
   		result = FALSE;
   }

   // restore the search state 
   for (i = 0; i < 8; i++){
      ROM_NO[i] = rom_backup[i];
   }
   onewire_dev.LastDiscrepancy = ld_backup;
   onewire_dev.LastDeviceFlag = ldf_backup;
   onewire_dev.LastFamilyDiscrepancy = lfd_backup;

   // return the result of the verify
   return result;
}

//--------------------------------------------------------------------------
// Setup the search to find the device type 'family_code' on the next call
// to OWNext() if it is present.
//
void onewire_targetsetup(unsigned char family_code)
{
   int i;

   // set the search state to find SearchFamily type devices
   ROM_NO[0] = family_code;
   for (i = 1; i < 8; i++){
   		ROM_NO[i] = 0;
   }
   onewire_dev.LastDiscrepancy = 64;
   onewire_dev.LastFamilyDiscrepancy = 0;
   onewire_dev.LastDeviceFlag = FALSE;
}

//--------------------------------------------------------------------------
// Setup the search to skip the current device type on the next call
// to OWNext().
//
void onewire_familyshipsetup()
{
   // set the Last discrepancy to last family discrepancy
    onewire_dev.LastDiscrepancy = onewire_dev.LastFamilyDiscrepancy;
    onewire_dev.LastFamilyDiscrepancy = 0;

   // check for end of list
   if (onewire_dev.LastDiscrepancy == 0){
       onewire_dev.LastDeviceFlag = TRUE;
   }
}


void read_command(){
	uint8_t i;
	onewire_writebyte(0x55);
	
	
//	onewire_writebyte(0x28);
//	onewire_writebyte(0x95);
//	onewire_writebyte(0x01);
//	onewire_writebyte(0x76);
//	onewire_writebyte(0x44);
//	onewire_writebyte(0xf5);
//	onewire_writebyte(0x00);
//	onewire_writebyte(0x00);
	
}


bool send_matchrom(uint8_t (*FoundROM)[8],int num)
{
    WWDG_SetCounter(0 );
	if(onewire_resetcheck() != 0){ 			//检查总线上从机是否存在
		return FALSE;
	}
//	num=Search_ROM(device_t.ROMID);
	onewire_writebyte(MATCH_ROM);// 匹配 ROM 指令
	for(int i = 0;i < 8;i++){
//		PRINT(" %2X", FoundROM[num][i]);
		onewire_writebyte(FoundROM[num][i]);
	}
	return TRUE;
}
float MY18B20_outputtotemp(int16_t out,int num)
{
	return ((float)out/16.0);	
}


void romid_crc(int num){
	if(device_t.ROMID[num][7] == onewire_crc8(&device_t.ROMID[num][0],7)){
	    device_t.type = MY18B20Z ;
		PRINT("\r\n");
		PRINT("this temp device serial number is:%d\r\n",num);
		PRINT("this device is:%d\r\n",  device_t.type);
	}
	else{
		if(device_t.ROMID[num][7] == 0 && (device_t.ROMID[num][6]) == 0){
		    device_t.type = MDC02;
//		    strcpy(&device_t.type,"mdc02");
			PRINT("\r\n");
			PRINT("this mdc02 device serial number is:%d\r\n",num);
			PRINT("this device is:%d\r\n", device_t.type);
		}
	}
}

//////////////******************读取温度*************///////////////////
int read_temp(int num){ 
	float fTemp=0; 
	uint16_t iTemp=0; 
	
	if(converttemp(num) == TRUE)
	{
    	ow_delay_us(15);
		read_tempwaiting(&iTemp,num);
		fTemp=MY18B20_outputtotemp((int16_t)iTemp,num);
		PRINT("Array_index= %d ,T= %3.3f \r\n",num, fTemp);
		PRINT("\r\n");
		modbus_ireg_update(MB_IREG_ADDR_MY18B20Z,fTemp);
	}
	else
	{
		PRINT("\r\n No MDC02");
	}
	ow_delay_us(990);
				
	return 1;
}

static int judge_status(float a,float b){
	if(a > 5.0 && b > 5.0){
		PRINT("进水了\r\n");
		return 1;
	}
	else{
		return 0;
	}
}
static int read_caps(int num)
{	
	float fcap1, fcap2, fcap3, fcap4; uint16_t iTemp, icap1, icap[1];
	uint8_t status, cfg;
	uint8_t status_wids=0;
	
	setcapchannel(CAP_CH1CH2_SEL,num);
    WWDG_SetCounter(0 );
	readstatusconfig((uint8_t *)&status, (uint8_t *)&cfg,num);
    WWDG_SetCounter(0 );
	
	if(convertcap(num) == FALSE)
	{
		PRINT("No MDC02\r\n");
	}
	else{
		WWDG_SetCounter(0 );
		ow_delay_us(15);
		readcapconfigure(&onewire_dev.CapCfg_offset, &onewire_dev.CapCfg_range,num);
        WWDG_SetCounter(0 );
//		PRINT("%f  %.5f\r\n",onewire_dev.CapCfg_offset,onewire_dev.CapCfg_range);
		readstatusconfig((uint8_t *)&status, (uint8_t *)&cfg,num);
        WWDG_SetCounter(0 );

		readtempcap1(&iTemp, &icap1,num);
        WWDG_SetCounter(0 );
		if(readcapc2c3c4(&icap[0],num) == FALSE){
			PRINT("read cap2 error");
		}
        WWDG_SetCounter(0 );
		fcap1 = mdc02_outputtocap(icap1, onewire_dev.CapCfg_offset, onewire_dev.CapCfg_range);
		fcap2 = mdc02_outputtocap(icap[0], onewire_dev.CapCfg_offset, onewire_dev.CapCfg_range);
        WWDG_SetCounter(0 );
		PRINT("Array_index= %d, C1=%4d , %6.3f pf  ,C2=%4d, %6.3f pf , S=%02X   C=%02X\r\n",num, icap1, fcap1, icap[0], fcap2, status, cfg);
//		if(fcap1 > 5.0 && fcap2 > 5.0){
//			PRINT("进水了\r\n");
//			return 1;
//		}
//		else{
//			return 0;
//		}
		status_wids=judge_status(fcap1,fcap2);
		PRINT("\r\n");
//		OLED_ShowNum(82, 16, status_wids, 1, 16, 1);
		OLED_ShowNum(82, 16, status_wids, 1, 16, 1);

		OLED_Refresh(0);
		modbus_di_update(MB_DI_ADDR_WIDS,status_wids);
		
		}

	ow_delay_us(990);
	return 1;		
}
////
static int read_tempc1(int num)
{
	uint16_t iTemp, iCap1; 
	float fTemp, fCap1;
		
	readcapconfigure(&onewire_dev.CapCfg_offset, &onewire_dev.CapCfg_range,num);
	setcapchannel(CAP_CH1_SEL,num);

	if(convert_tempcap1(num) == TRUE)
	{
		ow_delay_us(15);		
		if(readtempcap1(&iTemp, &iCap1,num) == TRUE) 
		{	
//			PRINT("%2x\r\n",iTemp);
			fTemp=mdc02_outputtotemp(iTemp);
			fCap1=mdc02_outputtocap(iCap1, onewire_dev.CapCfg_offset, onewire_dev.CapCfg_range);
            WWDG_SetCounter(0 );
			PRINT(" Array_index=%d ,T= %3.3f ℃ C1= %6.3f pF\r\n",num, fTemp, fCap1);
		}
	}
	else
	{
		PRINT("\r\n No MDC02");
	}
	ow_delay_us(990);
	return 1;		
}
////  * @brief  等待转换结束后读测量结果。和@ConvertTemp联合使用
////  * @param  iTemp：返回的16位温度测量结果
////  * @retval 读状态
////*/
bool read_tempwaiting(uint16_t *iTemp,int num)
{
	uint8_t scrb[sizeof(MDC02_SCRATCHPAD_READ)];
	MDC02_SCRATCHPAD_READ *scr = (MDC02_SCRATCHPAD_READ *) scrb;

	/*读9个字节。前两个是温度转换结果，最后字节是前8个的校验和--CRC。*/
	if(mdc02_readscratchpad_skiprom(scrb,num) == FALSE)
	{
		return FALSE;  /*读寄存器失败*/
	}

	/*计算接收的前8个字节的校验和，并与接收的第9个CRC字节比较。*/
  if(scrb[8] != onewire_crc8(&scrb[0], 8))
  {
		return FALSE;  /*CRC验证未通过*/
  }

	/*将温度测量结果的两个字节合成为16位字。*/
	*iTemp=(uint16_t)scr->T_msb<<8 | scr->T_lsb;

  return TRUE;
}




void dev_type_choose_function(){
	uint16_t dev_num;
	volatile int num_i;
	dev_num=Search_ROM(device_t.ROMID);
	for(num_i = 0;num_i < dev_num;num_i++){
		romid_crc(num_i);
		read_sampling_mode(MDC04_REPEATABILITY_HIGH,MDC04_MPS_1Hz,num_i);
		if(device_t.type == MDC02){
            WWDG_SetCounter(0 );
			mdc02_range(0, 30, num_i);
			read_tempc1(num_i);
            WWDG_SetCounter(0 );    
			read_caps(num_i);
            WWDG_SetCounter(0);
		}
		else{
            WWDG_SetCounter(0 );
			read_temp(num_i);
            WWDG_SetCounter(0 );
		}
	}
}



/*
  * @brief  设置偏置电容offset
*/
//int mdc02_offset(float Co)
//{
//	PRINT("Co= %5.2f\r\n", Co);
//	if(!((Co >=0.0) && (Co <= 103.5))) {
//		PRINT(" %s", "The input is out of range");
//		return 0;
//	}
//
//	mdc02_capconfigureoffset(Co,num);
//	return 1;
//}
/*
  * @brief  设置电容测量范围
  * 请勿设置超出电容量程0~119pf,请勿超出最大range:±15.5pf
*/
int mdc02_range(float Cmin,float Cmax,int num)
{ 
//		printf("\r\nCmin= %3.2f Cmax=%3.2f", Cmin, Cmax);
	if(!((Cmax <= 119.0) && (Cmax > Cmin) && (Cmin >= 0.0) && 
			((Cmax-Cmin) <= 31.0)))  
	{
		PRINT(" %s", "The input is out of range"); 
		return 0;
	}
		
	mdc02_capconfigurerange(Cmin, Cmax,num);
		
	readcapconfigure(&onewire_dev.CapCfg_offset, &onewire_dev.CapCfg_range,num);
//	PRINT("%.2f  %.5f\r\n",CapCfg_offset,CapCfg_range);
		
	return 1;
}

/*
  * @brief  设置配置寄存器
  * MPS:  000   001     010    011    100   101
  *      单次  0.5次/S 1次/S  2次/S  4次/S 10次/S
  * Repeatability:  00: 低重复性
  *                 01：中重复性
  *                 10：高重复性
*/
int read_sampling_mode(int repeatability,int mps,int num)
{ 
	int status, cfg;
	setconfig(mps & 0x07, repeatability & 0x03,num);
	readstatusconfig((uint8_t *)&status, (uint8_t *)&cfg,num);
//	PRINT("S=%02x C=%02x", status, cfg);
	
	return 0;
}

//int mdc02_channel(uint8_t channel)
//{
//	setcapchannel(channel,num);
//									
//	return 1;
//}

//-----------------------------------------

