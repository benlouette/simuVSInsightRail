#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Copyright (c) 2014, 2015 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander - initial API and implementation and/or initial documentation
 *    Ian Craggs - convert to FreeRTOS
 *******************************************************************************/
/*
 * MQTTFreeRTOS_lwIP.c
 *
 * Adapted from MQTTFreeRTOS.c to interface with the lwIP stack used in the SKF SW platform
 * Daniel van der Velde
 */

/*
 * Includes
 */
#include <string.h>              // memset


#include "MQTTFreeRTOS_lwIP.h"

#if (CONFIG_COMM_LOWER_STACK_SELECT == CONFIG_COMM_LOWER_STACK_SELECT_NET)


#include "lwip/api.h"
#include "lwip/ip_addr.h"
#include "Log.h"

/*
 * Macros
 */
// lwIP recv time-out in [ms]; any read() timeout must be at least this number. This is
// also the lowest resolution that can be used for MQTT read time-outs.
#define MQTTFREERTOS_LWIP_RECV_TIMEOUT              (100) // TODO Check recv() timeout value


int MQTTFreeRTOS_lwIP_read(MQTT_Network* n, unsigned char* buffer, int len, int timeout_ms)
{
	TickType_t xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
	TimeOut_t xTimeOut;
	int recvLen = 0;
	void *inbuf_p;
	u16_t inbuflen, copylen;
	struct pbuf *p;
    err_t err;
    u8_t done = 0;

	vTaskSetTimeOutState(&xTimeOut); /* Record the time at which this function was entered. */

	do {
	    // Check if there is data left from the previous read operation
	    if ( n->lastdata_p )
	        inbuf_p = n->lastdata_p;
	    else {
	        // No data left from the previous operation, get new data from the network
	        err = netconn_recv_tcp_pbuf((struct netconn*)n->conn_p, (struct pbuf **)&inbuf_p);
	        if (err != ERR_OK) {
	            if (recvLen > 0) {
	                /* update receive window */
	                netconn_recved((struct netconn*)n->conn_p, (u32_t)recvLen);
	                /* already received data, return that */
	                return recvLen;
	            }
	            // If connection is fatal (closed, reset, etc): -1, 'error'=0 otherwise (timeout, ...)
	            if (ERR_IS_FATAL(err)) {
	                //LOG_DBG( LOG_LEVEL_COMM, "lwip_read: ERROR->DISCONNECT\n");
	                return -1;
	            } else {
                    //LOG_DBG( LOG_LEVEL_COMM, "lwip_read: <ERROR>: %d\n", err);
	                return 0;
	            }
	        }

	        n->lastdata_p = inbuf_p;
	    }

	    p = (struct pbuf *)inbuf_p;
	    inbuflen = p->tot_len;
	    inbuflen -= n->lastoffset;

	    if ( len > inbuflen ) {
	        copylen = inbuflen;
	    } else {
	        copylen = (u16_t)len;
	    }

	    /* Copy the contents of the received buffer into the supplied buffer */
	    pbuf_copy_partial( p, (u8_t*)buffer + recvLen, copylen, n->lastoffset );
	    recvLen += copylen;

        // Check if done
        len -= copylen;
        if ( (len <= 0) || (p->flags & PBUF_FLAG_PUSH) ) {
            done = 1;
        }

        if ((inbuflen - copylen > 0)) {
          n->lastdata_p = inbuf_p;
          n->lastoffset += copylen;
        } else {
          n->lastdata_p = NULL;
          n->lastoffset = 0;
          pbuf_free((struct pbuf *)inbuf_p);
        }

	} while(!done && xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == pdFALSE);

    if (recvLen > 0) {
        /* update receive window */
        netconn_recved((struct netconn*)n->conn_p, (u32_t)recvLen);
    }

	return recvLen;
}


int MQTTFreeRTOS_lwIP_write(MQTT_Network* n, unsigned char* buffer, int len, int timeout_ms)
{
	TickType_t xTicksToWait = timeout_ms / portTICK_PERIOD_MS; /* convert milliseconds to ticks */
	TimeOut_t xTimeOut;
	int sentLen = 0;

	vTaskSetTimeOutState(&xTimeOut); /* Record the time at which this function was entered. */
	do
	{
	    size_t bytes_written;
		err_t  err;

		err = netconn_write_partly( (struct netconn*)n->conn_p, (void*)buffer, (size_t)len, NETCONN_NOFLAG, &bytes_written );
		if (err == ERR_OK) {
		    sentLen += bytes_written;
		} else {
		    sentLen = err;
		    break;
		}

	} while (sentLen < len && xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == pdFALSE);

	return sentLen;
}


void MQTTFreeRTOS_lwIP_disconnect(MQTT_Network* n)
{
//	FreeRTOS_closesocket(n->my_socket);
    netconn_disconnect( (struct netconn*)n->conn_p );
    netconn_delete( (struct netconn*)n->conn_p );
    n->conn_p = NULL;
}


