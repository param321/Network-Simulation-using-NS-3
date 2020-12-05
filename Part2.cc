#include <bits/stdc++.h>

#include "ns3/log.h"
#include "ns3/tcp-westwood.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/traced-value.h"
#include "ns3/tcp-yeah.h"
#include "ns3/tcp-scalable.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/packet-sink.h"
#include "ns3/tcp-hybla.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/sequence-number.h"
#include "ns3/traced-value.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/enum.h"
#include "ns3/netanim-module.h"


using namespace std;
using namespace ns3;

double duration;
int packetSizeMax;
int portNo;
int total_num_of_Packets;
string TransferRate;

class ApnaApp : public Application
{
public:
    ApnaApp();
    virtual ~ApnaApp();
    void Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);
    void ScheduleTx(void);
    void SendPacket(void);
    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    uint32_t m_nPackets;
    DataRate m_dataRate;
    EventId m_sendEvent;
    bool m_running;
    uint32_t m_packetsSent;
};

ApnaApp::ApnaApp() : m_socket(0), m_peer(), m_packetSize(0), m_nPackets(0), m_dataRate(0), m_sendEvent(), m_running(false), m_packetsSent(0)
{
}

ApnaApp::~ApnaApp()
{
    m_socket = 0;
}

void ApnaApp::Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
    m_socket = socket;
    m_peer = address;
    m_packetSize = packetSize;
    m_nPackets = nPackets;
    m_dataRate = dataRate;
}

void ApnaApp::StartApplication(void)
{
    m_running = true;
    m_packetsSent = 0;
    m_socket->Bind();
    m_socket->Connect(m_peer);
    SendPacket();
}

void ApnaApp::StopApplication(void)
{
    m_running = false;
    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket)
    {
        m_socket->Close();
    }
}

void ApnaApp::SendPacket(void)
{
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    if (++m_packetsSent < m_nPackets)
    {
        ScheduleTx();
    }
}

void ApnaApp::ScheduleTx(void)
{
    if (m_running)
    {
        Time tNext(Seconds(m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate())));
        m_sendEvent = Simulator::Schedule(tNext, &ApnaApp::SendPacket, this);
    }
}

/////////////////////(^_^)/////////////////////(^_^)/////////////////////(^_^)/////////////////////

map<string, double> IPV4BytesReceived;
map<string, double> throughput;
map<int, int> numberOfPacketLoss;
map<Address, double> dataAtSink;

Ptr<Socket> Flow(string type, Address sinkAddress, uint sinkPort,Ptr<Node> hostNode, Ptr<Node> sinkNode, double startTime) {

	if(type.compare("hybla") == 0) {
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpHybla::GetTypeId()));
	} else if(type.compare("westwood") == 0) {
        Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpWestwood::GetTypeId()));
	} else if(type.compare("yeah") == 0) {
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpYeah::GetTypeId()));
	}

	PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
	sinkApps.Start(Seconds(startTime));
	sinkApps.Stop(Seconds(startTime+duration));

	Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId());	
	Ptr<ApnaApp> app = CreateObject<ApnaApp>();
	app->Setup(ns3TcpSocket, sinkAddress, packetSizeMax, total_num_of_Packets, DataRate(TransferRate));
	hostNode->AddApplication(app);
	app->SetStartTime(Seconds(startTime));
	app->SetStopTime(Seconds(startTime+duration));
	return ns3TcpSocket;
}

void ThroughputData(FILE *stream, double startTime, string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint interface){
	double time = Simulator::Now().GetSeconds();	

	IPV4BytesReceived[context] += packet->GetSize();
	double curSpeed = (((IPV4BytesReceived[context] * 8.0) / 1024)/(time-startTime));
    fprintf(stream, "%s,%s\n",(to_string( time-startTime).c_str()), (to_string(curSpeed )).c_str() );
    throughput[context] = max(curSpeed,throughput[context]);
}

