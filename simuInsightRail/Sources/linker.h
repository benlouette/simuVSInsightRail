#ifdef __cplusplus
extern "C" {
#endif

/*
 * linker.h
 *
 *  Created on: 6 Dec 2018
 *      Author: RZ8556
 */

#ifndef SOURCES_LINKER_H_
#define SOURCES_LINKER_H_

#include <stdint.h>

/*
 * This file should be included for visibility of linker defined locations
 * e.g. shared memory objects
 */

extern uint32_t __app_image_size;

extern char __loader_version[];
extern char __app_version[];
extern uint32_t __app_version_size;
extern uint32_t __loader_version_size;

extern uint32_t __loader_origin[],
				__loader_image_size,
				__app_origin[],
				__app_text[],
                __app_nvdata[],
                __app_nvdata_size,
                __app_cfg[] ,
 				__app_cfg_size,
				__app_cfg_shadow[],
				__app_cfgsh[],
 				__device_calib[],
 				__device_calib_size,
 				__event_log[],
				__eventlog_size,
 				__bootcfg[],
 				__bootcfg_size,
				__ota_mgmnt_data[],
				__ota_mgmnt_size;

extern uint32_t __sample_buffer_size;
extern int32_t __sample_buffer[];

#endif /* SOURCES_LINKER_H_ */


#ifdef __cplusplus
}
#endif