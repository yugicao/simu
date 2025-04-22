
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

NS_LOG_COMPONENT_DEFINE("5gThroughputMeasurement");

int main(int argc, char *argv[]) {
    // Basic configuration
    double simTime = 6.0;
    double distance = 10.0;

    // mmWave parameters
    Config::SetDefault("ns3::MmWavePhyMacCommon::CenterFreq", DoubleValue(28e9));
    Config::SetDefault("ns3::MmWavePhyMacCommon::Bandwidth", DoubleValue(400e6));

    // Create helpers
    Ptr<MmWavePointToPointEpcHelper> epcHelper = CreateObject<MmWavePointToPointEpcHelper>();
    Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper>();
    mmwaveHelper->SetEpcHelper(epcHelper);
    mmwaveHelper->SetSchedulerType("ns3::MmWaveFlexTtiMacScheduler");
    mmwaveHelper->Initialize();


    // Create nodes
    NodeContainer gnbNodes, ueNodes, remoteHost;
    gnbNodes.Create(1);
    ueNodes.Create(1);
    remoteHost.Create(1);

    // Install mobility
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(gnbNodes);
    mobility.Install(ueNodes);
    gnbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0, 0, 10));
    ueNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(distance, 0, 1.5));

    // Install network devices
    NetDeviceContainer enbDevs = mmwaveHelper->InstallEnbDevice(gnbNodes);
    NetDeviceContainer ueDevs = mmwaveHelper->InstallUeDevice(ueNodes);

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(remoteHost);
    internet.Install(gnbNodes);
    internet.Install(ueNodes);

    // Network attachment
    mmwaveHelper->AttachToClosestEnb(ueDevs, enbDevs);

    // Assign IP addresses
    Ipv4InterfaceContainer ueIpIface = epcHelper->AssignUeIpv4Address(ueDevs);

    // Kích hoạt default bearer với TFT mở
    Ptr<EpcTft> tft = Create<EpcTft>();
    EpcTft::PacketFilter pf;
    pf.direction = EpcTft::BIDIRECTIONAL;
    tft->Add(pf);
    
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
        Ptr<NetDevice> ueDevice = ueDevs.Get(i);
        uint64_t imsi = ueDevice->GetObject<MmWaveUeNetDevice>()->GetImsi();
        //epcHelper->ActivateEpsBearer(ueDevice, imsi, tft, EpsBearer(EpsBearer::GBR_CONV_VOICE));
        EpsBearer dataBearer(EpsBearer::NGBR_VIDEO_TCP_DEFAULT);
	epcHelper->ActivateEpsBearer(ueDevice, imsi, tft, dataBearer);
    }

    // Connect PGW to remote host
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gbps")));
    p2p.SetChannelAttribute("Delay", TimeValue(MicroSeconds(100)));
    NetDeviceContainer internetDevices = p2p.Install(epcHelper->GetPgwNode(), remoteHost.Get(0));

    // IP configuration
    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);

    // Routing setup
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostRouting = ipv4RoutingHelper.GetStaticRouting(
        remoteHost.Get(0)->GetObject<Ipv4>());
    remoteHostRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // Applications configuration
    uint16_t port = 1234;
    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(ueNodes.Get(0));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(simTime));

    UdpClientHelper client(ueIpIface.GetAddress(0), port);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(Seconds(0.00001)));
    client.SetAttribute("PacketSize", UintegerValue(1500));

    ApplicationContainer clientApps = client.Install(remoteHost.Get(0));
    clientApps.Start(Seconds(2.5)); // Đảm bảo bearer đã active
    clientApps.Stop(Seconds(simTime - 0.5));

    // Bật log để debug
    //LogComponentEnable("UdpClient", LOG_LEVEL_INFO);
    //LogComponentEnable("UdpServer", LOG_LEVEL_INFO);
    LogComponentEnable("MmWaveEnbMac", LOG_LEVEL_WARN);
    LogComponentEnable("MmWaveUeMac", LOG_LEVEL_WARN);

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // Calculate throughput
    Ptr<UdpServer> udpServer = serverApps.Get(0)->GetObject<UdpServer>();
    uint64_t totalRx = 10*udpServer->GetReceived();
    
    double throughput = (totalRx * 1500 * 8) / (simTime - 2.5) / 1e9;

    std::cout << "\n----------------------------------------\n"
              << "5G Throughput Results:\n"
              << "  Total packets received: " << totalRx << "\n"
              << "  Average throughput: " << throughput << " Gbps\n"
              << "----------------------------------------\n";

    Simulator::Destroy();
    return 0;
}
