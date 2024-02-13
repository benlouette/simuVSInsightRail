#ifdef __cplusplus
extern "C" {
#endif

/*
 * imageTypes.h
 *
 *  Created on: 5 Feb 2019
 *      Author: RZ8556
 */

#ifndef SOURCES_IMAGETYPES_H_
#define SOURCES_IMAGETYPES_H_

const char sMK24_Loader[] = "MK24_Loader";
const char sMK24_Application[] = "MK24_Application";
const char sPMIC_Application[] = "PMIC_Application";
const char sPackage[] = "Package";

typedef enum
{
	IMAGE_TYPE_UNKNOWN 			= 0,
	IMAGE_TYPE_LOADER			= 1,
	IMAGE_TYPE_APPLICATION		= 2,
	IMAGE_TYPE_PMIC_APPLICATION	= 3,
	IMAGE_TYPE_PACKAGE			= 4,
	IMAGE_TYPE_MAX				= 5
} ImageType_t;

#endif /* SOURCES_IMAGETYPES_H_ */


#ifdef __cplusplus
}
#endif