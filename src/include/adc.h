

#ifndef HEAD_ADC_H_
#define HEAD_ADC_H_

#include  "CH58x_common.h"
#define FULL_RANGE  5
#define Vref    1.05
 typedef struct{
	float adc_collect_buff;
	char depth_L_S[20];
}array;


void adc_init();
void adc_function();
void adc_convert_caclute();


#endif /* HEAD_ADC_H_ */




