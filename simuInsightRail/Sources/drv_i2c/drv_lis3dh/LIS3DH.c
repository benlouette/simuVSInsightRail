#ifdef __cplusplus
extern "C" {
#endif

/*
 * LIS3DH.c
 *
 *  Created on: 19 Jan 2016
 *      Author: Rex Taylor BF1418 (Livingston)
 */

#include <stdint.h>
#include <stdbool.h>

#include "math.h"

#include "i2c.h"

#include "Device.h"

#include "PowerControl.h"
#include "PinConfig.h"
#include "PinDefs.h"

#include "LIS3DH.h"
#include "resources.h"

// private function prototypes

static bool readLis3dhRegister(uint8_t reg, uint8_t* regVal);
static bool writeLis3dhRegister(uint8_t reg, uint8_t value);
//static void resetRegisters();
static bool whoAmI();
static bool DisableMemsInterrupts();
static bool EnableMemsInterrupts();
static bool ConfigMemsForSelfTestMode(uint8_t outDataRate, uint8_t selftestBits, float *pResolution_2g);
static bool GetOpModeResolutionInG(uint8_t *pOpMode, float *pResolution_2g);
static uint8_t GetOutputDataRate(uint16_t sampleFreq_Hz);
static void LogMemsSamplingError();


/***********************************************************************************************************************/
// Register definitions for the LIS3DH MEMs sensor
// Please note:- data sheet STATES that writing to RESERVED addresses may damage the device on a permanent basis
//
/***********************************************************************************************************************/
// Registers 0x00 - 0x06 Reserved do not modify
#define STATUS_REG_AUX		0x07
#define OUT_ADC1_L			0x08
#define OUT_ADC1_H			0x09
#define OUT_ADC2_L			0x0A
#define OUT_ADC2_H			0x0B
#define OUT_ADC3_L			0x0C
#define OUT_ADC3_H			0x0D
#define INT_COUNTER_REG		0x0E
#define WHO_AM_I			0x0F
// Addr 0x10 - 0x1E	Reserved do not modify
#define TEMP_CFG_REG		0x1F
#define CTRL_REG1			0x20
#define CTRL_REG2			0x21
#define CTRL_REG3			0x22
#define CTRL_REG4			0x23
#define CTRL_REG5			0x24
#define CTRL_REG6			0x25
#define REFERENCE			0x26
#define STATUS_REG2			0x27
#define OUT_X_L				0x28
#define OUT_X_H				0x29
#define OUT_Y_L				0x2A
#define OUT_Y_H				0x2B
#define OUT_Z_L				0x2C
#define OUT_Z_H				0x2D
#define FIFO_CTRL_REG		0x2E
#define FIFO_SRC_REG		0x2F
#define INT1_CFG			0x30
#define INT1_SOURCE			0x31
#define INT1_THS			0x32
#define INT1_DURATION		0x33
// Addr 0x34 to 0x37 reserved
#define CLICK_CFG			0x38
#define CLICK_SRC			0x39
#define CLICK_THS			0x3A
#define TIME_LIMIT			0x3B
#define TIME_LATENCY		0x3C
#define TIME_WINDOW			0x3D

#define LIS3DH_AUTOINCREMENT 0x80
// control reg bit masks.
#define DISABLE_INTERRUPTS					(0x00)
#define ENABLE_XYZ_HIGH_INTERRUPTS			(0x2A)
#define EN_XYZ_AXIS_DETECTION				(0x07)
#define SHIFT_TO_UPPER_NIBBLE				(0x04)
#define SHIFT_TO_LOWER_NIBBLE				(0x04)
#define EN_HP_FILTER_NORMAL_MODE			(0x80)
#define EN_ZYXDA_INT_ON_PIN_INT1			(0x10)
#define LPEN_BIT_MASK						(0x08)	// Lpen, bit 3 of CTRL_REG1
#define ST_BIT_POSITION						(0x01)	// ST0-1, bit1-2 of CTRL_REG4
#define EN_HIGH_RES_OUTPUT_MODE_BIT_MASK	(0x08)	// HR, bit3 of CTRL_REG4
#define FS_SELECTION_BIT_MASKS				(0x30)	// FS0-1, bit4-5 of CTRL_REG4
// Delay in samples
#define HIGH_RES_OUT_MODE_DELAY_SAMPLES		(0x08)
#define DELAY_SAMPLES_OTHER_MODES			(0x02)


// globals

typedef enum MemsOpModes
{
	eLOW_POWER_MODE 		= 0,
	eNORMAL_MODE 			= 1,
	eHIGH_RESOLUTION_MODE 	= 2,
}MEMS_OP_MODES;

// locals
static struct {
    bool init_done;// used in the irq handling, to prevent acting on spurrious interrupts, when not ready for them
    SemaphoreHandle_t mutex;// mutex preventing access to the lis3d when a measurement function is started, until it is finished.
    SemaphoreHandle_t semIrq;// semaphore is given in the ISR of the lis3d,
    uint32_t irq_counter;// just for debugging purposes, to see we got interrupts
} lis3d_data = {
        false,
        NULL,
        NULL,
        0
};

// i2c setup data address and clockrate
static i2c_device_t lis3dh =
{
    .address = 0x18,
    .baudRate_kbps = 400,
};

// see table 31 'Data rate configuration' in datasheet rev2
// only considering the frequencies upto 400Hz
static uint16_t odr_to_samplefreq[8] = {
        0,// powerdown
        1,
        10,
        25,
        50,
        100,
        200,
        400
};
// just lazy, also the sampletime in milliseconds (used for delay's)
static uint16_t odr_to_sampletimeMs[8] = {
        0,// powerdown
        1000,
        100,
        40,
        20,
        10,
        5,
        3 // 1/400 = 2.5, but rounded to 3
};
/*
 * read all accelerometer registers in one go
 */
static bool readLis3dhAccRegisters( tLis3dAcc* accVals_p)
{
        bool retval = true;
        // read the registers

        retval = I2C_ReadRegister(I2C0_IDX, lis3dh, LIS3DH_AUTOINCREMENT | OUT_X_L, sizeof (*accVals_p), (uint8_t *) accVals_p ); // bit 7 of address enables autoincrement, see datasheet

        //printf("retval = %d x= %04x, y= %04x, y= %04x\n",retval, accVals_p->x, accVals_p->y, accVals_p->z);
        return retval;
}

//
// most control registers can be written in one go, make an easy structure for that.
//

typedef struct  {
    uint8_t temp;
    uint8_t ctrl1;
    uint8_t ctrl2;
    uint8_t ctrl3;
    uint8_t ctrl4;
    uint8_t ctrl5;
    uint8_t ctrl6;
    uint8_t ref;
} tLis3dreg;

static const tLis3dreg lis3d_defaultcfg = {
        .temp = 0,// no temperature, no adc
        .ctrl1 = 0x07, // power down mode, enable x,y,z
        .ctrl2 = 0x00,
        .ctrl3 = 0x00,
        .ctrl4 = 0x00,
        .ctrl5 = 0x00,
        .ctrl6 = 0x00,
        .ref = 0 // not setting a reference acc
};

/*
 * write all 8 control registers in one go, starting at TEMP_CFG_REG
 */

static bool writeLis3dhCfgRegisters( const tLis3dreg* cfgVals_p)
{
        bool retval ;

        retval = I2C_WriteRegisters(I2C0_IDX, lis3dh, LIS3DH_AUTOINCREMENT | TEMP_CFG_REG, sizeof(*cfgVals_p), (uint8_t *) cfgVals_p ); // bit 7 of address enables autoincrement, see datasheet

        return retval;
}

/*
 * create resources, and enable interrupts etc. to operate the mems chip lis3dh
 */
static bool lis3dh_init()
{
    bool rc_ok = true;

    if(!Device_HasPMIC())
    {
		if (lis3d_data.init_done == false)
		{
			// create the semaphores.
			if (lis3d_data.mutex == NULL)
			{
				lis3d_data.mutex = xSemaphoreCreateMutex();// prevent more than one task operating the lis3d
			}
			if (lis3d_data.semIrq == NULL)
			{
				lis3d_data.semIrq = xSemaphoreCreateBinary();// used in the isr, to signal the waiting task that the lis3d has issued an interrupt
			}

			// we need both !
			if (lis3d_data.mutex ==NULL || lis3d_data.semIrq == NULL)
			{
				rc_ok = false;
				LOG_DBG(LOG_LEVEL_I2C, "%s() failed\n", __func__);
			}
			else
			{
				// semaphores are present, how about the i/o config ?
				// setup the interrupt on ptd10, default lis3d config irq1 goes low to high on irq
				// Input pins:     Pin name Mux mode        Pull  Pull direction  Interrupt config
				pinConfigDigitalIn(ACCEL_INT1n,    kPortMuxAsGpio, true, kPortPullUp,  kPortIntRisingEdge); // todo: move this to a configLIS3D.[ch]
				// priority for portD is in resources.c

				lis3d_data.init_done = true; // clear to go
			}
		}
    }

    return rc_ok;
}

// make sure interrupts are gone
static void lis3dh_terminate()
{
    lis3d_data.init_done = false;
    pinConfigDigitalIn(ACCEL_INT1n,    kPortMuxAsGpio, true, kPortPullUp,  kPortIntDisabled); // todo: move this to a configLIS3D.[ch]
}


/*
 * LIS3DH_ISR
 *
 * @desc    Called in interrupt context (from event.c when using processor expert) when the lis3d generated an interrupt
 *
 * @param  -
 * @return -
 */
void LIS3DH_ISR( void )
{
    BaseType_t HigherPriorityTaskWoken = pdFALSE;

    lis3d_data.irq_counter++;// just for debugging

    // notify waiting task that
    if (lis3d_data.init_done) {

        if (pdTRUE != xSemaphoreGiveFromISR(lis3d_data.semIrq , &HigherPriorityTaskWoken)) {
            // oops, give semaphore went wrong
        }
        if (HigherPriorityTaskWoken == pdTRUE)  {
            vPortYieldFromISR();
        }
    }
}

//
// collect the specified number if samples (3d data 16 bit -> 6 bytes per sample) into a buffer (when pointer not NULL, or exit on error
// also optional calculate the mean and standard deviation (probably a better way to detect if the sensor is moving or stationary)
// simply, if a pointer is not zero, that will we calculated.
// output is not scaled, proper scaling must be applied by the caller (who should know what lis3d setting is used)
//
/*
 * std calculation stolen from wikipedia, calculates variance (square root of variance is standard deviation)
 * https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
 *
K = 0
n = 0
ex = 0
ex2 = 0

def add_variable(x):
    if (n == 0):
      K = x
    n = n + 1
    ex += x - K
    ex2 += (x - K) * (x - K)

def remove_variable(x):
    n = n - 1
    ex -= (x - K)
    ex2 -= (x - K) * (x - K)

def get_meanvalue():
    return K + ex / n

def get_variance():
    return (ex2 - (ex*ex)/n) / (n-1)
 */
/*
 *
 * lis3dh_collect3dAccSamples
 *
 * @desc    performs 3daccelerometer data collection, when a sample is ready (signaled by irq/semaphore)
 *
 * @param   samples number of samples to take
 * @param   acc3bufp    optional pointer to a buffer where the raw data is stored (3 dof) (NOT scaled to 'g' units !)
 * @param   acc3dmeanp  optional pointer to a structure to store the mean, scaled to 'g' unit.
 * @param   acc3dStd    optional pointer to a structure to store the standard deviation, scaled to 'g' unit.
 *
 * @return  false   if something went wrong
 *
 */
static bool lis3dh_collect3dAccSamples(uint32_t samples, tLis3dAcc * datap, tLis3dAccf * meanAccp, tLis3dAccf * stdAccp)
{
    bool rc_ok = true;
    tLis3dAcc acc3d;
    uint32_t samples_to_go = samples;
    int32_t x,y,z;// warning, this limits the amount of samples for averaging  to 65536

    int32_t n=0;
    int16_t k[3];
    int64_t ex[3];
    int64_t ex2[3];

    if (meanAccp) {
        x=0;
        y=0;
        z=0;
    }
    if (stdAccp) {
        for (int i=0; i<3; i++) {
            ex[i] = 0;
            ex2[i] = 0;
        }
    }

    while (samples_to_go && rc_ok) {

        // max wait of 1200 because lowest sample range is 1Hz, and the internal rc oscillator can be quite off
        if (pdTRUE == xSemaphoreTake(lis3d_data.semIrq,  1200 /  portTICK_PERIOD_MS  ))
        {
            //readLis3dhRegister(INT1_SOURCE, &regVal); // reading this clears the interrupt of the lis3d
            rc_ok = readLis3dhAccRegisters(&acc3d);// but irq is also cleared when reading all hi parts of the enabled sources (we enabled x,y,z)

            samples_to_go--;
            if (datap) {
                *datap++ = acc3d;
            }
            if (meanAccp) { // mean is easy
                x += acc3d.x;
                y += acc3d.y;
                z += acc3d.z;
            }

            if (stdAccp) { // standard deviation is a bit more complex
                union tmp_u {
                    int16_t ar[3];
                    tLis3dAcc vec3d;
                } *tmpu = (union tmp_u *) &acc3d ;

                if (n==0) {
                    for (int i=0; i<3; i++) {
                        k[i] = tmpu->ar[i];
                    }
                } else {
                    for (int i=0; i<3; i++) {
                        int16_t tmpi;
                        tmpi = tmpu->ar[i] - k[i];
                        ex[i] +=  tmpi;
                        ex2[i] += tmpi*tmpi;
                    }
                }
                n++;
            }
        }
        else
        {
            LOG_DBG(LOG_LEVEL_I2C, "%s() wait for acc data failed\n", __func__);
            rc_ok = false;
        }
    }

// now convert the mems sensor coordinate system to the passenger rail node coordinate system (as specified by Markk Rhodes, e-mail 2017-02-08)
// this depends on how and where the mems chip is soldered on the PCB
// to make things even more complex, according to the datasheet, the LIS3D reports the negative gravity acceleration as positive z value.
// Actually it reports the acceleration of the sensor, not the acceleration acting on the sensor.
// to make it that the gravity gives a -1 g, all axis must be swapped in sign (represented by the the (-1) in below equations).
// then we can apply the sensor on pcb orientation transform
// which in the case of rev2 and rev3 hardware means:
// sensor node x = -1 * (-1) * mems_x ->      mems_x
// sensor node y =      (-1) * mems_y -> -1 * mems_y
// sensor node z = -1 * (-1) * mems_z ->      mems_z


    if (meanAccp && rc_ok) {
        meanAccp->x = (float) x / samples;
        meanAccp->y = (float) -y / samples;// see sign/orientation discussion in above comment
        meanAccp->z = (float) z / samples;
    }

    if (stdAccp) {
        if (n>1) {
//            for (int i=0; i<3; i++) {
//                printf("%016llx %016llx\n", ex2[i], ex[i]);
//            }
            stdAccp->x = sqrt((ex2[0] - (ex[0]*ex[0])/n) / ((float) n -1 ));
            stdAccp->y = sqrt((ex2[1] - (ex[1]*ex[1])/n) / ((float) n -1 ));
            stdAccp->z = sqrt((ex2[2] - (ex[2]*ex[2])/n) / ((float) n -1 ));
        } else {
            stdAccp->x = 0;
            stdAccp->y = 0;
            stdAccp->z = 0;
         }
    }

    return rc_ok;
}


/*
 *
 * lis3dh_internalRead
 *
 * @desc    performs 3daccelerometer measurements
 *
 * @param   samples number of samples to take
 * @param   sample_freq will determine the 'odr' bits of the chip, the chip supports only a few fixed rates, 'odr' will be >= sample_freq, with a max of 400Hz
 * @param   selftestBits    the bit settings for enabling the selftest mode (see datasheet for details)
 * @param   acc3bufp    optional pointer to a buffer where the raw data is stored (3 dof) (NOT scaled to 'g' units !)
 * @param   acc3dmeanp  optional pointer to a structure to store the mean, scaled to 'g' unit.
 * @param   acc3dStd    optional pointer to a structure to store the standard deviation, scaled to 'g' unit.
 *
 * @return  false   if something went wrong
 *
 */

static bool lis3dh_internalRead(  uint32_t samples, uint16_t sample_freq, uint8_t selftestBits, tLis3dAcc *acc3buf, tLis3dAccf * acc3dmeanp, tLis3dAccf * acc3dStdp )
{
	bool retval = true;
    float GperVal = 1.0/32768;// default, will be overwritten
    uint32_t irq_counter = lis3d_data.irq_counter;
    uint8_t regVal = 0x55;

    // make sure we are ready to use the the lis3d semaphores/data and i/o pins are configured
    if (false == lis3dh_init())
    {
        return false;// early exit
    }

    // make sure no other functions are using this device, mutex take
    if (retval)
    {
        if (pdTRUE != xSemaphoreTake(lis3d_data.mutex, 100/portTICK_PERIOD_MS) )
        {
            LOG_DBG(LOG_LEVEL_I2C, "%s() mutex failed\n", __func__);
            return false;// early exit
        }
    }

    if (retval)
    {
        retval = whoAmI();	// 1st check the who am i register 0x0F
    }

    retval = DisableMemsInterrupts();

    // time to configure the lis3d, sample rate etc.
    if (retval)
    {
        uint8_t odr = GetOutputDataRate(sample_freq);
    	if(ConfigMemsForSelfTestMode(odr, selftestBits, &GperVal))
		{
    		retval = EnableMemsInterrupts();
    		irq_counter = lis3d_data.irq_counter;
		}
    }

    // and now the actual sampling
    if (retval)
    {
        retval = lis3dh_collect3dAccSamples(samples, acc3buf, acc3dmeanp, acc3dStdp);
        if (retval == false)
        {
        	LogMemsSamplingError();
            LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "lis3dh_internalRead() failed: irqs = %d-%d=%d",lis3d_data.irq_counter,irq_counter, lis3d_data.irq_counter-irq_counter);
        }
        else
        {
        	// If the read was successful, then verify the INT
       		retval = readLis3dhRegister(INT1_CFG, &regVal);
			if(regVal != ENABLE_XYZ_HIGH_INTERRUPTS)
			{
				LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "MEMS_INT_EN_ERROR, Got:0x%02x, Expected:0x2A", regVal);
			}
        }
    }

    writeLis3dhCfgRegisters(&lis3d_defaultcfg);// back to default config, return status ignored.

    // done with messing with the lis3d, so mutex give
    xSemaphoreGive(lis3d_data.mutex);

    lis3dh_terminate();

    // convert output values to 'g', except the raw buffer data
    if (acc3dmeanp)
    {
        acc3dmeanp->x *= GperVal;
        acc3dmeanp->y *= GperVal;
        acc3dmeanp->z *= GperVal;
    }
    if  (acc3dStdp)
    {
        acc3dStdp->x *= GperVal;
        acc3dStdp->y *= GperVal;
        acc3dStdp->z *= GperVal;
    }

	return retval;
}