void MQTTFreeRTOS_lwIP_NetworkInit(MQTT_Network* n)
{
	n->conn_p = NULL;
	n->mqttread = MQTTFreeRTOS_lwIP_read;
	n->mqttwrite = MQTTFreeRTOS_lwIP_write;
	n->disconnect = MQTTFreeRTOS_lwIP_disconnect;
	//n->errorhandler = NULL;
	n->lastdata_p = NULL;
	n->lastoffset = 0;
}


int MQTTFreeRTOS_lwIP_NetworkConnect(MQTT_Network* n, char* addr, int port)
{
    int retVal = -1;
#if 0
	struct freertos_sockaddr sAddr;
	int retVal = -1;
	uint32_t ipAddress;

	if ((ipAddress = FreeRTOS_gethostbyname(addr)) == 0)
		goto exit;

	sAddr.sin_port = FreeRTOS_htons(port);
	sAddr.sin_addr = ipAddress;

	if ((n->my_socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP)) < 0)
		goto exit;

	if ((retVal = FreeRTOS_connect(n->my_socket, &sAddr, sizeof(sAddr))) < 0)
	{
		FreeRTOS_closesocket(n->my_socket);
	    goto exit;
	}
#else
	err_t     err;
	ip_addr_t ipAddress;

    // TODO: Add MQTT Broker DNS lookup, use fixed IP for now

	ipAddress.addr = ipaddr_addr(addr);

    // Create a new TCP connection
    n->conn_p = netconn_new( NETCONN_TCP ); // TODO Check to use extended function with callback
    if (n->conn_p == NULL)
        goto exit;

    // Set connection parameters
    ((struct netconn*)n->conn_p)->recv_timeout = MQTTFREERTOS_LWIP_RECV_TIMEOUT; // Receive time-out on netconn_recv

    // Bind local IP/port
    err = netconn_bind( (struct netconn*)n->conn_p, IP_ADDR_ANY, 0 );
    if ( err != ERR_OK )
        goto exit;

    // Connect to remote IP/port
    err = netconn_connect( (struct netconn*)n->conn_p, &ipAddress, (u16_t)port );
    if ( err != ERR_OK )
        goto exit;
    else {
        // Notify lwIP that we want to partially read the buffers
        netconn_set_noautorecved( (struct netconn*)n->conn_p, 1 );
        retVal = 0;
    }
#endif

exit:
    if (retVal != 0) {
        // Remove netconn on error
        if (n->conn_p != NULL) {
            netconn_delete( (struct netconn*)n->conn_p );
            n->conn_p = NULL;
        }
        LOG_DBG( LOG_LEVEL_NET, "MQTT: NetworkConnect() failed\n");
    }
    return retVal;
}


#if 0
int MQTTFreeRTOS_lwIP_NetworkConnectTLS(MQTT_Network *n, char* addr, int port, SlSockSecureFiles_t* certificates, unsigned char sec_method, unsigned int cipher, char server_verify)
{
	SlSockAddrIn_t sAddr;
	int addrSize;
	int retVal;
	unsigned long ipAddress;

	retVal = sl_NetAppDnsGetHostByName(addr, strlen(addr), &ipAddress, AF_INET);
	if (retVal < 0) {
		return -1;
	}

	sAddr.sin_family = AF_INET;
	sAddr.sin_port = sl_Htons((unsigned short)port);
	sAddr.sin_addr.s_addr = sl_Htonl(ipAddress);

	addrSize = sizeof(SlSockAddrIn_t);

	n->my_socket = sl_Socket(SL_AF_INET, SL_SOCK_STREAM, SL_SEC_SOCKET);
	if (n->my_socket < 0) {
		return -1;
	}

	SlSockSecureMethod method;
	method.secureMethod = sec_method;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECMETHOD, &method, sizeof(method));
	if (retVal < 0) {
		return retVal;
	}

	SlSockSecureMask mask;
	mask.secureMask = cipher;
	retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_MASK, &mask, sizeof(mask));
	if (retVal < 0) {
		return retVal;
	}

	if (certificates != NULL) {
		retVal = sl_SetSockOpt(n->my_socket, SL_SOL_SOCKET, SL_SO_SECURE_FILES, certificates->secureFiles, sizeof(SlSockSecureFiles_t));
		if (retVal < 0)
		{
			return retVal;
		}
	}

	retVal = sl_Connect(n->my_socket, (SlSockAddr_t *)&sAddr, addrSize);
	if (retVal < 0) {
		if (server_verify || retVal != -453) {
			sl_Close(n->my_socket);
			return retVal;
		}
	}

	SysTickIntRegister(SysTickIntHandler);
	SysTickPeriodSet(80000);
	SysTickEnable();

	return retVal;
}
#endif

#endif // not configured


#ifdef __cplusplus
}
#endif