################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/OW/onewire.c 

OBJS += \
./src/OW/onewire.o 

C_DEPS += \
./src/OW/onewire.d 


# Each subdirectory must supply rules for building sources it contributes
src/OW/%.o: ../src/OW/%.c
	@	@	riscv-none-embed-gcc -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections  -g -DDEBUG=1 -I"../StdPeriphDriver/inc" -I"../src/include/utils" -I"../src/include/modbus" -I"../src/include" -I"../src/liblightmodbus-3.0/include" -I"../RVMSIS" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@	@