// next function only used from the cli
/*
 *
 * lis3dh_read
 *
 * @desc    performs 3daccelerometer measurements
 *
 * @param   samples number of samples to take
 * @param   acc3bufp    optional pointer to a buffer where the raw data is stored (3 dof) (NOT scaled to 'g' units !)
 * @param   acc3dmeanp  optional pointer to a structure to store the mean, scaled to 'g' unit.
 * @param   acc3dStd    optional pointer to a structure to store the standard deviation, scaled to 'g' unit.
 *
 * @return  false   if something went wrong
 *
 */
bool lis3dh_read(uint32_t samples,  tLis3dAcc *acc3bufp, tLis3dAccf * acc3dmeanp, tLis3dAccf * acc3dStd)
{
    bool rc_ok = true;

    rc_ok = lis3dh_internalRead(samples, LIS3DH_SAMPLEFREQ, 0, acc3bufp, acc3dmeanp, acc3dStd);

    return rc_ok;
}

// fsd in g
// threshold in ???
// duration in seconds
// if movement is detected

/*
 *
 * lis3dh_movementMonitoring_i
 *
 * @desc    monitors movement for the specified time, will return earlier if movement detected.
 *
 * @param   fsd         acceleration range 2,4,8 or16 g
 * @param   threshold   goes into the threshold register of the lis3dh, effective  value depends on selected fsd
 * @param   duration    in seconds the time to monitor for movement
 * @param   movementDetectedp   returns here true if movement detected, else false, or nothing when NULL
 *
 * @return false       if something went wrong
 *
 */
