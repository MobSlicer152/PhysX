// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Copyright (c) 2008-2023 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#include "foundation/PxPreprocessor.h"

#if !PX_PSP
#include "foundation/PxIntrinsics.h"
#include "foundation/PxMathIntrinsics.h"
#include "foundation/PxSocket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define INVALID_SOCKET -1

#ifndef SOMAXCONN
#define SOMAXCONN 5
#endif

namespace physx
{

const PxU32 PxSocket::DEFAULT_BUFFER_SIZE = 32768;

class SocketImpl
{
  public:
	SocketImpl(bool isBlocking);
	virtual ~SocketImpl();

	bool connect(const char* host, uint16_t port, PxU32 timeout);
	bool listen(uint16_t port);
	bool accept(bool block);
	void disconnect();

	void setBlocking(bool blocking);

	virtual PxU32 write(const uint8_t* data, PxU32 length);
	virtual bool flush();
	PxU32 read(uint8_t* data, PxU32 length);

	PX_FORCE_INLINE bool isBlocking() const
	{
		return mIsBlocking;
	}
	PX_FORCE_INLINE bool isConnected() const
	{
		return mIsConnected;
	}
	PX_FORCE_INLINE const char* getHost() const
	{
		return mHost;
	}
	PX_FORCE_INLINE uint16_t getPort() const
	{
		return mPort;
	}

  protected:
	bool nonBlockingTimeout() const;

