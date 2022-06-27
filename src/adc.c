#include "oled.h"
#include "stdlib.h"
#include "CH58x_common.h"
#include "adc.h"
#include <math.h>
#include <stdio.h>
#include "appinfo.h"
#include "iregs.h"
void adc_init(){
	GPIOA_ModeCfg(GPIO_Pin_7, GPIO_ModeIN_PD);
	ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);
}


static float firstorderfilter(float a,float newValue, float oldValue)
{
//	return  (1/a) * newValue + ((a-1)/a) * oldValue;
	
    return  a*newValue + (1-a)*oldValue;


}

static float adc_capture(float *adc_collect_buff){
	volatile uint8_t i;
	float sum=0.0;
	float adc_varge;
	float collect_buff[40];
	signed short RoughCalib_Value = 0; 
	RoughCalib_Value = ADC_DataCalib_Rough(); // 用于计算ADC内部偏差，记录到全局变量 RoughCalib_Value中
	PRINT("RoughCalib_Value =%d \r\n", RoughCalib_Value);
	ADC_ChannelCfg(11);
 	for (i=0;i<20;i++){
		collect_buff[i] = ADC_ExcutSingleConver() + RoughCalib_Value; // 连续采样20次
		PRINT("collected voltage value:%f \r\n", collect_buff[i]);
		
//		*(adc_collect_buff+i)=collect_buff[i];
		sum+=collect_buff[i];

 	}
	adc_varge=sum/20;
	PRINT("adc_varge=%f\r\n",adc_varge);
	*adc_collect_buff=adc_varge;
	
	return 0;
	
}
array array_t;
void adc_convert_caclute(){
	float v_filter[20]={0.0};
	float adc_relly[20];
	float depth_L=0	;
	float EC[20];
	adc_capture(&array_t.adc_collect_buff);

	for(volatile int i=0;i<20;i++){
	    WWDG_SetCounter(0 );
		v_filter[i]=firstorderfilter(0.8,array_t.adc_collect_buff,v_filter[i-1]);
		PRINT("v_filter=%f\r\n",v_filter[i]);
		WWDG_SetCounter(0 );
		
		adc_relly[i]=(v_filter[i]/2048)*Vref;
		PRINT("convert voltage value:%.4f \r\n", adc_relly[i]);
		WWDG_SetCounter(0 );

		EC[i]=((adc_relly[i]/2)/50)*1000;//将采集到的电压转化为电流：‘50’ 为采样电阻，‘2’为带反馈的运算放大器的放大倍数
		PRINT("electron current value: %f\r\n",EC[i]);
		WWDG_SetCounter(0 );
		
		depth_L=((EC[i]-4)*FULL_RANGE)/16;//转化为水的深度
		PRINT("water level: %f\r\n",depth_L);
		WWDG_SetCounter(0 );
		
		sprintf(array_t.depth_L_S,"%.3f",depth_L);
		OLED_ShowString(54,32,array_t.depth_L_S,16,1);
		OLED_Refresh(0);
		modbus_ireg_update(MB_IREG_ADDR_WATER_LEVEL,depth_L*1000);
//		DelayMs(500);

	}////还可以在优化一下，


    
    
}


