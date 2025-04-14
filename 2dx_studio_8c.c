/*  Time of Flight for 2DX4 -- Studio W8-0
                Code written to support data collection from VL53L1X using the Ultra Light Driver.
                I2C methods written based upon MSP432E4 Reference Manual Chapter 19.
                Specific implementation was based upon format specified in VL53L1X.pdf pg19-21
                Code organized according to en.STSW-IMG009\Example\Src\main.c
                
                The VL53L1X is run with default firmware settings.


            Written by Tom Doyle
            Updated by  Hafez Mousavi Garmaroudi
            Last Update: March 17, 2020
						
						Last Update: March 03, 2022
						Updated by Hafez Mousavi
						__ the dev address can now be written in its original format. 
								Note: the functions  beginTxI2C and  beginRxI2C are modified in vl53l1_platform_2dx4.c file
								
						Modified March 16, 2023 
						by T. Doyle
							- minor modifications made to make compatible with new Keil IDE

*/
#include <stdint.h>
#include "PLL.h"
#include "SysTick.h"
#include "uart.h"
#include "onboardLEDs.h"
#include "tm4c1294ncpdt.h"
#include "VL53L1X_api.h"





#define I2C_MCS_ACK             0x00000008  // Data Acknowledge Enable
#define I2C_MCS_DATACK          0x00000008  // Acknowledge Data
#define I2C_MCS_ADRACK          0x00000004  // Acknowledge Address
#define I2C_MCS_STOP            0x00000004  // Generate STOP
#define I2C_MCS_START           0x00000002  // Generate START
#define I2C_MCS_ERROR           0x00000002  // Error
#define I2C_MCS_RUN             0x00000001  // I2C Master Enable
#define I2C_MCS_BUSY            0x00000001  // I2C Busy
#define I2C_MCR_MFE             0x00000010  // I2C Master Function Enable

#define MAXRETRIES              5           // number of receive attempts before giving up

volatile uint32_t state = 0; 
volatile uint32_t count = 0;

typedef enum { 
	IDLE, 
	RUNNING, 
} MotorState;

MotorState motorState = IDLE;

void PortJ_Init(void){
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R8;				// activate clock for Port M
	while((SYSCTL_PRGPIO_R&SYSCTL_PRGPIO_R8) == 0){};	// allow time for clock to stabilize
	GPIO_PORTJ_DIR_R &= ~0x03;        								// configure Port M pins (PM0-PM3) as output
  GPIO_PORTJ_AFSEL_R &= ~0x03;     								// disable alt funct on Port M pins (PM0-PM3)
  GPIO_PORTJ_DEN_R |= 0x03;        								// enable digital I/O on Port M pins (PM0-PM3)
																									// configure Port M as GPIO
  GPIO_PORTJ_AMSEL_R &= ~0x03;     								// disable analog functionality on Port M	pins (PM0-PM3)	
	GPIO_PORTJ_PUR_R = 0x03; 
	return;
}
void PortH_Init(void) 
{
    SYSCTL_RCGCGPIO_R |= 0x80; 								//enable clock PortH (bit 7), 0x80 = 128, ex: 0000 |= 1000 => 1000
    while((SYSCTL_PRGPIO_R & 0x80) == 0){}; 	//wait until PortH is ready
    GPIO_PORTH_DIR_R |= 0x0F; 								//set pins 0-3 as output, 0x0F = 15
    GPIO_PORTH_DEN_R |= 0x0F; 								//enable digital function for pins 0-3
    GPIO_PORTH_AFSEL_R &= ~0x0F; 							//disable alternate function for pins 0-3, ex: 0001 & 1110 = 0000 (bit 0 cleared)
    GPIO_PORTH_AMSEL_R &= ~0x0F; 							//disable analog function for pins 0-3
}

void motorSpin(int direction) 
{
		uint32_t delay = 1;
	
		GPIO_PORTH_DATA_R = 0b00000011;
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00000110;
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00001100;
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00001001;
		SysTick_Wait10ms(delay);
		count++;
	
}