	PxI32 mSocket;
	PxI32 mListenSocket;
	const char* mHost;
	uint16_t mPort;
	bool mIsConnected;
	bool mIsBlocking;
	bool mListenMode;
};

void socketSetBlockingInternal(PxI32 socket, bool blocking);

SocketImpl::SocketImpl(bool isBlocking)
: mSocket(INVALID_SOCKET)
, mListenSocket(INVALID_SOCKET)
, mHost(NULL)
, mPort(0)
, mIsConnected(false)
, mIsBlocking(isBlocking)
, mListenMode(false)
{
}

SocketImpl::~SocketImpl()
{
}

bool SocketImpl::connect(const char* host, uint16_t port, PxU32 timeout)
{
	sockaddr_in socketAddress;
	intrinsics::memSet(&socketAddress, 0, sizeof(sockaddr_in));
	socketAddress.sin_family = AF_INET;
	socketAddress.sin_port = htons(port);

	// get host
	hostent* hp = gethostbyname(host);
	if(!hp)
	{
		in_addr a;
		a.s_addr = inet_addr(host);
		hp = gethostbyaddr(reinterpret_cast<const char*>(&a), sizeof(in_addr), AF_INET);
		if(!hp)
			return false;
	}
	intrinsics::memCopy(&socketAddress.sin_addr, hp->h_addr_list[0], hp->h_length);

	// connect
	mSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(mSocket == INVALID_SOCKET)
		return false;

	socketSetBlockingInternal(mSocket, false);

	int connectRet = ::connect(mSocket, reinterpret_cast<sockaddr*>(&socketAddress), sizeof(socketAddress));
	if(connectRet < 0)
	{
		if(errno != EINPROGRESS)
		{
			disconnect();
			return false;
		}

		// Setup poll function call to monitor the connect call.
		// By querying for POLLOUT we're checking if the socket is
		// ready for writing.
		pollfd pfd;
		pfd.fd = mSocket;
		pfd.events = POLLOUT;
		const int pollResult = ::poll(&pfd, 1, timeout /*milliseconds*/);

		const bool timeout = (pollResult == 0);
		const bool error = (pollResult < 0); // an error inside poll happened. Can check error with `errno` variable.
		if(timeout || error)
		{
			disconnect();
			return false;
		}
		else
		{
			PX_ASSERT(pollResult == 1);

			// check that event was precisely POLLOUT and not anything else (e.g., errors, hang-up)
			bool test = (pfd.revents & POLLOUT) && !(pfd.revents & (~POLLOUT));
			if(!test)
			{
				disconnect();
				return false;
			}
		}

		// check if we are really connected, above code seems to return
		// true if host is a unix machine even if the connection was
		// not accepted.
		char buffer;
		if(recv(mSocket, &buffer, 0, 0) < 0)
		{
			if(errno != EWOULDBLOCK)
			{
				disconnect();
				return false;
			}
		}
	}

	socketSetBlockingInternal(mSocket, mIsBlocking);

#if PX_APPLE_FAMILY
	int noSigPipe = 1;
	setsockopt(mSocket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(int));
#endif

	mIsConnected = true;
	mPort = port;
	mHost = host;
	return true;
}

bool SocketImpl::listen(uint16_t port)
{
	mListenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(mListenSocket == INVALID_SOCKET)
		return false;

	// enable address reuse: "Address already in use" error message
	int yes = 1;
	if(setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
		return false;

	mListenMode = true;

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	intrinsics::memSet(addr.sin_zero, '\0', sizeof addr.sin_zero);

	return bind(mListenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != -1 &&
	       ::listen(mListenSocket, SOMAXCONN) != -1;
}

bool SocketImpl::accept(bool block)
{
	if(mIsConnected || !mListenMode)
		return false;

	// set the listen socket to be non-blocking.
	socketSetBlockingInternal(mListenSocket, block);
	PxI32 clientSocket = ::accept(mListenSocket, 0, 0);
	if(clientSocket == INVALID_SOCKET)
		return false;

	mSocket = clientSocket;
	mIsConnected = true;
	socketSetBlockingInternal(mSocket, mIsBlocking); // force the mode to whatever the user set

	return mIsConnected;
}

void SocketImpl::disconnect()
{
	if(mListenSocket != INVALID_SOCKET)
	{
		close(mListenSocket);
		mListenSocket = INVALID_SOCKET;
	}
	if(mSocket != INVALID_SOCKET)
	{
		if(mIsConnected)
		{
			socketSetBlockingInternal(mSocket, true);
			shutdown(mSocket, SHUT_RDWR);
		}
		close(mSocket);
		mSocket = INVALID_SOCKET;
	}
	mIsConnected = false;
	mListenMode = false;
	mPort = 0;
	mHost = NULL;
}

bool SocketImpl::nonBlockingTimeout() const
{
	return !mIsBlocking && errno == EWOULDBLOCK;
}

void socketSetBlockingInternal(PxI32 socket, bool blocking)
{
	int mode = fcntl(socket, F_GETFL, 0);
	if(!blocking)
		mode |= O_NONBLOCK;
	else
		mode &= ~O_NONBLOCK;
	fcntl(socket, F_SETFL, mode);
}

// should be cross-platform from here down

void SocketImpl::setBlocking(bool blocking)
{
	if(blocking != mIsBlocking)
	{
		mIsBlocking = blocking;
		if(isConnected())
			socketSetBlockingInternal(mSocket, blocking);
	}
}

bool SocketImpl::flush()
{
	return true;
}

PxU32 SocketImpl::write(const uint8_t* data, PxU32 length)
{
	if(length == 0)
		return 0;

	int sent = send(mSocket, reinterpret_cast<const char*>(data), PxI32(length), 0);

	if(sent <= 0 && !nonBlockingTimeout())
		disconnect();

	return PxU32(sent > 0 ? sent : 0);
}

PxU32 SocketImpl::read(uint8_t* data, PxU32 length)
{
	if(length == 0)
		return 0;

	PxI32 received = recv(mSocket, reinterpret_cast<char*>(data), PxI32(length), 0);

	if(received <= 0 && !nonBlockingTimeout())
		disconnect();

	return PxU32(received > 0 ? received : 0);
}

class BufferedSocketImpl : public SocketImpl
{
  public:
	BufferedSocketImpl(bool isBlocking) : SocketImpl(isBlocking), mBufferPos(0)
	{
	}
	virtual ~BufferedSocketImpl()
	{
	}
	bool flush();
	PxU32 write(const uint8_t* data, PxU32 length);

  private:
	PxU32 mBufferPos;
	uint8_t mBuffer[PxSocket::DEFAULT_BUFFER_SIZE];
};

bool BufferedSocketImpl::flush()
{
	PxU32 totalBytesWritten = 0;

	while(totalBytesWritten < mBufferPos && mIsConnected)
		totalBytesWritten += PxI32(SocketImpl::write(mBuffer + totalBytesWritten, mBufferPos - totalBytesWritten));

	bool ret = (totalBytesWritten == mBufferPos);
	mBufferPos = 0;
	return ret;
}

PxU32 BufferedSocketImpl::write(const uint8_t* data, PxU32 length)
{
	PxU32 bytesWritten = 0;
	while(mBufferPos + length >= PxSocket::DEFAULT_BUFFER_SIZE)
	{
		PxU32 currentChunk = PxSocket::DEFAULT_BUFFER_SIZE - mBufferPos;
		intrinsics::memCopy(mBuffer + mBufferPos, data + bytesWritten, currentChunk);
		bytesWritten += PxU32(currentChunk); // for the user, this is consumed even if we fail to shove it down a
		// non-blocking socket

		PxU32 sent = SocketImpl::write(mBuffer, PxSocket::DEFAULT_BUFFER_SIZE);
		mBufferPos = PxSocket::DEFAULT_BUFFER_SIZE - sent;

		if(sent < PxSocket::DEFAULT_BUFFER_SIZE) // non-blocking or error
		{
			if(sent) // we can reasonably hope this is rare
				intrinsics::memMove(mBuffer, mBuffer + sent, mBufferPos);

			return bytesWritten;
		}
		length -= currentChunk;
	}

	if(length > 0)
	{
		intrinsics::memCopy(mBuffer + mBufferPos, data + bytesWritten, length);
		bytesWritten += length;
		mBufferPos += length;
	}

	return bytesWritten;
}

PxSocket::PxSocket(bool inIsBuffering, bool isBlocking)
{
	if(inIsBuffering)
	{
		void* mem = PX_ALLOC(sizeof(BufferedSocketImpl), "BufferedSocketImpl");
		mImpl = PX_PLACEMENT_NEW(mem, BufferedSocketImpl)(isBlocking);
	}
	else
	{
		void* mem = PX_ALLOC(sizeof(SocketImpl), "SocketImpl");
		mImpl = PX_PLACEMENT_NEW(mem, SocketImpl)(isBlocking);
	}
}

PxSocket::~PxSocket()
{
	mImpl->flush();
	mImpl->disconnect();
	mImpl->~SocketImpl();
	PX_FREE(mImpl);
}

bool PxSocket::connect(const char* host, uint16_t port, PxU32 timeout)
{
	return mImpl->connect(host, port, timeout);
}

bool PxSocket::listen(uint16_t port)
{
	return mImpl->listen(port);
}

bool PxSocket::accept(bool block)
{
	return mImpl->accept(block);
}

void PxSocket::disconnect()
{
	mImpl->disconnect();
}

bool PxSocket::isConnected() const
{
	return mImpl->isConnected();
}

const char* PxSocket::getHost() const
{
	return mImpl->getHost();
}

uint16_t PxSocket::getPort() const
{
	return mImpl->getPort();
}

bool PxSocket::flush()
{
	if(!mImpl->isConnected())
		return false;
	return mImpl->flush();
}

PxU32 PxSocket::write(const uint8_t* data, PxU32 length)
{
	if(!mImpl->isConnected())
		return 0;
	return mImpl->write(data, length);
}

PxU32 PxSocket::read(uint8_t* data, PxU32 length)
{
	if(!mImpl->isConnected())
		return 0;
	return mImpl->read(data, length);
}

void PxSocket::setBlocking(bool blocking)
{
	mImpl->setBlocking(blocking);
}

bool PxSocket::isBlocking() const
{
	return mImpl->isBlocking();
}

} // namespace physx
#endif // !PX_PSP
