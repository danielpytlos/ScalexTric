/*! @file board.c */

/* ################################################## Standard includes ################################################# */
#include <avr/io.h>
/* ################################################### Project includes ################################################# */
#include "board_spec.h"
#include "../include/board.h"
#include "../spi/spi.h"
/* ################################################### Global Variables ################################################# */
/* ############################################ Module Variables/Declarations ########################################### */
#define MOTOR_CONTROL_PWM_FREQ	25000L
#define MOTOR_CONTROL_PRESCALER	1L
#define MOTOR_CONTROL_TOP		(F_CPU/(MOTOR_CONTROL_PWM_FREQ * MOTOR_CONTROL_PRESCALER)-1L)

// MPU-9250 Gyro/Acc definitions
// Read bit or to register address to read from it
#define	MPU9520_READ				0x80

// MPU-9250 Registers
#define MPU9520_GYRO_CONFIG_REG		0x1B
#define MPU9520_ACCEL_CONFIG_REG	0x1C
#define MPU9520_ACCEL_XOUT_H_REG	0x3B
#define MPU9520_GYRO_XOUT_H_REG		0x43

#define GYRO_FULL_SCALE_250_DPS		0x00
#define GYRO_250_DPS_DIVIDER		131.0
#define GYRO_FULL_SCALE_500_DPS		0x08
#define GYRO_500_DPS_DIVIDER		65.5
#define GYRO_FULL_SCALE_1000_DPS	0x10
#define GYRO_1000_DPS_DIVIDER		32.8
#define GYRO_FULL_SCALE_2000_DPS	0x18
#define GYRO_2000_DPS_DIVIDER		16.4

#define ACC_FULL_SCALE_2_G			0x00
#define ACC_2G_DIVIDER				16384
#define ACC_FULL_SCALE_4_G			0x08
#define ACC_4G_DIVIDER				8192
#define ACC_FULL_SCALE_8_G			0x10
#define ACC_8G_DIVIDER				4096
#define ACC_FULL_SCALE_16_G			0x18
#define ACC_16G_DIVIDER				2048

typedef enum {_mpu9520_init_acc,_mpu9520_init_gyro, _mpu9520_poll_acc, _mpu9520_read_acc, _mpu9520_poll_gyro, _mpu9520_read_gyro} _mpu9520_states_t;

// Handle to SPI
static spi_p _spi_mpu9520 = 0;

static buffer_struct_t _mpu9520_rx_buffer;
static buffer_struct_t _mpu9250_tx_buffer;

static int16_t _x_acc = 0;
static int16_t _y_acc = 0;
static int16_t _z_acc = 0;

static int16_t _x_gyro = 0;
static int16_t _y_gyro = 0;
static int16_t _z_gyro = 0;
/* ################################################# Function prototypes ################################################ */
void _init_mpu9520();
static void _mpu9250_write_2_reg(uint8_t reg, uint8_t value);
static void _mpu9250_call_back(spi_p spi_instance, uint8_t spi_last_received_byte);

