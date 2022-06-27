#include "stdio.h"
#include "CH58x_common.h"
#include "worktime.h"
#include "configtool.h"
#include "oled.h"
#include "bmp.h"
#include "display.h"
#include "version.h"
#include "modbus.h"
#include "appinfo.h"
#include "uid.h"
#include "sensor.h"
#include "onewire.h"
#include "adc.h"

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */

int uuid_dump(){
	uint8_t uuid[10] = {0};
	int i = 0;
	GET_UNIQUE_ID(uuid);
	PRINT("deviceid:");
	for (i = 0; i < 8; i++){
		PRINT("%02x ", uuid[i]);
	}
	PRINT("\r\n");
	return 0;
}
int reset_dump(){
	SYS_ResetStaTypeDef rst = SYS_GetLastResetSta();
	PRINT("rst(");
	switch(rst){
		case RST_STATUS_SW:
			PRINT("sw reset");
			break;
		case RST_STATUS_RPOR:
			PRINT("poweron");
			break;
		case RST_STATUS_WTR:
			PRINT("wdt");
			break;
		case RST_STATUS_MR:
			PRINT("manual reset");
			break;
		case RST_STATUS_LRM0:
			PRINT("software wakeup");
			break;
		case RST_STATUS_GPWSM:
			PRINT("shutdown wakeup");
			break;
		case RST_STATUS_LRM1:
			PRINT("wdt wakeup");
			break;
		case RST_STATUS_LRM2:
			PRINT("manual wakeup");
			break;
	}
	PRINT("\r\n");
	return 0;
}

int main()
{
    uint32_t appversion;
	worktime_t worktime = 0;
	char buf[DISPLAY_LINE_LEN + 1];
    SetSysClock(CLK_SOURCE_PLL_60MHz);
	worktime_init();
	WWDG_ResetCfg(ENABLE);
	
    /* 配置串口1：先配置IO口模式，再配置串口 */
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);      // RXD-配置上拉输入
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA); // TXD-配置推挽输出，注意先让IO口输出高电平
    UART1_DefInit();
	
	//rs485en3 485方向控制     默认为接收
	GPIOB_ModeCfg(GPIO_Pin_18,GPIO_ModeOut_PP_5mA);
	GPIOB_SetBits(GPIO_Pin_18);

    WWDG_SetCounter(0 );
	reset_dump();
	PRINT("app start ...\r\n");
	uuid_dump();

    WWDG_SetCounter(0 );
	OLED_Init();
    OLED_Clear();
//	OLED_DisPlay_On();
//	OLED_ShowPicture(32, 0, 64, 64, (uint8_t *)smail_64x64_1, 1);
	oled_seneor_flag();
	OLED_Refresh(0);

    WWDG_SetCounter(0 );
	onewire_init();	
	adc_init();
    WWDG_SetCounter(0 );
	display_init();
	cfg_init();
    WWDG_SetCounter(0 );
    
	sensor_init();
    WWDG_SetCounter(0 );
	onewire_find_alldevice();
    WWDG_SetCounter(0 );
//	while(worktime_since(worktime) < 1000){
//		__nop();
//	}
	//DISPLAY_PRINT("Boot:%s", CURRENT_VERSION_STR());
//	if (upgrade_app_available()){
//		appversion = upgrade_app_version();
//		version_str(appversion, buf, DISPLAY_LINE_LEN);
//		DISPLAY_PRINT("APP:%s", buf);
//	}else{
//		DISPLAY_PRINT("APP:none");
//	}
	PRINT("main loop start ...\r\n");
    while(1){
        WWDG_SetCounter(0);
		if (worktime_since(worktime) >= 1000){
			worktime = worktime_get();
            dev_type_choose_function();
            WWDG_SetCounter(0);
            adc_convert_caclute();
            WWDG_SetCounter(0 );
//            OLED_Refresh(0);

//            OLED_Init();
//            OLED_Refresh(1);
//            OLED_Refresh(0);
	    }
		sensor_run();
	}
}


