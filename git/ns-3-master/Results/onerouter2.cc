
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-global-routing-helper.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Dctcp");

// ===========================================================================
//
//         node 0                 node 1
//   +----------------+    +----------------+
//   |    ns-3 TCP    |    |    ns-3 TCP    |
//   +----------------+    +----------------+
//   |    10.1.1.1    |    |    10.1.1.2    |
//   +----------------+    +----------------+
//   | point-to-point |    | point-to-point |
//   +----------------+    +----------------+
//           |                     |
//           +---------------------+
//                10 Mbps, 2 ms
//
//
// We want to look at changes in the ns-3 TCP congestion window.So, we can cook
// up a simple version of the on-off application that does what we want.  
//
// So first, we create a socket and do the trace connect on it; then we pass 
// this socket into the constructor of our simple application which we then 
// install in the source node.
// ===========================================================================
//
class MyApp : public Application 
{
public:

  MyApp ();
  virtual ~MyApp();

  void Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ScheduleTx (void);
  void SendPacket (void);

  Ptr<Socket>     m_socket;
  Address         m_peer;
  uint32_t        m_packetSize;
  uint32_t        m_nPackets;
  DataRate        m_dataRate;
  EventId         m_sendEvent;
  bool            m_running;
  uint32_t        m_packetsSent;
};

MyApp::MyApp ()
  : m_socket (0), 
    m_peer (), 
    m_packetSize (0), 
    m_nPackets (0), 
    m_dataRate (0), 
    m_sendEvent (), 
    m_running (false), 
    m_packetsSent (0)
{
}

MyApp::~MyApp()
{
  m_socket = 0;
}

void
MyApp::Setup (Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate)
{
  m_socket = socket;
  m_peer = address;
  m_packetSize = packetSize;
  m_nPackets = nPackets;
  m_dataRate = dataRate;
}

void
MyApp::StartApplication (void)
{
  m_running = true;
  m_packetsSent = 0;
  m_socket->Bind ();
  m_socket->Connect (m_peer);
  SendPacket ();
}

void 
MyApp::StopApplication (void)
{
  m_running = false;

  if (m_sendEvent.IsRunning ())
    {
      Simulator::Cancel (m_sendEvent);
    }

  if (m_socket)
    {
      m_socket->Close ();
    }
}

void 
MyApp::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);

  if (++m_packetsSent < m_nPackets)
    {
      ScheduleTx ();
    }
}

void 
MyApp::ScheduleTx (void)
{
  if (m_running)
    {
      Time tNext (Seconds (m_packetSize * 8 / static_cast<double> (m_dataRate.GetBitRate ())));
      m_sendEvent = Simulator::Schedule (tNext, &MyApp::SendPacket, this);
    }
}