int checkButtons() {
    int J0 = GPIO_PORTJ_DATA_R & 0x01;      // Read J0
    int J1 = (GPIO_PORTJ_DATA_R & 0x02); // Read J1 

    // Toggle motor state when J0 is pressed (ignoring other buttons until action complete)
    if (!J0 && J1) {
        state = !state;
        //GPIO_PORTN_DATA_R ^= 0x02; // Toggle LED 0 (D1)
        while (!(GPIO_PORTJ_DATA_R & 0x01)); // debounce
				return 1;
    }

    // Toggle direction when J1 is pressed (ignoring other buttons until action complete)
    if (!J1 && J0) {
				GPIO_PORTN_DATA_R ^= 0x01; // Toggle LED 1 (D2)
        while (!(GPIO_PORTJ_DATA_R & 0x02)); // debounce
				return 0;
    }
		return 2;
}
void updateMotorState() {
    switch (motorState) {
        case IDLE:
            if (state) motorState = RUNNING;
            break;

        case RUNNING:
            if (!state) {
							motorState = IDLE;
							//GPIO_PORTN_DATA_R &= ~0x02;  //toggle LED0 (PN1)     0011 & 1101 = 0001
							//GPIO_PORTN_DATA_R &= ~0x01; //turn OFF PN0 (Direction LED)
						}
						else {
							if (state) {
									motorSpin(1);
									//GPIO_PORTN_DATA_R |= 0x02;  //toggle LED0 (PN1)
							}
						}
            break;
    }
}
	
void I2C_Init(void){
  SYSCTL_RCGCI2C_R |= SYSCTL_RCGCI2C_R0;           													// activate I2C0
  SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R1;          												// activate port B
  while((SYSCTL_PRGPIO_R&0x0002) == 0){};																		// ready?

    GPIO_PORTB_AFSEL_R |= 0x0C;           																	// 3) enable alt funct on PB2,3       0b00001100
    GPIO_PORTB_ODR_R |= 0x08;             																	// 4) enable open drain on PB3 only

    GPIO_PORTB_DEN_R |= 0x0C;             																	// 5) enable digital I/O on PB2,3
//    GPIO_PORTB_AMSEL_R &= ~0x0C;          																// 7) disable analog functionality on PB2,3

                                                                            // 6) configure PB2,3 as I2C
//  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R&0xFFFF00FF)+0x00003300;
  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R&0xFFFF00FF)+0x00002200;    //TED
    I2C0_MCR_R = I2C_MCR_MFE;                      													// 9) master function enable
    I2C0_MTPR_R = 0b0000000000000101000000000111011;                       	// 8) configure for 100 kbps clock (added 8 clocks of glitch suppression ~50ns)
//    I2C0_MTPR_R = 0x3B;                                        						// 8) configure for 100 kbps clock
        
}

//The VL53L1X needs to be reset using XSHUT.  We will use PG0
void PortG_Init(void){
    //Use PortG0
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R6;                // activate clock for Port N
    while((SYSCTL_PRGPIO_R&SYSCTL_PRGPIO_R6) == 0){};    // allow time for clock to stabilize
    GPIO_PORTG_DIR_R &= 0x00;                                        // make PG0 in (HiZ)
  GPIO_PORTG_AFSEL_R &= ~0x01;                                     // disable alt funct on PG0
  GPIO_PORTG_DEN_R |= 0x01;                                        // enable digital I/O on PG0
                                                                                                    // configure PG0 as GPIO
  //GPIO_PORTN_PCTL_R = (GPIO_PORTN_PCTL_R&0xFFFFFF00)+0x00000000;
  GPIO_PORTG_AMSEL_R &= ~0x01;                                     // disable analog functionality on PN0

    return;
}

//XSHUT     This pin is an active-low shutdown input; 
//					the board pulls it up to VDD to enable the sensor by default. 
//					Driving this pin low puts the sensor into hardware standby. This input is not level-shifted.
void VL53L1X_XSHUT(void){
    GPIO_PORTG_DIR_R |= 0x01;                                        // make PG0 out
    GPIO_PORTG_DATA_R &= 0b11111110;                                 //PG0 = 0
    FlashAllLEDs();
    SysTick_Wait10ms(10);
    GPIO_PORTG_DIR_R &= ~0x01;                                            // make PG0 input (HiZ)
    
}


