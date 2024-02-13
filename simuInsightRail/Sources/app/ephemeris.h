#ifdef __cplusplus
extern "C" {
#endif

/*
 * ephemeris.h
 *
 *  Created on: 11 Feb 2021
 *      Author: KC2663
 */

#ifndef SOURCES_APP_EPHEMERIS_H_
#define SOURCES_APP_EPHEMERIS_H_

/*
 * 'Macro magic' to declare matching enums and strings
 * See https://stackoverflow.com/questions/9907160/how-to-convert-enum-names-to-string-in-c
 */

#define EPO_STATUS \
C(NO_UPDATE)					\
C(EPO_DOWNLOAD_OK)				\
C(EPO_UPDATE_IN_PROGRESS)		\
C(EPO_UPDATED_OK)				\
C(MQTT_SUBSCRIBE_ERROR)			\
C(MQTT_PUBLISH_ERROR)			\
C(DOWNLOAD_CRC_ERROR)			\
C(DOWNLOAD_TIMEOUT_ERROR)		\
C(GNSS_POWER_ON_ERROR)			\
C(GNSS_STATUS_ERROR)			\
C(GNSS_WRITE_BLOCK_ERROR)		\
C(GNSS_WRITE_ACK_ERROR)			\
C(GNSS_WRITE_END_BLOCK_ERROR)	\
C(GNSS_WRITE_END_ACK_ERROR)		\
C(EPO_STORE_TO_FLASH_ERROR)		\


#define C(x) x,
enum EpoStatus { EPO_STATUS EPO_STATUS_TOP };
#undef C

#define C(x) #x,
const char * const EpoErrorString[] = { EPO_STATUS };

enum EpoStatus Ephemeris_Download(void);
enum EpoStatus Ephemeris_epoToGNSS(void);

enum EpoStatus Ephemeris_LoadAndCheck();
void Ephemeris_Erase(void);
bool Ephemeris_CheckStatus(uint32_t *epoExpiry);
bool Ephemeris_CheckCommsError(enum EpoStatus result_code);
enum EpoStatus Ephemeris_Update(void);

#define TIMEOUT_EPHEMERIS_S		10
#define TIMEOUT_NO_EPHEMERIS_S	60

#define EPHEMERIS_DAYS_BEFORE_EXPIRY_TO_UPDATE (6)

#endif /* SOURCES_APP_EPHEMERIS_H_ */


#ifdef __cplusplus
}
#endif