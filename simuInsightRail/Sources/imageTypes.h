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

extern const char sMK24_Loader[] ;
extern const char sMK24_Application[] ;
extern const char sPMIC_Application[] ;
extern const char sPackage[] ;

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