static bool lis3dh_movementMonitoring_i(uint8_t fsd, uint8_t threshold, uint32_t duration,  bool * movementDetectedp)
{
    bool retval = true;
    uint8_t regVal = 0x00;

    if (movementDetectedp) *movementDetectedp=false;

    // make sure we are ready to use the the lis3d semaphores/data and i/o pinns are configured

    if (false == lis3dh_init()) {
        return false;// early exit
    }

    // make sure no other functions are using this device
    // mutex take
    if (pdTRUE != xSemaphoreTake(lis3d_data.mutex, 100/portTICK_PERIOD_MS) )
    {
            LOG_DBG(LOG_LEVEL_I2C, "lis3dh_init() mutex failed\n");
            return false;// early exit
    }

    // 1st check the who am i register 0x0F
    retval = whoAmI();

    // time to configure the lis3d, sample rate etc.
    if (retval)
    {
        tLis3dreg cfg = lis3d_defaultcfg;

        cfg.ctrl1 = 0x57; // Turn on sensor, enable X, Y and Z, ODR = 100Hz  (note that the application note has an error, it lists 0xA7)
        cfg.ctrl2 = 0x09; // High pass filter enable on data and INT1
        cfg.ctrl3 = 0x40; // AOI1 interrupt on INT1n

        regVal = cfg.ctrl4;

        switch(fsd)
        {
        case 2:
            regVal = regVal | 0x00;                 // fsd = 2g
            break;
        case 4:
            regVal = regVal | 0x10;                 // fsd = 4g
            break;
        case 8:
            regVal = regVal | 0x20;                 // fsd = 8g
            break;
        case 16:
            regVal = regVal | 0x30;                 // fsd = 16g
            break;
        default:
            regVal = regVal | 0x00;                 // fsd = 2g
            break;
        }
        cfg.ctrl4 = regVal;
        //cfg.ctrl5 = 0x08; // Interrupt latched not required when we react on the interrupt when it comes

        retval = writeLis3dhCfgRegisters(&cfg);
        if (retval != true) {
            LOG_DBG(LOG_LEVEL_I2C, "%s() failed\n", __func__);
        }

        if (retval) retval = writeLis3dhRegister(INT1_THS, threshold);   // Interrupt threshold level lsb ~ 16mg when fsd = 2g
        if (retval) retval = writeLis3dhRegister(INT1_DURATION, 0x00);   // Duration = 0x00
        if (retval) retval = readLis3dhRegister(REFERENCE, &regVal);     // read the reference reg, resets the reference value to delete the DC component
        if (retval) retval = readLis3dhRegister(INT1_SOURCE, &regVal); // reading this clears the interrupt of the lis3d
        if (retval) retval = writeLis3dhRegister(INT1_CFG, ENABLE_XYZ_HIGH_INTERRUPTS);        // OR combination of interrupt events

        xSemaphoreTake(lis3d_data.semIrq,  0 );// all this configuration stuff has probably already triggered an interrupt, clear the semaphore
    }

    if(retval)
    {
        if (pdTRUE == xSemaphoreTake(lis3d_data.semIrq,  duration * 1000 /  portTICK_PERIOD_MS  ))
        {
            readLis3dhRegister(INT1_SOURCE, &regVal); // reading this clears the interrupt of the lis3d
            if (movementDetectedp)
			{
            	*movementDetectedp=true; // we got movement
			}
        }
    }

    writeLis3dhCfgRegisters(&lis3d_defaultcfg);// back to default config, return status ignored.
    // done with messing with the lis3d, so mutex give
    xSemaphoreGive(lis3d_data.mutex);
    //printf("exit irq_count = %d\n", lis3d_data.irq_counter);

    lis3dh_terminate();

    return retval;
}



