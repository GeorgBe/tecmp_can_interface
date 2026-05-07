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

#include "tecmp_can_interface/tecmp_to_socketcan.h"

using namespace std::chrono_literals;

TecmpToSocketCan::TecmpToSocketCan(const std::string &interfaceName) : ethInterface_(interfaceName)
{

  // Enable promiscuous mode
  if (!setPromiscuousMode(ethInterface_, true))
  {
    std::cerr << "Failed to enable promiscuous mode on interface " << ethInterface_  << std::endl;
    std::cerr << "Shutting down node..." << std::endl;

    exit(1);
  }

  // Add Parameters for:
  // - CAN Channel

  //////////
  // Setup Sockets
  // - Ethernet Socket
  ethSocketFd_ = setupEthSocket();
  if (ethSocketFd_ == -1)
  {
    std::cerr << "Socket Creation failed at " << ethInterface_ << std::endl;
    std::cerr << "Shutting down node..." << std::endl;
    exit(1);
  }

  // - CAN Sockets
  // Initialize CAN channels and map TECMP channel IDs -> SocketCAN info
  canChannels_[1] = {"vcan0", setupCanSocket("vcan0"), "Virtual CAN Channel 0"};
  canChannels_[2] = {"vcan1", setupCanSocket("vcan1"), "Virtual CAN Channel 1"};
  canChannels_[3] = {"vcan2", setupCanSocket("vcan2"), "Virtual CAN Channel 2"};

  for (const auto &kv : canChannels_)
  {
    if (kv.second.fd == -1)
    {
      std::cerr << "Socket Creation failed at " << std::endl;
      std::cerr << "Shutting down node..." << std::endl;
      exit(1);
    }
  }

  // Start Main Loop
  run();
}

TecmpToSocketCan::~TecmpToSocketCan()
{
  std::cout << "Shutting down TECMP to SocketCAN node." << std::endl;
  // Deactivate promiscuous mode and close socket

  // Disable promiscuous mode
  if (!setPromiscuousMode(ethInterface_, false))
  {
    std::cerr << "Failed to disable promiscuous mode on interface "
              << ethInterface_ << "\n";
  }

  struct packet_mreq mr;
  memset(&mr, 0, sizeof(mr));
  mr.mr_ifindex = if_nametoindex(ethInterface_.c_str());
  mr.mr_type = PACKET_MR_PROMISC;

  if (setsockopt(ethSocketFd_, SOL_PACKET, PACKET_DROP_MEMBERSHIP, &mr,
                 sizeof(mr)) < 0)
  {
    perror("setsockopt");
  }

  close(ethSocketFd_);
  for (auto &kv : canChannels_)
  {
    if (kv.second.fd >= 0)
    {
      close(kv.second.fd);
    }
  }

  exit(0);
}

void TecmpToSocketCan::run()
{
  // Main Loop
  while (true)
  {

    struct sockaddr_ll sll;
    socklen_t sll_len = sizeof(sll);

    numBytes_ = recvfrom(ethSocketFd_, ethernetFrame_, BUF_SIZ, 0,
                         (struct sockaddr *)&sll, &sll_len);
    if (numBytes_ < 0)
    {
      int err = errno; // Capture the errno value
      std::cout << "Receive failed with errno: " << err << ", error: " << strerror(err) << std::endl;
    }
    // There is something to read
    else if (numBytes_ > 0)
    {

      auto [canFrame, channel_id] = decodeTECMPFrame();
      // Check the interface ID and send the frame to the corresponding CAN
      // socket
      auto it = canChannels_.find(channel_id);
      if (it != canChannels_.end() && it->second.fd >= 0)
      {
        sendCanFrame(it->second.fd, canFrame);
      }
      else
      {
        std::cerr << "Unknown TECMP channel_id " 
        << static_cast<int>(channel_id) 
        << " - no corresponding CAN channel found. Frame will be dropped."
        << std::endl;
      }
    }
  }
}

int TecmpToSocketCan::setupEthSocket()
{
  ///////////////////
  // Setup Ethernet Communication
  
  struct sockaddr_ll sll;
  struct packet_mreq mr;
  struct timeval timeout;

  int sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (sock < 0)
  {
    int err = errno; // Capture the errno value
    std::cout << "Socket creation failed with errno: " << err << ", error: " << strerror(err) << std::endl;
    return -1;
  }

  // Bind Socket to Interface
  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_ifindex = if_nametoindex(ethInterface_.c_str());

  if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0)
  {
    perror("bind");
    close(sock);
    return -1;
  }

  // Activate Promiscuous Mode
  memset(&mr, 0, sizeof(mr));
  mr.mr_ifindex = if_nametoindex(
      ethInterface_.c_str()); // Get the index of the specified network interface
  mr.mr_type = PACKET_MR_PROMISC;
  if (setsockopt(sock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) <
      0)
  {
    perror("setsockopt");
    close(sock);
    return -1;
  }

  // Set Socket timeout - avoid infinite blocking
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0)
  {
    perror("setsockopt");
    return -1;
  }

  return sock;
}

