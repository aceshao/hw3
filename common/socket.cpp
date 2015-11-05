#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include  "socket.h"

Socket::Socket(const char* ip, int port, SocketType type)
{
    m_pIp = ip;
    m_iPort = port;
    m_nSocketType = type;
    m_iSocket= -1;
}

Socket::~Socket()
{

}

int Socket::Create()
{
    if (m_nSocketType == ST_TCP)
    {
        m_iSocket = socket(PF_INET, SOCK_STREAM, 0);
        if (m_iSocket == -1)
            return -1;
    }
    else if (m_nSocketType == ST_UDP)
    {
        m_iSocket = socket(PF_INET, SOCK_DGRAM, 0);
        if (m_iSocket == -1)
            return -1;
    }
    else
        return -1;

    return 0;
}

int Socket::Bind()
{
    struct sockaddr_in bindSockAddr;
    bzero(&bindSockAddr, sizeof(bindSockAddr));
    bindSockAddr.sin_family = AF_INET;
    bindSockAddr.sin_addr.s_addr = inet_addr(m_pIp);
    bindSockAddr.sin_port = htons(m_iPort);
    return bind(m_iSocket, (sockaddr*)&bindSockAddr, sizeof(bindSockAddr));
}

int Socket::Connect()
{
    struct sockaddr_in serverSockAddr;
    bzero(&serverSockAddr, sizeof(serverSockAddr));
    serverSockAddr.sin_family = AF_INET;
    serverSockAddr.sin_addr.s_addr = inet_addr(m_pIp);
    serverSockAddr.sin_port = htons(m_iPort);
    return connect(m_iSocket, (sockaddr*)&serverSockAddr, sizeof(serverSockAddr));
}

int Socket::Listen(int backlog)
{
    return listen(m_iSocket, backlog);
}

int Socket::Accept(Socket* client)
{
    struct sockaddr_in connectSockAddr;
    bzero(&connectSockAddr, sizeof(connectSockAddr));
    socklen_t len = sizeof(connectSockAddr);
    int fd = -1;
    while(1)
    {
        fd = accept(m_iSocket, (sockaddr*)&connectSockAddr, &len);
        if (fd < 0)
        {
            if(errno == EINTR)
                continue;
            return -1;
        }
        break;
    }

    client->SetSocket(fd);
    return 0;
}

int Socket::Send(const void* buffer, unsigned int size)
{
    return send(m_iSocket, buffer, size, 0);
}

int Socket::Recv(const void* buffer, unsigned int size)
{
    return recv(m_iSocket, (char*)buffer, size, 0);
}

int Socket:: GetPeerName(struct sockaddr *name, socklen_t *namelen)
{
    return getpeername(m_iSocket, name, namelen);
}

int Socket::Close()
{
    return close(m_iSocket);
}

string Socket::GetIp()
{
    struct sockaddr_in clientaddr;
    bzero(&clientaddr, sizeof(clientaddr));
    socklen_t len = sizeof(clientaddr);
    GetPeerName((struct sockaddr*) &clientaddr, &len);
    return inet_ntoa(clientaddr.sin_addr);
}

int Socket::GetPort()
{
    struct sockaddr_in clientaddr;
    bzero(&clientaddr, sizeof(clientaddr));
    socklen_t len = sizeof(clientaddr);
    GetPeerName((struct sockaddr*) &clientaddr, &len);
    return ntohs(clientaddr.sin_port);;
}

int Socket:: SetSockOpt(int level, int optname, const void *optval, socklen_t optlen)
{
	return setsockopt(m_iSocket, level, optname, optval, optlen);
}

int Socket::SetSockAddressReuse(bool reuse)
{
	int optval = 0;
	if(reuse)
	{
		optval = 1;
	}
	return SetSockOpt(SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);	
}

int Socket:: RecvExact(void *pBuffer, size_t nBytes, struct timeval *timeout)
{
	int iNum, nRecvBytes=0;
	char *pRecvBuf= (char*)pBuffer;

	if ( nBytes<=0 ) return 0;

	while (nRecvBytes < (int)nBytes)
	{
		iNum= SelectRead (timeout);

		if (iNum < 0) { // on error
			return iNum;
		}
		else if (iNum == 0) { // timeout
			break;
		}

		iNum = Recv((char*)pRecvBuf, nBytes);
		if (iNum > 0) {
			nRecvBytes += iNum;
			pRecvBuf += iNum;
		}
		else if (iNum == 0) { // connection closed
			break;
		}
		else
		{ // onerror
			if (errno == EWOULDBLOCK)
			{ // timeout
				break;
			}
		}
	}
	return nRecvBytes;
}


int Socket:: SelectRead (struct timeval* timeout)
{
	fd_set recv_fds;
	int iNum= 0;

	if (m_iSocket <0) return -1;

	
	FD_ZERO( &recv_fds );
	FD_SET( m_iSocket, &recv_fds );
	
	iNum=select( m_iSocket+1, &recv_fds, NULL, NULL, timeout );	
	return iNum;
}