// according appnote chapter 6.2, table 16
// threshold register is 7 bit
// full scale threshold LSB
//   2      ~16
//   4      ~31
//   8      ~63
//   16    ~125
static uint8_t th_table[] = {
        /*  2  0x00 */  16,
        /*  4  0x10 */  31,
        /*  8  0x20 */  63,
        /* 16  0x30 */ 125
};

/*
 *
 * lis3dh_movementMonitoring
 *
 * @desc    monitors movement for the specified time, will return earlier if movement detected.
 *
 * @param   threshold   in g, goes into the threshold register of the lis3dh, effective  value depends on selected fsd
 * @param   duration    in seconds the time to monitor for movement
 * @param   movementDetectedp   returns here true if movement detected, else false, or nothing when NULL
 *
 * @return false       if something went wrong
 *
 */
bool lis3dh_movementMonitoring(float threshold_g, uint32_t duration_sec,  bool * movementDetectedp)
{
    uint32_t fs=3, ths, final_mg=0;
    uint32_t threshold_mg = fabs(threshold_g*1000);
    uint16_t i;

    // some safeguarding here
    if (duration_sec > 120) {
        LOG_DBG(LOG_LEVEL_I2C,"lis3dh_movementMonitoring(): quite long duration specified (%d) limiting to 2 minutes!\n",duration_sec );
        duration_sec = 120;
    }

    for (i=0; i<sizeof(th_table)/sizeof(th_table[0]); i++) {
        ths = (threshold_mg + (th_table[i]>>1))/th_table[i];// rounded division
        if (ths < 0x80) {
            fs = i;
            final_mg = th_table[i] * ths;
            break;
        }
    }

    if (ths > 0x7f) {
        LOG_DBG(LOG_LEVEL_I2C,"lis3dh_movementMonitoring(): threshold specified too high (%d mg)\n", threshold_mg );
        ths = 0x7f;
        fs = 3;
        final_mg = th_table[3] * ths;
    }

    if (final_mg == 0) {
        LOG_DBG(LOG_LEVEL_I2C,"lis3dh_movementMonitoring(): resulting threshold too low, adjusting to lowest possible value !\n" );
        ths = 1;
        fs = 0;
        final_mg = th_table[0] * ths;
    }

    LOG_DBG(LOG_LEVEL_I2C,"lis3dh_movementMonitoring(): time %d seconds, threshold %d mg -> fs = %d, ths = %d -> %d mg\n",duration_sec, threshold_mg, 1<<(fs+1), ths, final_mg );

    return lis3dh_movementMonitoring_i( 1<<(fs+1), ths,  duration_sec,  movementDetectedp);
}