bool TecmpToSocketCan::setPromiscuousMode(const std::string &interfaceName,
                                          bool enable)
{
  struct ifreq ifr;
  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    std::cerr << "Failed to create socket for setting promiscuous mode: "
              << strerror(errno) << "\n";
    return false;
  }

  strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ);
  if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) == -1)
  {
    std::cerr << "Failed to get interface flags for " << interfaceName << ": "
              << strerror(errno) << "\n";
    close(sockfd);
    return false;
  }

  if (enable)
  {
    ifr.ifr_flags |= IFF_PROMISC; // Set promiscuous mode flag
  }
  else
  {
    ifr.ifr_flags &= ~IFF_PROMISC; // Clear promiscuous mode flag
  }

  if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) == -1)
  {
    std::cerr << "Failed to set interface flags for " << interfaceName << ": "
              << strerror(errno) << "\n";
    close(sockfd);
    return false;
  }

  std::cout << (enable ? "Enabled" : "Disabled") << " promiscuous mode on "
            << interfaceName << std::endl;
  close(sockfd);
  return true;
}

int TecmpToSocketCan::setupCanSocket(std::string canChannelName)
{
  // CAN Communication
  // inspired by
  // https://github.com/craigpeacock/CAN-Examples/blob/master/cantransmit.c and
  // https://www.beyondlogic.org/example-c-socketcan-code/ int s;

  struct sockaddr_can canAddr;
  struct ifreq canIfr;
  int sockFd;

  std::cout << "Setting up Socket for " << canChannelName << std::endl;

  // Create Socket
  if ((sockFd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
  {
    perror("socket");
    return -1;
  }

  // Get Interface Name
  strcpy(canIfr.ifr_name, canChannelName.c_str());
  ioctl(sockFd, SIOCGIFINDEX, &canIfr);
  memset(&canAddr, 0, sizeof(canAddr));
  canAddr.can_family = AF_CAN;
  canAddr.can_ifindex = canIfr.ifr_ifindex;

  // Bind to CAN Socket
  if (bind(sockFd, (struct sockaddr *)&canAddr, sizeof(canAddr)) < 0)
  {
    perror("bind");
    return -1;
  }

  return sockFd;
}

std::pair<can_frame, uint8_t> TecmpToSocketCan::decodeTECMPFrame()
{
  // Parse the Packets and check for valid CAN Frames
  int32_t iterator = 0;

  tecmp_header header{};
  uint8_t *data = nullptr;

  can_frame canFrame{};
  uint32_t channelId = 0;

  int res = tecmp_next(ethernetFrame_, sizeof(ethernetFrame_), &iterator,
                       &header, &data);

  if (res >= 0 && header.message_type == TECMP_TYPE_LOGGING_STREAM &&
      header.data_type == TECMP_DATA_CAN)
  {
    memcpy(&tecmpCanMsg_, data, sizeof(tecmpCanMsg_));

    canFrame.can_dlc = tecmpCanMsg_.dlc;
    canFrame.can_id = ntohl(tecmpCanMsg_.can_id);
    memcpy(&canFrame.data, tecmpCanMsg_.data, sizeof(tecmpCanMsg_.data));

    // Get the CAN channel ID from the TECMP header to determine which SocketCAN interface to send to
    channelId = header.channel_id;

  }
  else
  {
    std::cerr << "Failed to parse TECMP frame or invalid message type.\n";
  }
  // Replace by pointers
  return std::make_pair(canFrame, channelId);
}

void TecmpToSocketCan::sendCanFrame(int canSocketFd, const can_frame& canFrame)
{
  if (canFrame.can_dlc > 8)
  {
    std::cerr << "Invalid CAN frame: DLC > 8 (got "
              << static_cast<int>(canFrame.can_dlc) << ")" << std::endl;
    return;
  }

  int nbytes = write(canSocketFd, &canFrame, sizeof(struct can_frame));
  if (nbytes != sizeof(struct can_frame))
  {
    std::cerr << "Failed to write CAN frame (" << nbytes
              << " bytes written): " << strerror(errno) << std::endl;
  };
}

// Main function
int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  // Get interface name from command line arguments or use default
  std::string interfaceName = DEFAULT_IF;
  if (argc > 1)
  {
    interfaceName = argv[1];
  }
  else
  {
    std::cout << "No interface name provided. Using default: " << DEFAULT_IF
              << std::endl;
  }

  TecmpToSocketCan canListener(interfaceName);

  canListener.run();

  return 0;
}