#ifdef __cplusplus
extern "C" {
#endif

/*
 * Insight.h
 *
 *  Created on: 23-Nov-2015
 *      Author: D. van der Velde
 */

#ifndef INSIGHT_H_
#define INSIGHT_H_

/*
 * Includes
 */
//#include "GitRev.h"
#define GIT_REVISION                   ((0/*BUILD_BUILDID*/))
/*
 * Defines
 */


#define INSIGHT_VERSION_CONFIG_NAME    "DEBUG"
//#define INSIGHT_VERSION_CONFIG_NAME    "ALPHA"
//#define INSIGHT_VERSION_CONFIG_NAME    "BETA"
//#define INSIGHT_VERSION_CONFIG_NAME    "RELEASE"

/*
 * Product Name
 */
#define INSIGHT_PRODUCT_NAME           "Insight Passenger Rail Cellular Node"

// Enable the passenger rail project specific implementation.
#define PASSENGER_RAIL
/*
 * Software version number
 */
#ifndef COMPILE_VERSION_MAJOR
#warning COMPILE_VERSION_MAJOR undefined!
#define COMPILE_VERSION_MAJOR (0)
#endif
#ifndef COMPILE_VERSION_MINOR
#warning COMPILE_VERSION_MINOR undefined!
#define COMPILE_VERSION_MINOR (0)
#endif

#define INSIGHT_VERSION_MAJOR            (COMPILE_VERSION_MAJOR)
#define INSIGHT_VERSION_MINOR            (COMPILE_VERSION_MINOR)


#define INSIGHT_VERSION_FORMAT			"v%d.%d.%ld" //! Major.Minor.GitRevision

// Git version info pulled in from GitRev.h.
#define INSIGHT_BUILD_NUMBER             (GIT_REVISION)

/*
 * Types
 */


#endif /* INSIGHT_H_ */


#ifdef __cplusplus
}
#endif