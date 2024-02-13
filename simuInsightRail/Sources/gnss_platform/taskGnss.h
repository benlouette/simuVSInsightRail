#ifdef __cplusplus
extern "C" {
#endif

/*
 * taskGnss.h
 *
 *  Created on: Jan 31, 2017
 *      Author: ka3112
 */

#ifndef SOURCES_APP_TASKGNSS_H_
#define SOURCES_APP_TASKGNSS_H_

/*
 * Includes
 */
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <FreeRTOS.h>
#include <timers.h>
#include <semphr.h>
#include <portmacro.h>

#include "taskGnssEvent.h"

typedef enum {
    GNSSSTATE_DOWN = 0,  	// gnss not ready for use, powered down/ startup message not yet received etc.
    GNSSSTATE_UP ,    		// gnss module powered, and communication ready
    GNSSSTATE_MAX           // Max enumeration value (not a setting)
} tGnssState;


// experimental first valid time signalling
#define GNSS_FIRST_VALID_TIME

#define MAXGNSS_SPEEDHIST (10)
#define MAX_SATID (32)

typedef struct {
    uint32_t powerupTimeMs;// TODO : take time which does not flip over (not needed for railway app, because it reboots every time)
    uint32_t firstFixTimeMs;// 0 means here  no fix yet,
    uint32_t firstAccurateFixTimeMs;// 0 means here  no fix yet,
    uint32_t currentFixTimeMs;

#ifdef GNSS_FIRST_VALID_TIME
    bool validTime;
#endif
    // data from RMC
    bool valid;
    float utc_time;
    float latitude;
    char NS;
    float longitude;
    char EW;
    float speed_knots;
    float course_degrees;
    uint32_t utc_date;

    // data from GSA
    float HDOP;

    // data from GSV
    struct gnss_satInfo {// struct layout is chosen because of the existing passenger rail reporting way.
        uint16_t numsat;
        uint8_t id[MAX_SATID];
        uint8_t snr[MAX_SATID];
    } gpsSat, glonassSat;
} tGnssCollectedData;

#define GNSS_MEANSPEED
typedef struct gnss_status {

    tGnssState state;


    SemaphoreHandle_t semAccess;// access control semaphore of part of this status struct (example:GNSS command in progress may not be interrupted by another command issued by another task)
    float HDOPlimit; // HDOP must be lower (= more accurate) than this value for reporting 'firstAccuarteFix'

    struct {
        uint16_t idx;
        uint16_t count;
        struct {
            float utc;
            bool valid;
            float speed;
            float HDOP;
            float lat;
            char NS;
            float lon;
            char EW;
        } data[MAXGNSS_SPEEDHIST];
#ifdef GNSS_MEANSPEED
        uint32_t n;
        float k,ex,ex2;
        float mean_speed, std_speed;
#endif
    } history;

    struct gnss_satInfo gpsSat, glonassSat;// copied to collectedData when reception complete, so no half fllled data
    tGnssCollectedData collectedData ;

} tGnssStatus;

// callback functions, a separate one, for every task event noteworthy
typedef uint8_t (* tGnssCallbackFuncPtr)(void * whatever);
typedef enum
{
    gnss_cb_nmea = 0,		// one of the 'standard' nmea messages
    gnss_cb_proprietary,	// the special ones '$P<whatever>'
    gnss_cb_first_accurate_fix, // when the first valid position is received whith a HDOP lower than the specified limit
    gnss_cb_max
} tGnssCallbackIndex;


typedef enum {
    nmea_unknown,
    nmea_GLL,	// Geographical Position - Latitude/Longitude
    nmea_RMC,	// Recommended Minimum Specific GNSS Data
    nmea_VTG,	// Course Over Ground and Ground Speed
    nmea_GGA,	// Global Positioning System Fixed Data
    nmea_GSA,	// GNSS DOP and Active Satellites
    nmea_GSV,	// GNSS Satellites in View
    nmea_ZDA,	// Time & Date
    nmea_LAST,// used in declaration of array size
} tNmeaType;

typedef enum {
    sat_gps = 'P',
    sat_glonass = 'L',
    sat_gnss = 'N'
} tSatSystem;

typedef struct {
    tNmeaType type;
    tSatSystem system;
    union {
        struct {
            float utc_time;
            bool valid;
            float latitude;
            char NS;
            float longitude;
            char EW;
            float speed_knots;
            float course_degrees;
            uint32_t date;
            float magvar;
            char magvar_EW;
            char mode;
        } rmc;

        struct {
            uint16_t number_of_msg;
            uint16_t msg_num;
            uint16_t sat_in_view;
            struct {
                uint16_t id;
                uint16_t elevation;
                uint16_t azimuth;
                uint16_t snr;
            } sat[4];
        } gsv;

        struct {
            float utc_time;
            float latitude;
            char NS;
            float longitude;
            char EW;
            uint8_t fix;// 0=nofix, 1=gps fix, 2= diff gps fix
            uint16_t SatsUsed;
            float HDOP;
            float MSL_altitude;
            char MSL_Units;
            float geoidalSeparation;
            char  geoidalSeparation_Units;
            uint32_t AgeOfDiffCorr;
        } gga;

        struct {
            char mode_1;// M: Manualforced to operate in 2D or 3D mode, A: 2 Automatic-allowed to automatically switch 2D/3D
            uint8_t mode_2;// 1:Fix not available, 2: 2D (<4 SVs used), 3: 3D (>= 4 SVs used)
            uint16_t Satellite_Used[12];// SV on Channel 1..12 (GPS SV No. #01~#32, GLONASS SV No. #65~#96)
            float PDOP; // Position Dilution of Precision
            float HDOP; // Horizontal Dilution of Precision
            float VDOP; // Vertical Dilution of Precision
        } gsa;

        struct {
            float course1;
            char reference1;//   T: True track made good, M: Magnetic track made good, N: Ground speed, knots, K:      Ground speed, Kilometers per hour
            float course2;// no idea ??
            char reference2;// no idea ??
            float speed1; // knots
            char speed_unit1;// N: knots
            float speed2;
            char speed_unit2; // K: kilometer/hour
            char mode;// A: Autonomous, D:differential, E: Estimated
        } vtg;

    } nmeaData;
} tNmeaDecoded;

extern TaskHandle_t _TaskHandle_Gnss;// not static because of easy print of task info in the CLI
extern tGnssStatus gnssStatus;

BaseType_t Gnss_NotifyRxData_ISR( );

void taskGnss_Init(bool nodeAsAtestBox);
bool gnssStartupModule( uint32_t baudrate, uint32_t maxStartupWaitMs);
bool gnssShutdownModule( uint32_t maxShutdownWaitMs);
bool gnssSendNmeaCommand( char * cmd,  uint32_t maxNMEAWaitMs);
bool gnssRetrieveCollectedData( tGnssCollectedData * collectedData_p, uint32_t maxWaitMs);
bool gnssSetHDOPlimit( float HDOP, uint32_t maxWaitMs);
void GnssRemoveCallback(tGnssCallbackIndex cbIdx, tGnssCallbackFuncPtr cbFunc);
void GnssSetCallback(tGnssCallbackIndex cbIdx, tGnssCallbackFuncPtr cbFunc);

// binary functions
BaseType_t Gnss_NotifyBinRxData_ISR(struct gnss_buf_str * processing);
struct gnss_buf_str * Gnss_WaitBinEvent();

char *get_token(char **str, char delimiter);//should go into some 'utils' location

#endif /* SOURCES_APP_TASKGNSS_H_ */


#ifdef __cplusplus
}
#endif