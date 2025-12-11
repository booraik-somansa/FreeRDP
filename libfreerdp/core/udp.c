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

#include <freerdp/config.h>

#include "udp.h"
#include "transport.h"

#include <winpr/crt.h>
#include <winpr/wlog.h>
#include <winpr/winsock.h>

#include <freerdp/log.h>

#define TAG FREERDP_TAG("core.udp")

/* Forward declarations for BIO methods */
static int udp_bio_write(BIO* bio, const char* buf, int size);
static int udp_bio_read(BIO* bio, char* buf, int size);
static int udp_bio_puts(BIO* bio, const char* str);
static int udp_bio_gets(BIO* bio, char* str, int size);
static long udp_bio_ctrl(BIO* bio, int cmd, long arg1, void* arg2);
static int udp_bio_new(BIO* bio);
static int udp_bio_free(BIO* bio);

static BIO_METHOD* bio_udp_socket_methods = NULL;

/**
 * Initialize UDP BIO methods
 */
static BIO_METHOD* udp_bio_methods_init(void)
{
	BIO_METHOD* methods = BIO_meth_new(BIO_TYPE_UDP, "udp_socket");
	if (!methods)
		return NULL;
	
	BIO_meth_set_write(methods, udp_bio_write);
	BIO_meth_set_read(methods, udp_bio_read);
	BIO_meth_set_puts(methods, udp_bio_puts);
	BIO_meth_set_gets(methods, udp_bio_gets);
	BIO_meth_set_ctrl(methods, udp_bio_ctrl);
	BIO_meth_set_create(methods, udp_bio_new);
	BIO_meth_set_destroy(methods, udp_bio_free);
	
	return methods;
}

BIO_METHOD* BIO_s_udp_socket(void)
{
	if (!bio_udp_socket_methods)
		bio_udp_socket_methods = udp_bio_methods_init();
	return bio_udp_socket_methods;
}

static int udp_bio_new(BIO* bio)
{
	BIO_set_init(bio, 1);
	BIO_set_data(bio, NULL);
	BIO_set_flags(bio, 0);
	return 1;
}

static int udp_bio_free(BIO* bio)
{
	if (!bio)
		return 0;
	
	BIO_set_data(bio, NULL);
	BIO_set_init(bio, 0);
	BIO_set_flags(bio, 0);
	return 1;
}

static int udp_bio_write(BIO* bio, const char* buf, int size)
{
	rdpUdpTransport* udp = (rdpUdpTransport*)BIO_get_data(bio);
	int status;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	BIO_clear_retry_flags(bio);
	
	if (udp->connected)
	{
		status = send(udp->sockfd, buf, size, 0);
	}
	else
	{
		status = sendto(udp->sockfd, buf, size, 0,
		                (struct sockaddr*)&udp->peerAddr, udp->peerAddrLen);
	}
	
	if (status < 0)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
		{
			BIO_set_retry_write(bio);
			return -1;
		}
		return -1;
	}
	
	udp->bytesSent += status;
	udp->packetsSent++;
	
	return status;
}

static int udp_bio_read(BIO* bio, char* buf, int size)
{
	rdpUdpTransport* udp = (rdpUdpTransport*)BIO_get_data(bio);
	int status;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	BIO_clear_retry_flags(bio);
	
	if (udp->connected)
	{
		status = recv(udp->sockfd, buf, size, 0);
	}
	else
	{
		socklen_t addrLen = sizeof(udp->peerAddr);
		status = recvfrom(udp->sockfd, buf, size, 0,
		                  (struct sockaddr*)&udp->peerAddr, &addrLen);
		udp->peerAddrLen = addrLen;
	}
	
	if (status < 0)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
		{
			BIO_set_retry_read(bio);
			return -1;
		}
		return -1;
	}
	
	if (status > 0)
	{
		udp->bytesReceived += status;
		udp->packetsReceived++;
	}
	
	return status;
}

static int udp_bio_puts(BIO* bio, const char* str)
{
	return udp_bio_write(bio, str, strlen(str));
}

static int udp_bio_gets(BIO* bio, char* str, int size)
{
	return -1; /* Not supported for UDP */
}