/*
 *
 * lis3dh_readAveraged
 *
 * @desc    performs 3daccelerometer measurements and returns an averaged value
 *
 * @param   samples number of samples to take
 * @param   gX,gY,gZ    pointers to where the averaged g values are written
 * @param   durationSecs duration in seconds
 * @param   samplesPerSecond ??? not used anymore
 *
 * @return false   if something went wrong
 *
 */

bool lis3dh_readAveraged(float* gX, float* gY, float* gZ, uint8_t durationSecs, uint8_t samplesPerSecond)
{
    bool rc_ok = true;
    tLis3dAccf mean3dOut;

    // We use the define, so what to do with the samplesPerSecond parameter ?
    rc_ok = lis3dh_internalRead( durationSecs* LIS3DH_SAMPLEFREQ, LIS3DH_SAMPLEFREQ, 0, NULL, &mean3dOut, NULL);

    if (rc_ok) {
        if (gX) *gX = mean3dOut.x;
        if (gY) *gY = mean3dOut.y;
        if (gZ) *gZ = mean3dOut.z;
    }

    return rc_ok;
}



static bool writeLis3dhRegister(uint8_t reg, uint8_t value)
{
	bool retval = false;

	// Write the new value back
	retval = I2C_WriteRegister(I2C0_IDX, lis3dh, reg, value);

	return retval;
}

