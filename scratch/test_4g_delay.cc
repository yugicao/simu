// test_4g_delay.cc
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("4gLatencyMeasurement");

class PacketTimestampTag : public Tag {
public:
  static TypeId GetTypeId() {
    static TypeId tid = TypeId("PacketTimestampTag")
      .SetParent<Tag>()
      .AddConstructor<PacketTimestampTag>();
    return tid;
  }
  virtual TypeId GetInstanceTypeId() const override { return GetTypeId(); }
  virtual uint32_t GetSerializedSize() const override { return sizeof(uint64_t); }
  virtual void Serialize(TagBuffer buf) const override { buf.WriteU64(m_timestamp); }
  virtual void Deserialize(TagBuffer buf) override { m_timestamp = buf.ReadU64(); }
  virtual void Print(std::ostream& os) const override {}

  void SetTimestamp(Time time) { m_timestamp = time.GetNanoSeconds(); }
  Time GetTimestamp() const { return NanoSeconds(m_timestamp); }
private:
  uint64_t m_timestamp;
};

void TagPacket(Ptr<const Packet> packet) {
  PacketTimestampTag tag;
  tag.SetTimestamp(Simulator::Now());
  const_cast<Packet*>(PeekPointer(packet))->AddPacketTag(tag);
}

void CalculateLatency(Ptr<const Packet> packet) {
  PacketTimestampTag tag;
  if (packet->PeekPacketTag(tag)) {
    Time delay = Simulator::Now() - tag.GetTimestamp();
    //std::cout << "Delay: " << delay.GetMilliSeconds() << " ms" << std::endl;
  }
}

int main (int argc, char *argv[]) {
  // Kích hoạt log cho ứng dụng UDP
  LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
  LogComponentEnable("UdpServer", LOG_LEVEL_INFO);

  // Tạo helper cho LTE và EPC
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);

  // Tạo remote host (nút nguồn) và cài đặt Internet Stack cho nó
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Tạo node cho eNodeB (trạm gốc) và UE
  NodeContainer enbNodes;
  NodeContainer ueNodes;
  enbNodes.Create(1);
  ueNodes.Create(1);
  internet.Install(enbNodes);
  internet.Install(ueNodes);

  // Cấu hình vị trí cho eNodeB và UE
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(enbNodes);
  mobility.Install(ueNodes);
  enbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 30.0)); // eNodeB cao hơn mặt đất
  ueNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(10.0, 0.0, 1.5));

  // Cài đặt thiết bị LTE cho eNodeB và UE
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

  // Gán địa chỉ IP cho UE qua EPC
  Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(ueLteDevs);

  // Kết nối UE với eNodeB
  lteHelper->Attach(ueLteDevs, enbLteDevs.Get(0));

  // Thiết lập liên kết giữa PGW và remote host
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(30)));
  NetDeviceContainer internetDevices = p2p.Install(pgw, remoteHost);
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("1.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4.Assign(internetDevices);

  // Cấu hình định tuyến trên remote host: thêm route đến subnet của UE (thường là 7.0.0.0/8)
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // Cài đặt ứng dụng UDP: UdpServer trên UE, UdpClient trên remote host
  uint16_t port = 1234;
  UdpServerHelper server(port);
  ApplicationContainer serverApps = server.Install(ueNodes.Get(0));
  serverApps.Start(Seconds(0.5));
  serverApps.Stop(Seconds(4.0));

  UdpClientHelper client(ueIpIface.GetAddress(0), port);
  client.SetAttribute("MaxPackets", UintegerValue(100));
  client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
  client.SetAttribute("PacketSize", UintegerValue(512));
  ApplicationContainer clientApps = client.Install(remoteHost);
  // Đặt thời gian Start của UdpClient ở 2.0 giây để đảm bảo kết nối ổn định
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(3.9));

  // Gắn callback: tag gói tin khi Tx và đo độ trễ khi Rx
  Ptr<UdpClient> udpClient = clientApps.Get(0)->GetObject<UdpClient>();
  udpClient->TraceConnectWithoutContext("Tx", MakeCallback(&TagPacket));
  Ptr<UdpServer> udpServer = serverApps.Get(0)->GetObject<UdpServer>();
  udpServer->TraceConnectWithoutContext("Rx", MakeCallback(&CalculateLatency));

  // Chạy mô phỏng
  Simulator::Stop(Seconds(4.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