static long udp_bio_ctrl(BIO* bio, int cmd, long arg1, void* arg2)
{
	rdpUdpTransport* udp = (rdpUdpTransport*)BIO_get_data(bio);
	int status = 0;
	
	switch (cmd)
	{
		case BIO_C_SET_UDP_SOCKET:
			if (udp)
				udp->sockfd = (SOCKET)(intptr_t)arg2;
			return 1;
		
		case BIO_C_GET_UDP_SOCKET:
			if (udp && arg2)
				*(SOCKET*)arg2 = udp->sockfd;
			return 1;
		
		case BIO_C_SET_NONBLOCK:
			if (udp)
				return freerdp_udp_set_nonblocking(udp, arg1 != 0) ? 1 : 0;
			return 0;
		
		case BIO_CTRL_GET_CLOSE:
			return BIO_get_shutdown(bio);
		
		case BIO_CTRL_SET_CLOSE:
			BIO_set_shutdown(bio, (int)arg1);
			return 1;
		
		case BIO_CTRL_FLUSH:
			return 1;
		
		default:
			return 0;
	}
}

rdpUdpTransport* freerdp_udp_transport_new(wLog* log)
{
	rdpUdpTransport* udp = (rdpUdpTransport*)calloc(1, sizeof(rdpUdpTransport));
	if (!udp)
		return NULL;
	
	udp->sockfd = INVALID_SOCKET;
	udp->connected = FALSE;
	udp->nonBlocking = FALSE;
	udp->flags = 0;
	
	udp->recvBufferSize = UDP_DEFAULT_RECV_BUFFER_SIZE;
	udp->sendBufferSize = UDP_DEFAULT_SEND_BUFFER_SIZE;
	udp->recvTimeout = UDP_DEFAULT_TIMEOUT_MS;
	udp->sendTimeout = UDP_DEFAULT_TIMEOUT_MS;
	
	udp->bytesSent = 0;
	udp->bytesReceived = 0;
	udp->packetsSent = 0;
	udp->packetsReceived = 0;
	
	udp->event = NULL;
	udp->bio = NULL;
	udp->log = log ? log : WLog_Get(TAG);
	
	return udp;
}

void freerdp_udp_transport_free(rdpUdpTransport* udp)
{
	if (!udp)
		return;
	
	freerdp_udp_disconnect(udp);
	
	if (udp->bio)
	{
		BIO_free_all(udp->bio);
		udp->bio = NULL;
	}
	
	if (udp->event)
	{
		CloseHandle(udp->event);
		udp->event = NULL;
	}
	
	free(udp);
}

BOOL freerdp_udp_connect(rdpUdpTransport* udp, const char* hostname, UINT16 port, DWORD timeout)
{
	struct addrinfo hints;
	struct addrinfo* result = NULL;
	struct addrinfo* ptr = NULL;
	char portStr[16];
	int status;
	
	if (!udp || !hostname)
		return FALSE;
	
	if (udp->sockfd != INVALID_SOCKET)
	{
		WLog_Print(udp->log, WLOG_WARN, "UDP transport already connected");
		return FALSE;
	}
	
	snprintf(portStr, sizeof(portStr), "%u", port);
	
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	
	status = getaddrinfo(hostname, portStr, &hints, &result);
	if (status != 0)
	{
		WLog_Print(udp->log, WLOG_ERROR, "getaddrinfo failed: %d", status);
		return FALSE;
	}
	
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		udp->sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (udp->sockfd == INVALID_SOCKET)
			continue;
		
		/* Store peer address */
		memcpy(&udp->peerAddr, ptr->ai_addr, ptr->ai_addrlen);
		udp->peerAddrLen = (socklen_t)ptr->ai_addrlen;
		
		/* Connect the socket for connected UDP */
		if (connect(udp->sockfd, ptr->ai_addr, (int)ptr->ai_addrlen) == 0)
		{
			udp->connected = TRUE;
			break;
		}
		
		closesocket(udp->sockfd);
		udp->sockfd = INVALID_SOCKET;
	}
	
	freeaddrinfo(result);
	
	if (udp->sockfd == INVALID_SOCKET)
	{
		WLog_Print(udp->log, WLOG_ERROR, "Failed to connect UDP socket to %s:%u", hostname, port);
		return FALSE;
	}
	
	/* Apply buffer sizes */
	freerdp_udp_set_recv_buffer_size(udp, udp->recvBufferSize);
	freerdp_udp_set_send_buffer_size(udp, udp->sendBufferSize);
	
	/* Create event handle */
#ifdef _WIN32
	udp->event = WSACreateEvent();
	if (udp->event)
		WSAEventSelect(udp->sockfd, udp->event, FD_READ | FD_WRITE | FD_CLOSE);
#else
	udp->event = CreateFileDescriptorEvent(NULL, FALSE, FALSE, udp->sockfd, WINPR_FD_READ);