static bool readLis3dhRegister(uint8_t reg, uint8_t* regVal)
{
	bool retval = false;

	// read the register
	retval = I2C_ReadRegister(I2C0_IDX, lis3dh, reg, 1, regVal);

	return retval;
}



static const tLis3dAccf selftestLimitsLow  = { 17*4e-3,  17*4e-3,  17*4e-3 };// [g] from datasheet rev 2, 'normal' 10 bit mode values scaled to hi res 12 bit mode, (*4)
static const tLis3dAccf selftestLimitsHigh = {360*4e-3, 360*4e-3, 360*4e-3 };

// used in application
// used from cli
/*
 *
 * lis3dh_read
 *
 * @desc    performs    3daccelerometer selftest (see datasheet for what it does)
 *
 * @param   samples     number of samples to take for averaging the result
 *
 * @return false   if something went wrong
 *
 */
bool lis3dh_selfTest(uint32_t samples)
{
    bool rc_ok = true;
    bool test_ok = true;
    tLis3dAccf test[5];
    uint8_t st[5] = {0,1,0,2,0};// the ST bits for reg 4


    if (samples==0) samples = (LIS3DH_SAMPLEFREQ + 9)/10;// some sane value if nothing was supplied, which is approximate 0.1 second

    LOG_DBG(LOG_LEVEL_I2C,"Using %d samples for averaging\n", samples);

    // normal read
    for (int i=0; rc_ok && i<sizeof(test)/sizeof(*test); i++) {
        if (rc_ok) rc_ok = lis3dh_internalRead( samples, LIS3DH_SAMPLEFREQ, st[i], NULL, & test[i], NULL );
    }

    if (rc_ok) {

        for (int i=1; i<sizeof(test)/sizeof(*test); i++) {
            LOG_DBG(LOG_LEVEL_I2C,"test mode bits  %d -> %d\n",st[i-1], st[i] );

            if ( (fabs(test[i].x - test[i-1].x) > selftestLimitsHigh.x) || (fabs(test[i].x - test[i-1].x) < selftestLimitsLow.x) ) {
                test_ok = false;
                LOG_DBG(LOG_LEVEL_I2C," ! failure ! ");
            }
            LOG_DBG(LOG_LEVEL_I2C," gX : %9.6f - %9.6f = %9.6f\n", test[i-1].x , test[i].x, test[i].x - test[i-1].x);

            if ( (fabs(test[i].y - test[i-1].y) > selftestLimitsHigh.y) || (fabs(test[i].y - test[i-1].y) < selftestLimitsLow.y) ) {
                test_ok = false;
                LOG_DBG(LOG_LEVEL_I2C," ! failure ! ");
            }
            LOG_DBG(LOG_LEVEL_I2C," gY : %9.6f - %9.6f = %9.6f\n", test[i-1].y , test[i].y, test[i].y - test[i-1].y);

            if ( (fabs(test[i].z - test[i-1].z) > selftestLimitsHigh.z) || (fabs(test[i].z - test[i-1].z) < selftestLimitsLow.z) ) {
                test_ok = false;
                LOG_DBG(LOG_LEVEL_I2C," ! failure ! ");
            }
            LOG_DBG(LOG_LEVEL_I2C," gZ : %9.6f - %9.6f = %9.6f\n", test[i-1].z , test[i].z, test[i].z - test[i-1].z);
        }
    } else {
        LOG_DBG(LOG_LEVEL_I2C,"selftest failure\n");
    }

    return rc_ok && test_ok;// both should be OK

}

