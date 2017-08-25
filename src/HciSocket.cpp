// Copyright 2017 Paul Nettle.
//
// This file is part of Gobbledegook.
//
// Gobbledegook is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Gobbledegook is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Gobbledegook.  If not, see <http://www.gnu.org/licenses/>.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// Low-level socket interface used to communicate with the Bluetooth Management API (see HciAdapter.cpp)
//
// >>
// >>>  DISCUSSION
// >>
//
// This class intended to support `HciAdapter` - see HciAdapter.cpp for more information.
//
// This file contains raw communication layer for the Bluetooth Management API, which is used to configure the Bluetooth adapter
// (such as enabling LE, setting the device name, etc.) This class is used by HciAdapter (HciAdapter.h) to perform higher-level
// functions.
//
// This code is for example purposes only. If you plan to use this in a production environment, I suggest rewriting it.
//
// The information for this implementation (as well as HciAdapter.h) came from:
//
//     https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/mgmt-api.txt
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <thread>

#include "HciSocket.h"
#include "Logger.h"
#include "Utils.h"

// Initializes an unconnected socket
HciSocket::HciSocket()
: fdSocket(-1)
{
}

// Socket destructor
//
// This will automatically disconnect the socket if it is currently connected
HciSocket::~HciSocket()
{
	disconnect();
}

// Connects to an HCI socket using the Bluetooth Management API protocol
//
// Returns true on success, otherwise false
bool HciSocket::connect()
{
	disconnect();

	fdSocket = socket(PF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, BTPROTO_HCI);
	if (fdSocket < 0)
	{
		logErrno("Connect(socket)");
		return false;
	}

	struct sockaddr_hci addr;
	memset(&addr, 0, sizeof(addr));
	addr.hci_family = AF_BLUETOOTH;
	addr.hci_dev = HCI_DEV_NONE;
	addr.hci_channel = HCI_CHANNEL_CONTROL;

	if (bind(fdSocket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		logErrno("Connect(bind)");
		disconnect();
		return false;
	}

	Logger::debug(SSTR << "Connected to HCI control socket (file descriptor = " << fdSocket << ")");

	return true;
}

// Returns true if the socket is currently connected, otherwise false
bool HciSocket::isConnected() const
{
	return fdSocket >= 0;
}

// Disconnects from the HCI socket
void HciSocket::disconnect()
{
	if (isConnected())
	{
		close(fdSocket);
		fdSocket = -1;
	}
}

// Reads data from the HCI socket
//
// Raw data is read until no more data is available. If no data is available when this method initially starts to read, it will
// retry for a maximum timeout defined by `kMaxRetryTimeMS`.
//
// Returns true if any data was read successfully, otherwise false is returned in the case of an error or a timeout.
bool HciSocket::read(std::vector<uint8_t> &response) const
{
	// Clear out our response
	response.clear();

	uint8_t responseChunk[kResponseChunkSize];

	int retryTimeMS = 0;
	while (retryTimeMS < kMaxRetryTimeMS && !ggkIsServerRunning())
	{
		ssize_t bytesRead = ::read(fdSocket, responseChunk, kResponseChunkSize);
		if (bytesRead > 0)
		{
			if (response.size() + bytesRead > kResponseMaxSize)
			{
				Logger::warn(SSTR << "Response has exceeded maximum size");
				return false;
			}
			// We just received some data, add it to our buffer
			std::vector<uint8_t> insertBuf(responseChunk, responseChunk + bytesRead);
			response.insert(response.end(), insertBuf.begin(), insertBuf.end());
		}
		else
		{
			// If we have data, we're at the end
			if (response.size() != 0)
			{
				break;
			}
		}

		// Retry (or continue reading)
		std::this_thread::sleep_for(std::chrono::milliseconds(kRetryIntervalMS));
		retryTimeMS += kRetryIntervalMS;
	}

	// Did we time out?
	if (retryTimeMS >= kMaxRetryTimeMS)
	{
		logErrno("read(header)");
		return false;
	}

	Logger::debug(SSTR << "  + Read " << response.size() << " bytes");
	Logger::debug(SSTR << Utils::hex(response.data(), response.size()));
	return true;
}

// Writes the array of bytes of a given count
//
// This method returns true if the bytes were written successfully, otherwise false
bool HciSocket::write(std::vector<uint8_t> buffer) const
{
	return write(buffer.data(), buffer.size());
}

// Writes the array of bytes of a given count
//
// This method returns true if the bytes were written successfully, otherwise false
bool HciSocket::write(const uint8_t *pBuffer, size_t count) const
{
	Logger::debug(SSTR << "  + Writing " << count << " bytes");
	Logger::debug(SSTR << Utils::hex(pBuffer, count));

	size_t len = ::write(fdSocket, pBuffer, count);

	if (len != count)
	{
		logErrno("write");
		return false;
	}

	return true;
}

// Utilitarian function for logging errors for the given operation
void HciSocket::logErrno(const char *pOperation) const
{
	Logger::error(SSTR << "" << pOperation << " on Bluetooth management socket error (" << errno << "): " << strerror(errno));
}