#endif
	
	/* Get local address */
	udp->localAddrLen = sizeof(udp->localAddr);
	getsockname(udp->sockfd, (struct sockaddr*)&udp->localAddr, &udp->localAddrLen);
	
	udp->flags |= UDP_FLAG_CONNECTED;
	
	WLog_Print(udp->log, WLOG_DEBUG, "UDP connected to %s:%u", hostname, port);
	
	return TRUE;
}

BOOL freerdp_udp_bind(rdpUdpTransport* udp, const char* hostname, UINT16 port)
{
	struct addrinfo hints;
	struct addrinfo* result = NULL;
	struct addrinfo* ptr = NULL;
	char portStr[16];
	int status;
	int opt = 1;
	
	if (!udp)
		return FALSE;
	
	if (udp->sockfd != INVALID_SOCKET)
	{
		WLog_Print(udp->log, WLOG_WARN, "UDP transport already has socket");
		return FALSE;
	}
	
	snprintf(portStr, sizeof(portStr), "%u", port);
	
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;
	
	status = getaddrinfo(hostname, portStr, &hints, &result);
	if (status != 0)
	{
		WLog_Print(udp->log, WLOG_ERROR, "getaddrinfo failed: %d", status);
		return FALSE;
	}
	
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
	{
		udp->sockfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (udp->sockfd == INVALID_SOCKET)
			continue;
		
		/* Allow address reuse */
		setsockopt(udp->sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
		
		if (bind(udp->sockfd, ptr->ai_addr, (int)ptr->ai_addrlen) == 0)
		{
			memcpy(&udp->localAddr, ptr->ai_addr, ptr->ai_addrlen);
			udp->localAddrLen = (socklen_t)ptr->ai_addrlen;
			break;
		}
		
		closesocket(udp->sockfd);
		udp->sockfd = INVALID_SOCKET;
	}
	
	freeaddrinfo(result);
	
	if (udp->sockfd == INVALID_SOCKET)
	{
		WLog_Print(udp->log, WLOG_ERROR, "Failed to bind UDP socket to %s:%u", 
		           hostname ? hostname : "*", port);
		return FALSE;
	}
	
	/* Apply buffer sizes */
	freerdp_udp_set_recv_buffer_size(udp, udp->recvBufferSize);
	freerdp_udp_set_send_buffer_size(udp, udp->sendBufferSize);
	
	/* Create event handle */
#ifdef _WIN32
	udp->event = WSACreateEvent();
	if (udp->event)
		WSAEventSelect(udp->sockfd, udp->event, FD_READ | FD_WRITE | FD_CLOSE);
#else
	udp->event = CreateFileDescriptorEvent(NULL, FALSE, FALSE, udp->sockfd, WINPR_FD_READ);
#endif
	
	WLog_Print(udp->log, WLOG_DEBUG, "UDP bound to %s:%u", hostname ? hostname : "*", port);
	
	return TRUE;
}

BOOL freerdp_udp_disconnect(rdpUdpTransport* udp)
{
	if (!udp)
		return FALSE;
	
	if (udp->sockfd != INVALID_SOCKET)
	{
#ifdef _WIN32
		if (udp->event)
		{
			WSAEventSelect(udp->sockfd, udp->event, 0);
			WSACloseEvent(udp->event);
			udp->event = NULL;
		}
#endif
		closesocket(udp->sockfd);
		udp->sockfd = INVALID_SOCKET;
	}
	
	udp->connected = FALSE;
	udp->flags &= ~UDP_FLAG_CONNECTED;
	
	ZeroMemory(&udp->peerAddr, sizeof(udp->peerAddr));
	udp->peerAddrLen = 0;
	ZeroMemory(&udp->localAddr, sizeof(udp->localAddr));
	udp->localAddrLen = 0;
	
	return TRUE;
}

int freerdp_udp_send(rdpUdpTransport* udp, const BYTE* data, size_t length)
{
	int status;
	
	if (!udp || !data || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	if (length > UDP_MAX_DATAGRAM_SIZE)
	{
		WLog_Print(udp->log, WLOG_WARN, "Datagram too large: %zu > %d", 
		           length, UDP_MAX_DATAGRAM_SIZE);
		return -1;
	}
	
	if (udp->connected)
	{
		status = send(udp->sockfd, (const char*)data, (int)length, 0);
	}
	else
	{
		status = sendto(udp->sockfd, (const char*)data, (int)length, 0,
		                (struct sockaddr*)&udp->peerAddr, udp->peerAddrLen);
	}
	
	if (status > 0)
	{
		udp->bytesSent += status;
		udp->packetsSent++;
	}
	else if (status < 0)
	{
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
		{
			WLog_Print(udp->log, WLOG_ERROR, "UDP send failed: %d", err);
		}
	}
	
	return status;
}

int freerdp_udp_recv(rdpUdpTransport* udp, BYTE* data, size_t length)
{
	int status;
	
	if (!udp || !data || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	if (udp->connected)
	{
		status = recv(udp->sockfd, (char*)data, (int)length, 0);
	}
	else
	{
		socklen_t addrLen = sizeof(udp->peerAddr);
		status = recvfrom(udp->sockfd, (char*)data, (int)length, 0,
		                  (struct sockaddr*)&udp->peerAddr, &addrLen);
		udp->peerAddrLen = addrLen;
	}
	
	if (status > 0)
	{
		udp->bytesReceived += status;
		udp->packetsReceived++;
	}
	else if (status < 0)
	{
		int err = WSAGetLastError();
		if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
		{
			WLog_Print(udp->log, WLOG_ERROR, "UDP recv failed: %d", err);
		}
	}
	
	return status;
}

int freerdp_udp_sendto(rdpUdpTransport* udp, const BYTE* data, size_t length,
                       const struct sockaddr* addr, socklen_t addrLen)
{
	int status;
	
	if (!udp || !data || !addr || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	if (length > UDP_MAX_DATAGRAM_SIZE)
	{
		WLog_Print(udp->log, WLOG_WARN, "Datagram too large: %zu > %d", 
		           length, UDP_MAX_DATAGRAM_SIZE);
		return -1;
	}
	
	status = sendto(udp->sockfd, (const char*)data, (int)length, 0, addr, addrLen);
	
	if (status > 0)
	{
		udp->bytesSent += status;
		udp->packetsSent++;
	}
	
	return status;
}

int freerdp_udp_recvfrom(rdpUdpTransport* udp, BYTE* data, size_t length,
                         struct sockaddr* addr, socklen_t* addrLen)
{
	int status;
	
	if (!udp || !data || !addr || !addrLen || udp->sockfd == INVALID_SOCKET)
		return -1;
	
	status = recvfrom(udp->sockfd, (char*)data, (int)length, 0, addr, addrLen);
	
	if (status > 0)
	{
		udp->bytesReceived += status;
		udp->packetsReceived++;
	}
	
	return status;
}

BOOL freerdp_udp_set_nonblocking(rdpUdpTransport* udp, BOOL nonBlocking)
{
	u_long mode;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	mode = nonBlocking ? 1 : 0;
	
	if (ioctlsocket(udp->sockfd, FIONBIO, &mode) != 0)
	{
		WLog_Print(udp->log, WLOG_ERROR, "Failed to set non-blocking mode");
		return FALSE;
	}
	
	udp->nonBlocking = nonBlocking;
	
	if (nonBlocking)
		udp->flags |= UDP_FLAG_NONBLOCKING;
	else
		udp->flags &= ~UDP_FLAG_NONBLOCKING;
	
	return TRUE;
}

BOOL freerdp_udp_set_recv_buffer_size(rdpUdpTransport* udp, size_t size)
{
	int opt = (int)size;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	if (setsockopt(udp->sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&opt, sizeof(opt)) != 0)
	{
		WLog_Print(udp->log, WLOG_WARN, "Failed to set receive buffer size to %zu", size);
		return FALSE;
	}
	
	udp->recvBufferSize = size;
	return TRUE;
}

BOOL freerdp_udp_set_send_buffer_size(rdpUdpTransport* udp, size_t size)
{
	int opt = (int)size;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	if (setsockopt(udp->sockfd, SOL_SOCKET, SO_SNDBUF, (const char*)&opt, sizeof(opt)) != 0)
	{
		WLog_Print(udp->log, WLOG_WARN, "Failed to set send buffer size to %zu", size);
		return FALSE;
	}
	
	udp->sendBufferSize = size;
	return TRUE;
}

BOOL freerdp_udp_set_recv_timeout(rdpUdpTransport* udp, DWORD timeout)
{
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
#ifdef _WIN32
	{
		DWORD tv = timeout;
		if (setsockopt(udp->sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) != 0)
			return FALSE;
	}
#else
	{
		struct timeval tv;
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		if (setsockopt(udp->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
			return FALSE;
	}
#endif
	
	udp->recvTimeout = timeout;
	return TRUE;
}

BOOL freerdp_udp_set_send_timeout(rdpUdpTransport* udp, DWORD timeout)
{
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
#ifdef _WIN32
	{
		DWORD tv = timeout;
		if (setsockopt(udp->sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) != 0)
			return FALSE;
	}
#else
	{
		struct timeval tv;
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		if (setsockopt(udp->sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0)
			return FALSE;
	}
#endif
	
	udp->sendTimeout = timeout;
	return TRUE;
}

SOCKET freerdp_udp_get_socket(rdpUdpTransport* udp)
{
	if (!udp)
		return INVALID_SOCKET;
	return udp->sockfd;
}

HANDLE freerdp_udp_get_event(rdpUdpTransport* udp)
{
	if (!udp)
		return NULL;
	return udp->event;
}

BOOL freerdp_udp_wait_read(rdpUdpTransport* udp, DWORD timeout)
{
	fd_set readfds;
	struct timeval tv;
	int status;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	FD_ZERO(&readfds);
	FD_SET(udp->sockfd, &readfds);
	
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	
	status = select((int)udp->sockfd + 1, &readfds, NULL, NULL, &tv);
	
	return (status > 0 && FD_ISSET(udp->sockfd, &readfds));
}

BOOL freerdp_udp_wait_write(rdpUdpTransport* udp, DWORD timeout)
{
	fd_set writefds;
	struct timeval tv;
	int status;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	FD_ZERO(&writefds);
	FD_SET(udp->sockfd, &writefds);
	
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;
	
	status = select((int)udp->sockfd + 1, NULL, &writefds, NULL, &tv);
	
	return (status > 0 && FD_ISSET(udp->sockfd, &writefds));
}

void freerdp_udp_get_stats(rdpUdpTransport* udp, UINT64* bytesSent,
                           UINT64* bytesReceived, UINT64* packetsSent,
                           UINT64* packetsReceived)
{
	if (!udp)
		return;
	
	if (bytesSent)
		*bytesSent = udp->bytesSent;
	if (bytesReceived)
		*bytesReceived = udp->bytesReceived;
	if (packetsSent)
		*packetsSent = udp->packetsSent;
	if (packetsReceived)
		*packetsReceived = udp->packetsReceived;
}

void freerdp_udp_reset_stats(rdpUdpTransport* udp)
{
	if (!udp)
		return;
	
	udp->bytesSent = 0;
	udp->bytesReceived = 0;
	udp->packetsSent = 0;
	udp->packetsReceived = 0;
}

BOOL freerdp_udp_set_broadcast(rdpUdpTransport* udp, BOOL enable)
{
	int opt = enable ? 1 : 0;
	
	if (!udp || udp->sockfd == INVALID_SOCKET)
		return FALSE;
	
	if (setsockopt(udp->sockfd, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt)) != 0)
	{
		WLog_Print(udp->log, WLOG_ERROR, "Failed to set broadcast mode");
		return FALSE;
	}
	
	if (enable)
		udp->flags |= UDP_FLAG_BROADCAST;
	else
		udp->flags &= ~UDP_FLAG_BROADCAST;
	
	return TRUE;
}

char* freerdp_udp_get_peer_address(rdpUdpTransport* udp)
{
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];
	char* result;
	size_t len;
	
	if (!udp || udp->peerAddrLen == 0)
		return NULL;
	
	if (getnameinfo((struct sockaddr*)&udp->peerAddr, udp->peerAddrLen,
	                host, sizeof(host), serv, sizeof(serv),
	                NI_NUMERICHOST | NI_NUMERICSERV) != 0)
	{
		return NULL;
	}
	
	len = strlen(host) + strlen(serv) + 2;
	result = (char*)malloc(len);
	if (!result)
		return NULL;
	
	snprintf(result, len, "%s:%s", host, serv);
	return result;
}

char* freerdp_udp_get_local_address(rdpUdpTransport* udp)
{
	char host[NI_MAXHOST];
	char serv[NI_MAXSERV];
	char* result;
	size_t len;
	
	if (!udp || udp->localAddrLen == 0)
		return NULL;
	
	if (getnameinfo((struct sockaddr*)&udp->localAddr, udp->localAddrLen,
	                host, sizeof(host), serv, sizeof(serv),
	                NI_NUMERICHOST | NI_NUMERICSERV) != 0)
	{
		return NULL;
	}
	
	len = strlen(host) + strlen(serv) + 2;
	result = (char*)malloc(len);
	if (!result)
		return NULL;
	
	snprintf(result, len, "%s:%s", host, serv);
	return result;
}