// reads an ID register to confirm we are talking to the right chip
static bool whoAmI()
{
	bool retval = false;
	uint8_t regVal = 0x00;

	// 1st check the who am i register 0x0F
	retval = readLis3dhRegister(WHO_AM_I, &regVal);
	if(retval != true || regVal != 0x33)
	{
		LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL,"**WhoAmI() failed In** , %s",__FILE__);
	}

	return retval;
}

// Disable MEMS interrupts.
static bool DisableMemsInterrupts()
{
	bool retval = false;
	uint8_t regVal = 0x55;

	retval = writeLis3dhRegister(INT1_CFG, DISABLE_INTERRUPTS);
	if (retval)
	{
		retval = readLis3dhRegister(INT1_CFG, &regVal);			// Check if the interrupts have been disabled.
		if(regVal != DISABLE_INTERRUPTS)
		{
			LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "MEMS_INT_DIS_ERROR, Got:0x%x, Expected:0", regVal);
			retval = false;
		}
	}

	return retval;
}

// Enable MEMS Interrupts.
static bool EnableMemsInterrupts()
{
	bool retval = false;
    tLis3dAcc stcAcc3d;
    uint8_t regVal;

	retval = readLis3dhAccRegisters(&stcAcc3d);// dummy read all registers, seems to work more reliable than reading INT1_SOURCE to clear interrupts
	if (retval)
	{
		retval = readLis3dhRegister(INT1_SOURCE, &regVal); // reading this clears the interrupt of the lis3d as per datasheet
	}
	if (retval)
	{
		retval = writeLis3dhRegister(INT1_CFG, ENABLE_XYZ_HIGH_INTERRUPTS);	// Now enable all interrupts.
	}

	return retval;
}

static bool ConfigMemsForSelfTestMode(uint8_t outDataRate, uint8_t selftestBits, float *pResolution_2g)
{
	bool retval = false;
    tLis3dreg stcCfg = lis3d_defaultcfg;
    uint16_t sampleTime_msec = odr_to_sampletimeMs[outDataRate];
    MEMS_OP_MODES OpMode;

    // Set up the control regs.
    stcCfg.ctrl1 = EN_XYZ_AXIS_DETECTION | (outDataRate << SHIFT_TO_UPPER_NIBBLE); // 100Hz enable x,y,z
    stcCfg.ctrl2 = EN_HP_FILTER_NORMAL_MODE; 	// normal mode not reset HP (why ?)
    stcCfg.ctrl3 = EN_ZYXDA_INT_ON_PIN_INT1; 	// data ready irq on INT1n
    stcCfg.ctrl4 = EN_HIGH_RES_OUTPUT_MODE_BIT_MASK | (selftestBits << ST_BIT_POSITION); // HR (high resolution enable), what does it really do ?

    retval = writeLis3dhCfgRegisters(&stcCfg);

    if (retval != true)
    {
    	LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "Mems ST config failed.");
    }

    if (retval)
	{
    	retval = GetOpModeResolutionInG(&OpMode, pResolution_2g);
    	if(selftestBits)
    	{
    		// selftest takes some time to ripple to the mems filters (8 samples when in 12 bit mode)
    		if(OpMode == eHIGH_RESOLUTION_MODE)
    		{
    			sampleTime_msec *= HIGH_RES_OUT_MODE_DELAY_SAMPLES;
    		}
    		else
    		{
    			sampleTime_msec *= DELAY_SAMPLES_OTHER_MODES;
    		}
    	}
    }

	vTaskDelay(sampleTime_msec / portTICK_PERIOD_MS);// we should wait for at least one sample period according the data sheet, 15ms should be enough when sampling at 100Hz

    return retval;
}

