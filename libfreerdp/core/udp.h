/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * User Datagram Protocol (UDP)
 *
 * Copyright 2024 FreeRDP Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FREERDP_LIB_CORE_UDP_H
#define FREERDP_LIB_CORE_UDP_H

#include <winpr/windows.h>

#include <freerdp/types.h>
#include <freerdp/settings.h>
#include <freerdp/freerdp.h>
#include <freerdp/api.h>
#include <freerdp/transport_io.h>

#include <winpr/crt.h>
#include <winpr/synch.h>
#include <winpr/stream.h>
#include <winpr/winsock.h>

#include <openssl/bio.h>

/* UDP Transport Configuration */
#define UDP_DEFAULT_RECV_BUFFER_SIZE 65536
#define UDP_DEFAULT_SEND_BUFFER_SIZE 65536
#define UDP_MAX_DATAGRAM_SIZE 65507
#define UDP_DEFAULT_TIMEOUT_MS 5000

/* UDP BIO type identifier */
#define BIO_TYPE_UDP 70

/* UDP BIO control commands */
#define BIO_C_SET_UDP_SOCKET 1201
#define BIO_C_GET_UDP_SOCKET 1202
#define BIO_C_SET_UDP_PEER 1203
#define BIO_C_GET_UDP_PEER 1204
#define BIO_C_SET_UDP_TIMEOUT 1205
#define BIO_C_GET_UDP_TIMEOUT 1206

/* UDP Transport Flags */
#define UDP_FLAG_NONBLOCKING 0x0001
#define UDP_FLAG_CONNECTED 0x0002
#define UDP_FLAG_BROADCAST 0x0004
#define UDP_FLAG_MULTICAST 0x0008

/**
 * UDP Transport Context
 */
typedef struct rdp_udp_transport rdpUdpTransport;

struct rdp_udp_transport
{
	SOCKET sockfd;
	BOOL connected;
	BOOL nonBlocking;
	DWORD flags;
	
	/* Peer address information */
	struct sockaddr_storage peerAddr;
	socklen_t peerAddrLen;
	
	/* Local address information */
	struct sockaddr_storage localAddr;
	socklen_t localAddrLen;
	
	/* Buffer configuration */
	size_t recvBufferSize;
	size_t sendBufferSize;
	
	/* Timeout configuration (milliseconds) */
	DWORD recvTimeout;
	DWORD sendTimeout;
	
	/* Statistics */
	UINT64 bytesSent;
	UINT64 bytesReceived;
	UINT64 packetsSent;
	UINT64 packetsReceived;
	
	/* Event handle for async operations */
	HANDLE event;
	
	/* OpenSSL BIO */
	BIO* bio;
	
	/* Logging */
	wLog* log;
};

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Create a new UDP transport instance
 * 
 * @param log Logger instance (can be NULL)
 * @return New UDP transport instance or NULL on failure
 */
FREERDP_LOCAL rdpUdpTransport* freerdp_udp_transport_new(wLog* log);

/**
 * Free a UDP transport instance
 * 
 * @param udp UDP transport instance to free
 */
FREERDP_LOCAL void freerdp_udp_transport_free(rdpUdpTransport* udp);

/**
 * Connect UDP transport to a remote host
 * 
 * @param udp UDP transport instance
 * @param hostname Remote hostname or IP address
 * @param port Remote port number
 * @param timeout Connection timeout in milliseconds (0 for default)
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_connect(rdpUdpTransport* udp, const char* hostname, 
                                       UINT16 port, DWORD timeout);

/**
 * Bind UDP transport to a local address
 * 
 * @param udp UDP transport instance
 * @param hostname Local hostname or IP address (NULL for any)
 * @param port Local port number (0 for any available port)
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_bind(rdpUdpTransport* udp, const char* hostname, UINT16 port);

/**
 * Disconnect UDP transport
 * 
 * @param udp UDP transport instance
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_disconnect(rdpUdpTransport* udp);

/**
 * Send data over UDP transport
 * 
 * @param udp UDP transport instance
 * @param data Data buffer to send
 * @param length Length of data to send
 * @return Number of bytes sent, or -1 on error
 */
FREERDP_LOCAL int freerdp_udp_send(rdpUdpTransport* udp, const BYTE* data, size_t length);

/**
 * Receive data from UDP transport
 * 
 * @param udp UDP transport instance
 * @param data Buffer to receive data into
 * @param length Maximum length of data to receive
 * @return Number of bytes received, 0 if no data available, or -1 on error
 */
FREERDP_LOCAL int freerdp_udp_recv(rdpUdpTransport* udp, BYTE* data, size_t length);

/**
 * Send data to a specific address (for unconnected sockets)
 * 
 * @param udp UDP transport instance
 * @param data Data buffer to send
 * @param length Length of data to send
 * @param addr Destination address
 * @param addrLen Length of destination address
 * @return Number of bytes sent, or -1 on error
 */
FREERDP_LOCAL int freerdp_udp_sendto(rdpUdpTransport* udp, const BYTE* data, size_t length,
                                     const struct sockaddr* addr, socklen_t addrLen);