// ----------------------------------------------------------------------------------------------------------------------
void init_main_board() {
	// HORN
	*(&HORN_PORT_reg - 1) |= _BV(HORN_PIN_bit); // set pin to output

	// HEAD LIGHT
	*(&HEAD_LIGHT_PORT_reg - 1) |= _BV(HEAD_LIGHT_PIN_bit); // set pin to output

	// BRAKE LIGHT
	*(&BRAKE_LIGHT_PORT_reg - 1) |= _BV(BRAKE_LIGHT_PIN_bit); // set pin to output
	
	// MOTOR SPEED Fast-PWM ICR = TOP setup
	// Mode 14
	MOTOR_CONTROL_TCCRA_reg |= _BV(MOTOR_CONTROL_WGM1_bit);
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_WGM2_bit) | _BV(MOTOR_CONTROL_WGM3_bit);
	// Set OCA on compare match and clear on BOTTOM
	MOTOR_CONTROL_OCRA_reg = MOTOR_CONTROL_TOP;
	MOTOR_CONTROL_TCCRA_reg |= _BV(MOTOR_CONTROL_COMA1_bit) | _BV(MOTOR_CONTROL_COMA0_bit);
	*(&MOTOR_CONTROL_OCA_PORT_reg - 1) |= _BV(MOTOR_CONTROL_OCA_PIN_bit); // set pin to output
	// Set OCB on compare match and clear on BOTTOM
	MOTOR_CONTROL_OCRB_reg = MOTOR_CONTROL_TOP;
	MOTOR_CONTROL_TCCRA_reg |= _BV(MOTOR_CONTROL_COMB1_bit) | _BV(MOTOR_CONTROL_COMB0_bit);
	*(&MOTOR_CONTROL_OCB_PORT_reg - 1) |= _BV(MOTOR_CONTROL_OCB_PIN_bit); // set pin to output
	// PWM Freq set to 25 KHz
	// TOP = F_CPU/(Fpwm*N)-1
	MOTOR_CONTROL_ICR_reg = MOTOR_CONTROL_TOP;
	// Set prescaler and start timer
	#if (MOTOR_CONTROL_PRESCALER == 1)
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_CS0_bit);    // Prescaler 1 and Start Timer
	#elif ((MOTOR_CONTROL_PRESCALER == 8))
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_CS1_bit);    // Prescaler 8 and Start Timer
	#elif ((MOTOR_CONTROL_PRESCALER == 64))
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_CS0_bit) | _BV(MOTOR_CONTROL_CS1_bit);    // Prescaler 64 and Start Timer
	#elif ((MOTOR_CONTROL_PRESCALER == 256))
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_CS2_bit);    // Prescaler 256 and Start Timer
	#elif ((MOTOR_CONTROL_PRESCALER == 1024))
	MOTOR_CONTROL_TCCRB_reg |= _BV(MOTOR_CONTROL_CS0_bit) | _BV(MOTOR_CONTROL_CS2_bit); ;    // Prescaler 1024 and Start Timer
	#endif
	
	_init_mpu9520();
}

// ----------------------------------------------------------------------------------------------------------------------
void set_horn(uint8_t state){
	if (state) {
		HORN_PORT_reg |= _BV(HORN_PIN_bit);
	}
	else {
		HORN_PORT_reg &= ~_BV(HORN_PIN_bit);
	}
}

// ----------------------------------------------------------------------------------------------------------------------
void set_head_light(uint8_t state){
	if (state) {
		HEAD_LIGHT_PORT_reg |= _BV(HEAD_LIGHT_PIN_bit);
	}
	else {
		HEAD_LIGHT_PORT_reg &= ~_BV(HEAD_LIGHT_PIN_bit);
	}
}

// ----------------------------------------------------------------------------------------------------------------------
void set_brake_light(uint8_t state){
	if (state) {
		BRAKE_LIGHT_PORT_reg |= _BV(BRAKE_LIGHT_PIN_bit);
	}
	else {
		BRAKE_LIGHT_PORT_reg &= ~_BV(BRAKE_LIGHT_PIN_bit);
	}
}

// ----------------------------------------------------------------------------------------------------------------------
void set_motor_speed(int8_t speed_percent){
	if (speed_percent < -10) {
		speed_percent = -10;
	}
	else if (speed_percent > 100)
	{
		speed_percent = 100;
	}
	
	int16_t ocr;
	if (speed_percent < 0) {
		MOTOR_CONTROL_OCRA_reg = MOTOR_CONTROL_TOP;
		ocr = MOTOR_CONTROL_OCRB_reg = (100-(-speed_percent))*MOTOR_CONTROL_TOP/100;
	}
	else {
		MOTOR_CONTROL_OCRB_reg = MOTOR_CONTROL_TOP;
		ocr = MOTOR_CONTROL_OCRA_reg = (100-speed_percent) * MOTOR_CONTROL_TOP/100;
	}
}

// ----------------------------------------------------------------------------------------------------------------------
float get_x_accel() {
	return ((float)_x_acc)/ACC_2G_DIVIDER;
}

// ----------------------------------------------------------------------------------------------------------------------
float get_y_accel() {
	return ((float)_y_acc)/ACC_2G_DIVIDER;
}

// ----------------------------------------------------------------------------------------------------------------------
float get_z_accel() {
	return ((float)_z_acc)/ACC_2G_DIVIDER;
}

// ----------------------------------------------------------------------------------------------------------------------
float get_x_rotation() {
	return ((float)_x_gyro)/GYRO_500_DPS_DIVIDER;
}