static void
CwndChange (uint32_t oldCwnd, uint32_t newCwnd)
{
  NS_LOG_UNCOND (Simulator::Now ().GetSeconds () << "\t" << newCwnd);
}
void
CheckQueueSize1 (Ptr<QueueDisc> queue)
{
  uint32_t qSize = queue->GetCurrentSize ().GetValue ();

  Simulator::Schedule (Seconds (0.004), &CheckQueueSize1, queue);

  std::ofstream fPlotQueue (std::stringstream ("./queue-1.plotme").str ().c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}
void
TraceCwnd (uint32_t node, uint32_t cwndWindow,
           Callback <void, uint32_t, uint32_t> CwndTrace)
{
  Config::ConnectWithoutContext ("/NodeList/" + std::to_string (node) + "/$ns3::TcpL4Protocol/SocketList/" + std::to_string (cwndWindow) + "/CongestionWindow", CwndTrace);
}
static void CwndChangeS10 (uint32_t oldCwnd, uint32_t newCwnd)
{
  std::ofstream fPlotQueue ("./rtt.plotme", std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " <<abs(newCwnd/4965.0) << std::endl;
  fPlotQueue.close ();
}

void InstallBulkSend (Ptr<Node> node, Address address, uint16_t port, 
                      uint32_t nodeId, uint32_t cwndWindow,
                      Callback <void, uint32_t, uint32_t> CwndTrace)
{

  BulkSendHelper source ("ns3::TcpSocketFactory", 
                         address);

  source.SetAttribute ("MaxBytes", UintegerValue (0));
  ApplicationContainer sourceApps = source.Install (node);
  Time timeToStart = Seconds(0.);
  sourceApps.Start (Seconds(0.));
  Simulator::Schedule (timeToStart + Seconds (0.0049), &TraceCwnd, nodeId, cwndWindow, CwndTrace);
  sourceApps.Stop (Seconds(1000.));
}
int 
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);
  
  NodeContainer nodes;
  nodes.Create (3);
  PointToPointHelper pointToPoint1, pointToPoint2;
  pointToPoint1.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  pointToPoint1.SetChannelAttribute ("Delay", StringValue ("2ms"));

  pointToPoint2.SetDeviceAttribute ("DataRate", StringValue ("1Mbps"));
  pointToPoint2.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices1,devices2;
  devices1 = pointToPoint1.Install (nodes.Get(0),nodes.Get(1));
  devices2 = pointToPoint2.Install (nodes.Get(1),nodes.Get(2));
 
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (false));
  // RED params
  NS_LOG_INFO ("Set RED params");
  Config::SetDefault ("ns3::RedQueueDisc::Mode", StringValue ("QUEUE_MODE_PACKETS"));
  Config::SetDefault ("ns3::RedQueueDisc::MeanPktSize", UintegerValue (12000));
  Config::SetDefault ("ns3::RedQueueDisc::QW", DoubleValue (1.0));
  Config::SetDefault ("ns3::RedQueueDisc::UseMarkP", BooleanValue (true));
  Config::SetDefault ("ns3::RedQueueDisc::MarkP", DoubleValue (2.0));
  Config::SetDefault ("ns3::RedQueueDisc::MinTh", DoubleValue (17));
  Config::SetDefault ("ns3::RedQueueDisc::MaxTh", DoubleValue (17));
  Config::SetDefault ("ns3::RedQueueDisc::QueueLimit", UintegerValue (100));

  // TCP params
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (12000));
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType", StringValue ("ns3::TcpNewReno"));

  // enable ECN
  Config::SetDefault ("ns3::TcpSocketBase::UseEcn", BooleanValue (true));
  Config::SetDefault ("ns3::RedQueueDisc::UseEcn", BooleanValue (true));

  Config::SetDefault ("ns3::DctcpSocket::DctcpWeight", DoubleValue (1.0 / 16));

  Config::SetDefault ("ns3::TcpL4Protocol::SocketBaseType", TypeIdValue(TypeId::LookupByName ("ns3::D2tcpSocket")));





 InternetStackHelper stack;
  stack.Install (nodes);

  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc ("ns3::RedQueueDisc");

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.252");
  Ipv4InterfaceContainer interfaces1 = address.Assign (devices1);
  address.NewNetwork();
  Ipv4InterfaceContainer interfaces2 = address.Assign (devices2);
  address.NewNetwork();
 
 Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  uint16_t sinkPort = 8080;
  Address sinkAddress (InetSocketAddress (interfaces2.GetAddress (1), sinkPort));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (2));
  sinkApps.Start (Seconds (0.));
  sinkApps.Stop (Seconds (1000.));

  InstallBulkSend (nodes.Get (0), sinkAddress, sinkPort, 0, 0, MakeCallback (&CwndChangeS10));
  pointToPoint2.EnablePcapAll("./N",true);
  tchRed.Uninstall(devices2);
  
  QueueDiscContainer qd1,qd2,qd3;
  qd2=tchRed.Install(devices2);
  
  Simulator::ScheduleNow (&CheckQueueSize1, qd2.Get (0));

  Simulator::Stop (Seconds (1000));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}