//*********************************************************************************************************
//*********************************************************************************************************
//***********					MAIN Function				*****************************************************************
//*********************************************************************************************************
//*********************************************************************************************************
uint16_t	dev = 0x29;			//address of the ToF sensor as an I2C slave peripheral
int status=0;

int main(void) {
  uint8_t byteData, sensorState = 0, myByteArray[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, i = 0;
  uint16_t wordData;
  uint16_t Distance;
  uint8_t dataReady;
  uint8_t RdByte;
  uint16_t RdWord;

  // initialize
  PLL_Init();
  PortH_Init();
	PortJ_Init();
  SysTick_Init();
  onboardLEDs_Init();
  I2C_Init();
  UART_Init();
  
  // hello world!
  UART_printf("Program Begins\r\n");
  int mynumber = 1;
  sprintf(printf_buffer, "2DX ToF Program Studio Code %d\r\n", mynumber);
  UART_printf(printf_buffer);

  // Basic I2C read functions to check I2C functionality
  status = VL53L1X_GetSensorId(dev, &wordData);
  sprintf(printf_buffer, "(Model_ID, Module_Type)=0x%x\r\n", wordData);
  UART_printf(printf_buffer);

  // Wait for ToF device to boot
  while (sensorState == 0) {
    status = VL53L1X_BootState(dev, &sensorState);
    SysTick_Wait10ms(10);
  }
  FlashAllLEDs();
  UART_printf("ToF Chip Booted!\r\n Please Wait...\r\n");

  status = VL53L1X_ClearInterrupt(dev); /* clear interrupt */
  status = VL53L1X_SensorInit(dev);      /* Initialize the sensor */
  Status_Check("SensorInit", status);
	/*while(1) {
		GPIO_PORTN_DATA_R ^= 0b00000010; 								//hello world!
			SysTick_Wait10ms(100);														//.05s delay
			GPIO_PORTN_DATA_R ^= 0b00000010;			
			SysTick_Wait10ms(100);														//.05s delay
	}*/
	int resetFlag = 0;
	while (1) {
		// Start ranging
		status = VL53L1X_StartRanging(dev);
		
		while(count < 512 ) {
			checkButtons();
			updateMotorState();
			if ( (state && count%4 == 0) || (!state && !checkButtons()) ) {
				while (dataReady == 0) {
					status = VL53L1X_CheckForDataReady(dev, &dataReady);
					VL53L1_WaitMs(dev, 5);
				}
				dataReady = 0;

				// Read the distance data from the ToF sensor
				status = VL53L1X_GetDistance(dev, &Distance);
				FlashLED2(1);
				// Send the distance reading over UART
				double angle = (double)count*0.703125;
				sprintf(printf_buffer, "Distance: %u mm after %f degrees\r\n", Distance, angle);
				FlashLED1(1);
				UART_printf(printf_buffer);
				
				// Clear interrupt to enable next data read
				status = VL53L1X_ClearInterrupt(dev);

				// Delay to simulate sensor settling before the next measurement
				//SysTick_Wait10ms(500);  // Wait for 500 ms before moving to the next position
			}
		}
				sprintf(printf_buffer, "360 degrees completed!\n");
				UART_printf(printf_buffer);
		// Stop ranging when done
		VL53L1X_StopRanging(dev);
		while(1) {
			if (!(GPIO_PORTJ_DATA_R & 0x02)) {
				sprintf(printf_buffer, "Home time!\n");
				UART_printf(printf_buffer);
				FlashLED3(1);
				while (!(GPIO_PORTJ_DATA_R & 0x02));
				while(count > 0) {
						GPIO_PORTH_DATA_R = 0b00001001;
						SysTick_Wait10ms(1);
						GPIO_PORTH_DATA_R = 0b00001100;
						SysTick_Wait10ms(1);
						GPIO_PORTH_DATA_R = 0b00000110;
						SysTick_Wait10ms(1);
						GPIO_PORTH_DATA_R = 0b00000011;
						SysTick_Wait10ms(1);
						count--;
				}
				state = 0;
				break;
			}
		}
	}
}