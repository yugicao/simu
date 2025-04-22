// test_5g_delay.cc
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/mmwave-module.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE("5gLatencyMeasurement");

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
    //std::cout << "Packet delay: " << delay.GetMilliSeconds() << " ms" << std::endl;
  }
}

int main(int argc, char *argv[]) {
  // Kích hoạt log cho các ứng dụng UDP (các log của mmWaveEnbMac và mmWaveUeMac để giảm output)
  LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
  LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
  // (Các log chi tiết của mmWaveEnbMac/MmWaveUeMac có thể được tắt bằng cách không kích hoạt chúng)

  // Cấu hình hệ thống
  Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
  Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(400e6));
  // Không cấu hình lại LongTermUpdatePeriod vì thuộc tính này được quản lý nội bộ theo ns3-mmwave 3.42

  // Khởi tạo helper cho EPC và mmWave
  Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
  mmwaveHelper->SetEpcHelper(epcHelper);
  mmwaveHelper->Initialize();

  // Tạo remote host (nút nguồn) và cài đặt Internet stack
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Tạo node cho gNB và UE
  NodeContainer gnbNodes, ueNodes;
  gnbNodes.Create(1);
  ueNodes.Create(1);
  internet.Install(gnbNodes);
  internet.Install(ueNodes);

  // Cấu hình vị trí cho gNB và UE
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.Install(gnbNodes);
  mobility.Install(ueNodes);
  gnbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0, 0, 10));
  ueNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(5, 0, 1.5));

  // Cài đặt thiết bị mạng mmWave cho gNB và UE
  NetDeviceContainer enbDevs = mmwaveHelper->InstallEnbDevice(gnbNodes);
  NetDeviceContainer ueDevs = mmwaveHelper->InstallUeDevice(ueNodes);

  // Gán địa chỉ IP cho UE qua EPC
  Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(ueDevs);

  // Kích hoạt EPS Bearer cho UE
  Ptr<EpcTft> tft = Create<EpcTft>();
  EpcTft::PacketFilter pf;
  pf.direction = EpcTft::BIDIRECTIONAL;
  tft->Add(pf);
  for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
    Ptr<NetDevice> ueDevice = ueDevs.Get(i);
    uint64_t imsi = ueDevice->GetObject<MmWaveUeNetDevice>()->GetImsi();
    epcHelper->ActivateEpsBearer(ueDevice, imsi, tft,
      EpsBearer(EpsBearer::NGBR_VIDEO_TCP_DEFAULT));
  }

  // Kết nối UE và gNB (beamforming được thực hiện tự động khi gọi AttachToClosestEnb)
  mmwaveHelper->AttachToClosestEnb(ueDevs, enbDevs);
  mmwaveHelper->EnableTraces();

  // Thiết lập liên kết giữa PGW và remote host
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
  p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost);
  Ipv4AddressHelper ipv4;
  ipv4.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4.Assign(internetDevices);

  // Cấu hình định tuyến trên remote host: route đến UE (subnet của UE thường là 7.0.0.0/8)
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // Cài đặt ứng dụng UDP: UdpServer được cài đặt trên UE
  uint16_t port = 1234;
  UdpServerHelper server(port);
  ApplicationContainer serverApps = server.Install(ueNodes.Get(0));
  serverApps.Start(Seconds(0.5));
  serverApps.Stop(Seconds(4.0));

  // Cài đặt UdpClient trên remote host (đi qua EPC đến UE)
  UdpClientHelper client(ueIpIface.GetAddress(0), port);
  client.SetAttribute("MaxPackets", UintegerValue(100));
  client.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
  client.SetAttribute("PacketSize", UintegerValue(512));
  ApplicationContainer clientApps = client.Install(remoteHost);


  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(3.9));

  // Gắn callback: tag gói tin khi Tx và tính toán độ trễ khi Rx
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
