#include "types.h"
#include "poll.h"
#include "network/udp.h"

#include <cstdio>

struct LoopbackCallbacks final : Udp::Callbacks {
  int count = 0;
  int bytes = 0;
  void OnMsg(sockaddr_in&, UdpMsg*, int len) override {
    ++count;
    bytes += len;
  }
};

int main() {
  Poll poll;
  LoopbackCallbacks callbacks;
  Udp udp;
  constexpr uint16 port = 39877;
  udp.Init(port, &poll, &callbacks);

  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr);

  UdpMsg message(UdpMsg::SyncRequest);
  const int packet_size = message.PacketSize();
  udp.SendTo(reinterpret_cast<char*>(&message), packet_size, 0,
             reinterpret_cast<sockaddr*>(&destination), sizeof(destination));

  for (int i = 0; i < 5 && callbacks.count == 0; ++i) {
    poll.Pump(5);
  }

  std::printf("GGPO_UDP_SMOKE count=%d bytes=%d packet_size=%d\n",
              callbacks.count, callbacks.bytes, packet_size);
  return callbacks.count == 1 && callbacks.bytes == packet_size ? 0 : 1;
}
