// test_4g_throughput.cc
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("4gThroughputMeasurement");

int main(int argc, char *argv[]) {
    // Thông số cấu hình
    double simTime = 6.0;          // Thời gian mô phỏng
    double distance = 10.0;       // Khoảng cách UE - eNB (m)
    uint32_t packetSize = 1500;    // Kích thước gói tin (bytes)
    std::string schedulerType = "ns3::PfFfMacScheduler"; // Loại scheduler

    // Cấu hình LTE
    Config::SetDefault("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue(320));
    Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(true));
    Config::SetDefault("ns3::LteEnbNetDevice::UlBandwidth", UintegerValue(100)); // 20 MHz
    Config::SetDefault("ns3::LteEnbNetDevice::DlBandwidth", UintegerValue(100));

    // Tạo helper cho LTE và EPC
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    lteHelper->SetSchedulerType(schedulerType);

    // Tạo các node
    NodeContainer enbNodes, ueNodes, remoteHost;
    enbNodes.Create(1);
    ueNodes.Create(1);
    remoteHost.Create(1);

    // Cài đặt stack Internet
    InternetStackHelper internet;
    internet.Install(remoteHost);
    internet.Install(enbNodes);
    internet.Install(ueNodes);

    // Cấu hình vị trí
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(enbNodes);
    mobility.Install(ueNodes);
    enbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 30.0));
    ueNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(distance, 0.0, 1.5));

    // Cài đặt thiết bị LTE
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

    // Gán địa chỉ IP
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(ueDevs);

    // Kết nối UE với eNB
    lteHelper->Attach(ueDevs, enbDevs.Get(0));

    // Kích hoạt EPS Bearer
    Ptr<EpcTft> tft = Create<EpcTft>();
    EpcTft::PacketFilter pf;
    pf.direction = EpcTft::BIDIRECTIONAL;
    tft->Add(pf);
    
    lteHelper->ActivateDedicatedEpsBearer(
        ueDevs.Get(0), 
        EpsBearer(EpsBearer::GBR_CONV_VOICE), 
        tft
    );

    // Kết nối PGW với remote host
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("10Gbps")));
    p2p.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
    NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost.Get(0));

    // Cấu hình IP
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);

    // Cấu hình định tuyến
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostRouting = ipv4RoutingHelper.GetStaticRouting(
        remoteHost.Get(0)->GetObject<Ipv4>());
    remoteHostRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // Ứng dụng UDP
    uint16_t port = 1234;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(simTime));

    UdpClientHelper client(ueIpIface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(Seconds(0.00001))); // 10 us interval
    client.SetAttribute("PacketSize", UintegerValue(packetSize));

    ApplicationContainer clientApps = client.Install(remoteHost.Get(0));
    clientApps.Start(Seconds(2.5)); // Bắt đầu sau khi kết nối ổn định
    clientApps.Stop(Seconds(simTime - 0.5));

    // Bật log để debug
    //LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
    //LogComponentEnable("UdpServer", LOG_LEVEL_INFO);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Tính toán throughput
    Ptr<UdpServer> udpServer = serverApps.Get(0)->GetObject<UdpServer>();
    uint64_t totalRx = udpServer->GetReceived();
    double throughput = (totalRx * packetSize * 8) / (simTime - 2.5) / 1e6; // Mbps

    std::cout << "\n----------------------------------------\n"
              << "4G LTE Throughput Results:\n"
              << "  Total packets received: " << totalRx << "\n"
              << "  Average throughput: " << throughput << " Mbps\n"
              << "----------------------------------------\n";

    Simulator::Destroy();
    return 0;
}