/**
 * Receive data with source address information
 * 
 * @param udp UDP transport instance
 * @param data Buffer to receive data into
 * @param length Maximum length of data to receive
 * @param addr Buffer to receive source address
 * @param addrLen Pointer to address length (in/out)
 * @return Number of bytes received, 0 if no data available, or -1 on error
 */
FREERDP_LOCAL int freerdp_udp_recvfrom(rdpUdpTransport* udp, BYTE* data, size_t length,
                                       struct sockaddr* addr, socklen_t* addrLen);

/**
 * Set non-blocking mode for UDP transport
 * 
 * @param udp UDP transport instance
 * @param nonBlocking TRUE for non-blocking, FALSE for blocking
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_nonblocking(rdpUdpTransport* udp, BOOL nonBlocking);

/**
 * Set receive buffer size
 * 
 * @param udp UDP transport instance
 * @param size Buffer size in bytes
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_recv_buffer_size(rdpUdpTransport* udp, size_t size);

/**
 * Set send buffer size
 * 
 * @param udp UDP transport instance
 * @param size Buffer size in bytes
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_send_buffer_size(rdpUdpTransport* udp, size_t size);

/**
 * Set receive timeout
 * 
 * @param udp UDP transport instance
 * @param timeout Timeout in milliseconds
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_recv_timeout(rdpUdpTransport* udp, DWORD timeout);

/**
 * Set send timeout
 * 
 * @param udp UDP transport instance
 * @param timeout Timeout in milliseconds
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_send_timeout(rdpUdpTransport* udp, DWORD timeout);

/**
 * Get the UDP socket handle
 * 
 * @param udp UDP transport instance
 * @return Socket handle or INVALID_SOCKET on error
 */
FREERDP_LOCAL SOCKET freerdp_udp_get_socket(rdpUdpTransport* udp);

/**
 * Get the event handle for async operations
 * 
 * @param udp UDP transport instance
 * @return Event handle or NULL on error
 */
FREERDP_LOCAL HANDLE freerdp_udp_get_event(rdpUdpTransport* udp);

/**
 * Wait for UDP data to be available
 * 
 * @param udp UDP transport instance
 * @param timeout Timeout in milliseconds
 * @return TRUE if data is available, FALSE on timeout or error
 */
FREERDP_LOCAL BOOL freerdp_udp_wait_read(rdpUdpTransport* udp, DWORD timeout);

/**
 * Wait for UDP socket to be writable
 * 
 * @param udp UDP transport instance
 * @param timeout Timeout in milliseconds
 * @return TRUE if socket is writable, FALSE on timeout or error
 */
FREERDP_LOCAL BOOL freerdp_udp_wait_write(rdpUdpTransport* udp, DWORD timeout);

/**
 * Get transport statistics
 * 
 * @param udp UDP transport instance
 * @param bytesSent Pointer to receive bytes sent (can be NULL)
 * @param bytesReceived Pointer to receive bytes received (can be NULL)
 * @param packetsSent Pointer to receive packets sent (can be NULL)
 * @param packetsReceived Pointer to receive packets received (can be NULL)
 */
FREERDP_LOCAL void freerdp_udp_get_stats(rdpUdpTransport* udp, UINT64* bytesSent,
                                         UINT64* bytesReceived, UINT64* packetsSent,
                                         UINT64* packetsReceived);

/**
 * Reset transport statistics
 * 
 * @param udp UDP transport instance
 */
FREERDP_LOCAL void freerdp_udp_reset_stats(rdpUdpTransport* udp);

/**
 * Enable broadcast mode for UDP transport
 * 
 * @param udp UDP transport instance
 * @param enable TRUE to enable, FALSE to disable
 * @return TRUE on success, FALSE on failure
 */
FREERDP_LOCAL BOOL freerdp_udp_set_broadcast(rdpUdpTransport* udp, BOOL enable);

/**
 * Get the OpenSSL BIO for UDP transport
 * 
 * @return BIO method for UDP sockets
 */
FREERDP_LOCAL BIO_METHOD* BIO_s_udp_socket(void);

/**
 * Create a transport layer for UDP connection
 * 
 * @param context RDP context
 * @param hostname Remote hostname
 * @param port Remote port
 * @param timeout Connection timeout
 * @return Transport layer or NULL on failure
 */
FREERDP_LOCAL rdpTransportLayer* freerdp_udp_connect_layer(rdpContext* context, 
                                                           const char* hostname,
                                                           int port, DWORD timeout);

/**
 * Get peer address as string
 * 
 * @param udp UDP transport instance
 * @return Allocated string with peer address or NULL on error (caller must free)
 */
FREERDP_LOCAL char* freerdp_udp_get_peer_address(rdpUdpTransport* udp);

/**
 * Get local address as string
 * 
 * @param udp UDP transport instance
 * @return Allocated string with local address or NULL on error (caller must free)
 */
FREERDP_LOCAL char* freerdp_udp_get_local_address(rdpUdpTransport* udp);

#ifdef __cplusplus
}
#endif

#endif /* FREERDP_LIB_CORE_UDP_H */
