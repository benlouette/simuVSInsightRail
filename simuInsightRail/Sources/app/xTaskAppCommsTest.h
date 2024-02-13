#ifdef __cplusplus
extern "C" {
#endif

/*
 * xTaskAppCommsTest.h
 *
 *  Created on: 7 July 2016
 *      Author: George de Fockert
 */

#ifndef SOURCES_APP_XTASKAPPCOMMSTEST_H_
#define SOURCES_APP_XTASKAPPCOMMSTEST_H_

//#define SIMULATED_MEASRECORD_MASK           (1<<0)
//#define SIMULATED_COMMSRECORD_MASK          (1<<1)
//#define SIMULATED_WAVEFORM_ENV3_MASK        (1<<2)
//#define SIMULATED_WAVEFORM_WHEELFLAT_MASK   (1<<3)
//#define SIMULATED_WAVEFORM_RAW_MASK         (1<<4)
//#define SIMULATED_ALL_MASK (SIMULATED_MEASRECORD_MASK | SIMULATED_COMMSRECORD_MASK |SIMULATED_WAVEFORM_ENV3_MASK | SIMULATED_WAVEFORM_WHEELFLAT_MASK | SIMULATED_WAVEFORM_RAW_MASK )




/*
 * TODO : NOTE: these constants should be part of the measure task of the project (not the platform part)
 *
 * default scaling value calculations :
 *
 *
    Info from Bart and Julian  (19-oct-2016):


    xtal + pre-amp : 16.8mV/g  (how derived, theoretical or just looking at what comes out of the ADC ?)
    flats signal path gain : 1.01
    raw signal path gain   : 1.02

    ADC Vref : 3 Volt

    raw_adcval_to_g_factor    = (3V/2^24)/(16.8e-3 * 1.02)
    wflats_adcval_to_g_factor = (3V/2^24)/(16.8e-3 * 1.01)


    extra input : e-mail julian 24-nov-2016:
     The 16.8mV/g is a theoretical value calculated based on the circuit design.
     Last week we used  5 sensors to perform a gage R&R where 3 operators tested each sensor twice and
     I analysed the data which shows that the mean sensitivity is 17.54mV/g, can you update the firmware to include this figure?

    filter paths gains according to Bart:

    rectifier gain: 1
    smoothing gain: 1
    decim1    gain: 2
    decim2    gain: 2

    which implies :

    raw_dspout_to_g_factor    = raw_adcval_to_g_factor  ;  no dsp signal processing
    wflats_dspout_to_g_factor = wflats_adcval_to_g_factor / ( 2*2)


    input : e-mail Mark Rhodes 24-mar-2017:

        Raw measurements are scaled correctly.
        For env3 and wheelflat we have discovered that a scaling factor is applied in the Microlog so that an increase in vibration amplitude from 1g to 2g is correctly shown as a 1 g increase in the enveloped signal.
        We need to apply a factor of 1/0.6 (i.e. 1.66666666) to the env3 and wheeflat time series data.

 *
 *
 */

#include "TaskComm.h"

#define MEASURE_MICROLOG_FACTOR (0.6)

#define MEASURE_XTALPREAMPGAIN           (17.54e-3)
#define MEASURE_RAW_SCALING             ((3.0/(1<<24))/(MEASURE_XTALPREAMPGAIN * 1.02))
#define MEASURE_BEARING_ENV3_SCALING    ((3.0/(1<<24))/(MEASURE_XTALPREAMPGAIN * 1.01 * MEASURE_MICROLOG_FACTOR) / (2*2))
#define MEASURE_WHEELFLAT_SCALING       ((3.0/(1<<24))/(MEASURE_XTALPREAMPGAIN * 1.01 * MEASURE_MICROLOG_FACTOR) / (2*2))


bool xTaskAppCommsTestInit();
bool commHandling(uint32_t param, bool simulationMode);
void print_measurement_record(void);
void print_communication_record(void);
bool publishImagesManifestUpdate();
uint32_t GetDatetimeInSecs(void);
void checkInitialSetup();
bool commsDoOtaOnly();

tCommHandle* getCommHandle();


#endif /* SOURCES_APP_XTASKAPPCOMMSTEST_H_ */


#ifdef __cplusplus
}
#endif