void GoodputData(FILE *stream, double startTime, string context, Ptr<const Packet> packet, const Address& addr){
	double time = Simulator::Now().GetSeconds();

	dataAtSink[addr] += packet->GetSize();

    double speed = (((dataAtSink[addr] * 8.0) / 1024)/(time-startTime));
    fprintf(stream, "%s,%s\n",(to_string( time-startTime).c_str() ) , (to_string(speed )).c_str() );
}

static void UpdateCwnd(FILE *stream, double startTime, uint oldCwnd, uint newCwnd){
    fprintf(stream, "%s,%s\n",(to_string(Simulator::Now ().GetSeconds () - startTime)).c_str(), (to_string(newCwnd )).c_str());
}

static void CapturePacketDrop(uint myId){
	numberOfPacketLoss[myId]++;
}

int main(){

    string router_to_host_speed = "100Mbps";
	string router_to_host_latency = "20ms";
	string router_to_router_speed = "10Mbps";
	string router_to_router_latency = "50ms";

    portNo = 7000;
    double timegone = 0;
    duration = 1000;
    total_num_of_Packets = 1000000;
    packetSizeMax = 1024*1.3;
    TransferRate = "40Mbps";

    // Time::SetResolution (Time::NS);
    Ptr<RateErrorModel> errM = CreateObjectWithAttributes<RateErrorModel> ("ErrorRate", DoubleValue (0.00001));

    PointToPointHelper rtr, htr;

    htr.SetChannelAttribute("Delay", StringValue(router_to_host_latency));
    rtr.SetChannelAttribute("Delay", StringValue(router_to_router_latency));

    htr.SetDeviceAttribute("DataRate", StringValue(router_to_host_speed));
    rtr.SetDeviceAttribute("DataRate", StringValue(router_to_router_speed));

    htr.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize("250000B")));
  
    rtr.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue (QueueSize("62500B")));

    NodeContainer nodes;

    nodes.Create(8);

    NodeContainer h1r1 = NodeContainer(nodes.Get(0),nodes.Get(3)); 
    NodeContainer h2r1 = NodeContainer(nodes.Get(1),nodes.Get(3));
    NodeContainer h3r1 = NodeContainer(nodes.Get(2),nodes.Get(3));
    NodeContainer h4r2 = NodeContainer(nodes.Get(5),nodes.Get(4));
    NodeContainer h5r2 = NodeContainer(nodes.Get(6),nodes.Get(4));
    NodeContainer h6r2 = NodeContainer(nodes.Get(7),nodes.Get(4));
    NodeContainer r1r2 = NodeContainer(nodes.Get(3),nodes.Get(4));

    InternetStackHelper stackInternet;
    stackInternet.Install(nodes);

    NetDeviceContainer ndc_h1r1 = htr.Install(h1r1);
    NetDeviceContainer ndc_h2r1 = htr.Install(h2r1);
    NetDeviceContainer ndc_h3r1 = htr.Install(h3r1);
    NetDeviceContainer ndc_h4r2 = htr.Install(h4r2);
    NetDeviceContainer ndc_h5r2 = htr.Install(h5r2);
    NetDeviceContainer ndc_h6r2 = htr.Install(h6r2);
    NetDeviceContainer ndc_r1r2 = rtr.Install(r1r2);

    ndc_h1r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));
    ndc_h2r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));
    ndc_h3r1.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));
    ndc_h4r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));
    ndc_h5r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));
    ndc_h6r2.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(errM));

    Ipv4AddressHelper ipv4helper;

    ipv4helper.SetBase("171.20.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h1r1 = ipv4helper.Assign (ndc_h1r1);
    ipv4helper.SetBase ("171.20.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h2r1 = ipv4helper.Assign (ndc_h2r1);
    ipv4helper.SetBase ("171.20.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h3r1 = ipv4helper.Assign (ndc_h3r1);
    ipv4helper.SetBase ("171.20.5.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h4r2 = ipv4helper.Assign (ndc_h4r2);
    ipv4helper.SetBase ("171.20.6.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h5r2 = ipv4helper.Assign (ndc_h5r2);
    ipv4helper.SetBase ("171.20.7.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_h6r2 = ipv4helper.Assign (ndc_h6r2);
    ipv4helper.SetBase ("171.20.4.0", "255.255.255.0");
    Ipv4InterfaceContainer ipv4_r1r2 = ipv4helper.Assign (ndc_r1r2);


    FILE *h1hyblaCongestionWindowFile = fopen("hybla_congestionwindow_partA.txt", "w");
    FILE *h1hyblaThroughPutFile = fopen("hybla_throughput_partA.txt", "w");
    FILE *h1hyblaGoodPutFile = fopen("hybla_goodput_partA.txt", "w");

    FILE *h2westwoodCongestionWindowFile = fopen("westwood_congestionwindow_partA.txt", "w");
    FILE *h2westwoodThroughPutFile = fopen("westwood_throughput_partA.txt", "w");
    FILE *h2westwoodGoodPutFile = fopen("westwood_goodput_partA.txt", "w");

    FILE *h3CongestionWindowFile = fopen("yeah_congestionwindow_partA.txt", "w");
    FILE *h3ThroughPutFile = fopen("yeah_throughput_partA.txt", "w");
    FILE *h3GoodPutFile = fopen("yeah_goodput_partA.txt", "w");

    //////
    Ptr<Socket> YeahSocket = Flow("yeah",InetSocketAddress(ipv4_h4r2.GetAddress(0), portNo), portNo, nodes.Get(0), nodes.Get(5), timegone);
    Config::Connect("/NodeList/5/ApplicationList/0/$ns3::PacketSink/Rx", MakeBoundCallback(&GoodputData, h3GoodPutFile, timegone));
	Config::Connect("/NodeList/5/$ns3::Ipv4L3Protocol/Rx", MakeBoundCallback(&ThroughputData, h3ThroughPutFile, timegone));
    YeahSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&CapturePacketDrop, 1));
    YeahSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&UpdateCwnd, h3CongestionWindowFile, timegone));
    /////

    //////////////////////////////////
    Ptr<Socket> HyblaSocket = Flow("hybla",InetSocketAddress(ipv4_h5r2.GetAddress(0), portNo), portNo, nodes.Get(1), nodes.Get(6), timegone);
	Config::Connect("/NodeList/6/ApplicationList/0/$ns3::PacketSink/Rx", MakeBoundCallback(&GoodputData, h1hyblaGoodPutFile, timegone));
	Config::Connect("/NodeList/6/$ns3::Ipv4L3Protocol/Rx", MakeBoundCallback(&ThroughputData, h1hyblaThroughPutFile, timegone));
    HyblaSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&CapturePacketDrop, 2));
    HyblaSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&UpdateCwnd, h1hyblaCongestionWindowFile, timegone+50));
    //////////////////////////////////

    /////
    Ptr<Socket> WestwoodSocket = Flow("westwood",InetSocketAddress(ipv4_h6r2.GetAddress(0), portNo), portNo, nodes.Get(2), nodes.Get(7), timegone);
    Config::Connect("/NodeList/7/ApplicationList/0/$ns3::PacketSink/Rx", MakeBoundCallback(&GoodputData, h2westwoodGoodPutFile, timegone));
	Config::Connect("/NodeList/7/$ns3::Ipv4L3Protocol/Rx", MakeBoundCallback(&ThroughputData, h2westwoodThroughPutFile, timegone));
    WestwoodSocket->TraceConnectWithoutContext("Drop", MakeBoundCallback (&CapturePacketDrop, 3));
    WestwoodSocket->TraceConnectWithoutContext("CongestionWindow", MakeBoundCallback (&UpdateCwnd, h2westwoodCongestionWindowFile, timegone+50));
    /////
    timegone += duration;

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    Ptr<FlowMonitor> flowM;
	FlowMonitorHelper flowMHelper;
	flowM = flowMHelper.InstallAll();
	Simulator::Stop(Seconds(timegone+50));
	Simulator::Run();
	flowM->CheckForLostPackets();
	Ptr<Ipv4FlowClassifier> ipv4classifier = DynamicCast<Ipv4FlowClassifier>(flowMHelper.GetClassifier());
	map<FlowId, FlowMonitor::FlowStats> flowStats = flowM->GetFlowStats();

    for (map<FlowId, FlowMonitor::FlowStats>::const_iterator i = flowStats.begin(); i != flowStats.end(); ++i){
		Ipv4FlowClassifier::FiveTuple tempClassifier = ipv4classifier->FindFlow (i->first);
        if(tempClassifier.sourceAddress=="171.20.1.1"){

		    cout<<"TCP Yeah flow"<<(to_string( i->first)).c_str()<<"(171.20.1.1 -> 171.20.5.1)"<<endl;
            cout<<"Max throughput(in kbps):"<<(to_string( throughput["/NodeList/5/$ns3::Ipv4L3Protocol/Rx"] )).c_str()<<endl;
            cout<<"Total Packets transmitted:"<<(to_string( total_num_of_Packets )).c_str()<<endl;
            cout<<"Packets Successfully Transferred:"<<(to_string(total_num_of_Packets- i->second.lostPackets )).c_str()<<endl;
            cout<<"Total Packet Lost:"<<(to_string( i->second.lostPackets)).c_str()<<endl;
            cout<<"Packet Lost due to overflow of buffer:"<<(to_string( numberOfPacketLoss[1] )).c_str()<<endl;
            cout<<"Packet Lost due to Congestion:"<<(to_string( i->second.lostPackets - numberOfPacketLoss[1] )).c_str()<<endl;
            cout<<endl;
		}else if(tempClassifier.sourceAddress=="171.20.2.1"){

            cout<<"TCP Hybla flow"<<(to_string( i->first)).c_str()<<"(171.20.2.1 -> 171.20.6.1)"<<endl;
            cout<<"Max throughput(in kbps):"<<(to_string( throughput["/NodeList/6/$ns3::Ipv4L3Protocol/Rx"] )).c_str()<<endl;
            cout<<"Total Packets transmitted:"<<(to_string( total_num_of_Packets )).c_str()<<endl;
            cout<<"Packets Successfully Transferred:"<<(to_string(total_num_of_Packets- i->second.lostPackets )).c_str()<<endl;
            cout<<"Total Packet Lost:"<<(to_string( i->second.lostPackets)).c_str()<<endl;
            cout<<"Packet Lost due to overflow of buffer:"<<(to_string( numberOfPacketLoss[2] )).c_str()<<endl;
            cout<<"Packet Lost due to Congestion:"<<(to_string( i->second.lostPackets - numberOfPacketLoss[2] )).c_str()<<endl;
            cout<<endl;
        }else if(tempClassifier.sourceAddress=="171.20.3.1"){

            cout<<"TCP Westwood flow "<<(to_string( i->first)).c_str()<<"(171.20.2.1 -> 171.20.6.1)"<<endl;
            cout<<"Max throughput(in kbps):"<<(to_string( throughput["/NodeList/7/$ns3::Ipv4L3Protocol/Rx"] )).c_str()<<endl;
            cout<<"Total Packets transmitted:"<<(to_string( total_num_of_Packets )).c_str()<<endl;
            cout<<"Packets Successfully Transferred:"<<(to_string(total_num_of_Packets- i->second.lostPackets )).c_str()<<endl;
            cout<<"Total Packet Lost:"<<(to_string( i->second.lostPackets)).c_str()<<endl;
            cout<<"Packet Lost due to overflow of buffer:"<<(to_string( numberOfPacketLoss[3] )).c_str()<<endl;
            cout<<"Packet Lost due to Congestion:"<<(to_string( i->second.lostPackets - numberOfPacketLoss[3] )).c_str()<<endl;
        }
	}

    Simulator::Destroy();
    return 0;
}