// ----------------------------------------------------------------------------------------------------------------------
float get_y_rotation() {
	return ((float)_y_gyro)/GYRO_500_DPS_DIVIDER;
}

// ----------------------------------------------------------------------------------------------------------------------
float get_z_rotation() {
	return ((float)_z_gyro)/GYRO_500_DPS_DIVIDER;
}
// ----------------------------------------------------------------------------------------------------------------------
void _init_mpu9520() {
	_spi_mpu9520 = spi_new_instance(SPI_MODE_MASTER, SPI_CLOCK_DIVIDER_32, 3, SPI_DATA_ORDER_MSB, &PORTB, PB0, 0,	&_mpu9520_rx_buffer, &_mpu9250_tx_buffer, &_mpu9250_call_back);
	_mpu9250_call_back(0,0);
}

// ----------------------------------------------------------------------------------------------------------------------
static void _mpu9250_write_2_reg(uint8_t reg, uint8_t value) {
	uint8_t buf[2];
	buf[0] = reg;
	buf[1] = value;

	spi_send_string(_spi_mpu9520, buf , 2);
}

// ----------------------------------------------------------------------------------------------------------------------
static void _mpu9250_read_reg(uint8_t reg, uint8_t no_of_bytes)
{
	uint8_t send[no_of_bytes+1];
	send[0] = MPU9520_READ | reg;
	for (uint8_t i = 1; i <= no_of_bytes; i++)
	{
		send[i] = 0;
	}
	
	buffer_clear(&_mpu9520_rx_buffer);
	spi_send_string(_spi_mpu9520, send , no_of_bytes+1);
}

// ----------------------------------------------------------------------------------------------------------------------
static void _poll_acc() {
	_mpu9250_read_reg(MPU9520_ACCEL_XOUT_H_REG ,6);
}

// ----------------------------------------------------------------------------------------------------------------------
static void _poll_gyro() {
	_mpu9250_read_reg(MPU9520_GYRO_XOUT_H_REG ,6);
}
// ----------------------------------------------------------------------------------------------------------------------
static void _mpu9250_call_back(spi_p spi_instance, uint8_t spi_last_received_byte)
{
	uint8_t lsb, msb;
	static _mpu9520_states_t state = _mpu9520_init_acc;

	switch (state)
	{
		case _mpu9520_init_acc:
		{
			// Setup Acc
			state = _mpu9520_init_gyro;
			_mpu9250_write_2_reg(MPU9520_ACCEL_CONFIG_REG, ACC_FULL_SCALE_2_G);
			break;
		}
		
		case _mpu9520_init_gyro:
		{
			if (buffer_no_of_items(&_mpu9520_rx_buffer) == 2) {
				// Setup Gyro
				state = _mpu9520_poll_acc;
				buffer_clear(&_mpu9520_rx_buffer);
				_mpu9250_write_2_reg(MPU9520_GYRO_CONFIG_REG, GYRO_FULL_SCALE_500_DPS);
			}
			break;
		}

		case _mpu9520_poll_acc:
		{
			if (buffer_no_of_items(&_mpu9520_rx_buffer) == 2) {
				state = _mpu9520_read_acc;
				buffer_clear(&_mpu9520_rx_buffer);
				_poll_acc();
			}
			break;
		}

		case _mpu9520_read_acc:
		{
			if (buffer_no_of_items(&_mpu9520_rx_buffer) == 7) {
				buffer_get_item(&_mpu9520_rx_buffer, &lsb); // Throw away the command response
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_x_acc = (msb << 8) | lsb;
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_y_acc = (msb << 8) | lsb;
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_z_acc = (msb << 8) | lsb;
				
				state = _mpu9520_read_gyro;
				_poll_gyro();
			}
			break;
		}

		case _mpu9520_read_gyro:
		{
			if (buffer_no_of_items(&_mpu9520_rx_buffer) == 7) {
				buffer_get_item(&_mpu9520_rx_buffer, &lsb); // Throw away the command response
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_x_gyro = (msb << 8) | lsb;
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_y_gyro = (msb << 8) | lsb;
				buffer_get_item(&_mpu9520_rx_buffer, &msb);
				buffer_get_item(&_mpu9520_rx_buffer, &lsb);
				_z_gyro = (msb << 8) | lsb;

				state = _mpu9520_read_acc;
				_poll_acc();
			}
			break;
		}

		default:
		break;
	}
}