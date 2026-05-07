// Copyright 2025 FZM TU Dresden - Georg Beierlein
// georg.beierlein@tu-dresden.de
// 2025/04/03
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// Georg Beierlein - IAD FZM
// georg.beierlein@tu-dresden.de
// 2025/04/03

#ifndef TECMP_CAN_INTERFACE_TECMP_TO_SOCKETCAN_H
#define TECMP_CAN_INTERFACE_TECMP_TO_SOCKETCAN_H

#include <stdint.h>
#include <iostream>
#include <unistd.h>
#include <string.h>

#include <thread>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/ip.h>
#include <linux/if_packet.h> // Required for sockaddr_ll

#include <net/ethernet.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <deque>
#include <mutex>
#include <map>

#include "libtecmp/tecmp.h"

static constexpr int CAN_MTU_SIZE = 16;
static constexpr uint16_t ETHER_TYPE = 0x99FE; // TECMP

static constexpr const char *DEFAULT_IF = "eth0";
static constexpr int BUF_SIZ = 1024;

struct CanChannelInfo
{
    std::string name;        ///< SocketCAN interface name (e.g., "vcan0")
    int fd = -1;             ///< Socket file descriptor for this CAN channel
    std::string description; ///< Optional description of the CAN channel
};

class TecmpToSocketCan
{
private:
    std::string ethInterface_; // Name of the network interface
    int ethSocketFd_;

    tecmp_can_message tecmpCanMsg_;

    uint8_t ethernetFrame_[BUF_SIZ];

    // Receive Stuff
    ssize_t numBytes_;

    // Map from TECMP channel ID -> CanChannelInfo (name + socket fd)
    std::map<uint8_t, CanChannelInfo> canChannels_;

    /**
     * @brief Set the Promiscuous Mode for the given interface
     *
     * @param interfaceName Name of the network interface
     * @param enable true to enable promiscuous mode, false to disable it
     * @return true if successful, false otherwise
     *
     * This function sets the promiscuous mode for the specified network interface.
     * Promiscuous mode allows the interface to receive all packets on the network,
     * not just those addressed to it.
     */
    bool setPromiscuousMode(const std::string &interfaceName, bool enable);

public:
    /**
     * @brief Constructor for TecmpToSocketCan
     * @param interfaceName Name of the ethernet network interface to listen on (default: "enp21s0f0")
     */
    TecmpToSocketCan(const std::string &interfaceName = DEFAULT_IF);

    ~TecmpToSocketCan();

    /**
     * @brief Infinite loop to receive Ethernet frames, decode TECMP messages, and send CAN frames to SocketCAN interfaces
     *
     */
    void run();

    /**
     * @brief Set up the Ethernet socket for receiving frames
     *
     * @return int fileDescriptor
     */
    int setupEthSocket();

    /**
     * @brief Set up a CAN socket for sending frames to a SocketCAN interface
     *
     * @param canChannelName Name of the CAN channel to connect to (e.g., "vcan0")
     * @return int fileDescriptor
     */
    int setupCanSocket(std::string canChannelName);

    /**
     * @brief Decode a TECMP frame and extract the CAN frame and TECMP channel ID
     *
     * @return std::pair<can_frame, uint8_t> Pair of the decoded CAN frame and channel ID
     */
    std::pair<can_frame, uint8_t> decodeTECMPFrame();

    /**
     * @brief Send a CAN frame to a SocketCAN interface
     *
     * @param canSocketFd File descriptor for the CAN socket
     * @param canFrame The CAN frame to send
     */
    void sendCanFrame(int canSocketFd, const can_frame &canFrame);
};

#endif // TECMP_CAN_INTERFACE_TECMP_TO_SOCKETCAN_H