// Get the resolution in G @ +/- 2g based on the op mode
static bool GetOpModeResolutionInG(uint8_t *pOpMode, float *pResolution_2g)
{
	bool retval = false;
	uint8_t reg1Val = 0;
	uint8_t reg4Val = 0;
	MEMS_OP_MODES eMode = eLOW_POWER_MODE;

	retval = readLis3dhRegister(CTRL_REG1, &reg1Val);
	if (retval)
	{
		retval = readLis3dhRegister(CTRL_REG4, &reg4Val);
	}
	if(retval)
	{
        // we apply here the setting for the scaling, so also determine the proper scaling factor
        // get sensitivity out of the config reg 4, bits FS1,FS0
        // Full scale selection. default value: 00
        // (00: +/- 2G; 01: +/- 4G; 10: +/- 8G; 11: +/- 16G)
        // the documentation makes not much sense but this seems to work, although nowhere mentioned in the data sheet, the resolution appears to be 12 bit.
        // then the spec of 1mg/digit at +/- 2G range makes sense (+/- 12 bit -> +/- 2048 -> one increment 1mg)
		if(pResolution_2g != NULL)
		{
			*pResolution_2g = (float) (2 << ((reg4Val & FS_SELECTION_BIT_MASKS) >> SHIFT_TO_LOWER_NIBBLE)) / 32768.0f;
		}

		// LPen = 1, HR = 0
		if(((reg1Val & LPEN_BIT_MASK) == true) && ((reg4Val & EN_HIGH_RES_OUTPUT_MODE_BIT_MASK) == false))
		{
			eMode = eLOW_POWER_MODE;	// Low power mode, 8-bit data o/p.
		}
		// LPen = 0, HR = 0
		else if(((reg1Val & LPEN_BIT_MASK) == false) && ((reg4Val & EN_HIGH_RES_OUTPUT_MODE_BIT_MASK) == false))
		{
			eMode = eNORMAL_MODE;		// Normal mode, 10-bit data o/p.
		}
		// LPen = 0, HR = 1
		else if(((reg1Val & LPEN_BIT_MASK) == false) && ((reg4Val && EN_HIGH_RES_OUTPUT_MODE_BIT_MASK) == true))
		{
			eMode = eHIGH_RESOLUTION_MODE;	// High Resolution mode, 12-bit data o/p.
		}
		else
		{
			LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "Invalid Mems mode config.");
			retval = false;
		}

		if(pOpMode != NULL)
		{
			*pOpMode = eMode;
		}
	}

	return retval;
}

static uint8_t GetOutputDataRate(uint16_t sampleFreq_Hz)
{
	uint8_t i = 0;
    // find frequency >= requested frequency
    for (i = 0; i < sizeof(odr_to_samplefreq) / sizeof(*odr_to_samplefreq); i++)
    {
        if (sampleFreq_Hz <= odr_to_samplefreq[i])
        {
            break;
        }
    }

    return i;
}

static void LogMemsSamplingError()
{
	// read status reg to find out what went wrong
	uint8_t abyStatReg[4] = {0,0,0,0};
	bool bReadSuccess = true;

	if (true == readLis3dhRegister(STATUS_REG_AUX, &abyStatReg[0]))
	{
		LOG_DBG(LOG_LEVEL_I2C,"STATUS_REG_AUX = %02x\n",abyStatReg[0]);
	}
	else
	{
		bReadSuccess = false;
	}
	if (true == readLis3dhRegister(STATUS_REG2, &abyStatReg[1]))
	{
		LOG_DBG(LOG_LEVEL_I2C,"STATUS_REG2 = %02x\n",abyStatReg[1]);
	}
	else
	{
		bReadSuccess = false;
	}
	if (true == readLis3dhRegister(INT1_SOURCE, &abyStatReg[2]))
	{
		LOG_DBG(LOG_LEVEL_I2C,"INT1_SOURCE = %02x\n",abyStatReg[2]);
	}
	else
	{
		bReadSuccess = false;
	}
	if (true == readLis3dhRegister(WHO_AM_I, &abyStatReg[3]))
	{
		LOG_DBG(LOG_LEVEL_I2C,"WHO_AM_I = %02x\n",abyStatReg[3]);
	}
	else
	{
		bReadSuccess = false;
	}

	if(bReadSuccess == false)
	{
		LOG_EVENT( 0, LOG_LEVEL_I2C, ERRLOGFATAL, "%s() REG_AUX:%02x, REG2:%02x, INT1_SRC:%02x, I:%02x read failed",
					__func__, abyStatReg[0], abyStatReg[1], abyStatReg[2], abyStatReg[3]);
	}
}


#ifdef __cplusplus
}
#endif