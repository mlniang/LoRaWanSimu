#include <algorithm>
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include <cstring>
#include "ns3/energy-module.h"
#include <fstream>
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/simple-device-energy-model.h"
#include "ns3/spectrum-module.h"
#include "ns3/stats-module.h"

using namespace ns3;

static uint32_t received = 0;
static Gnuplot2dDataset rdataset("received");
static Gnuplot2dDataset pdataset("power");
static Gnuplot2dDataset sdataset("SM");
static Gnuplot3dDataset tdataset("Temperature");
static Gnuplot2dDataset states("States");

/**
 * This file holds weather data obtained from weather station in
 * Senegal's river. It has been downloaded from http://www.wunderground.com/history/
 * Like in our simulation, data are monitored by 30mns gap
 */
static std::ifstream inputData("data.csv");

//Crop parameters
static const double minMoisture = 255.75; //25%
static const double maxMoisture = 511.5; //50%
static double soilMoisture = 409.2; //40%
static const double minTemp = 19.0;
static const double maxTemp = 25.0;
static double temp = 20.0;
static const double tempInterval = (maxTemp - minTemp) / 3;
static const double maxH = 80.0; //%
static const double minH = 50.0; //%
static double hum = 60.0;
//Actuators
static bool stateA = false;
static bool stateB = false;
//
static double smLastCheckTime = 0.0;

static double interval;

NS_LOG_COMPONENT_DEFINE("WSN");

/*static void StateChangeNotification(std::string context, Time now, LrWpanPhyEnumeration oldState,
                                      LrWpanPhyEnumeration newState) {
  NS_LOG_UNCOND(context << " state change at " << now.GetSeconds()
                         << " from " << LrWpanHelper::LrWpanPhyEnumerationPrinter(oldState)
                         << " to " << LrWpanHelper::LrWpanPhyEnumerationPrinter(newState));
}

static void DataConfirm(McpsDataConfirmParams params) {
  NS_LOG_UNCOND("LrWpanMcpsDataConfirmStatus = " << params.m_status);
}*/

static std::string readData() {
	std::string str;
	std::getline(inputData, str);

	return str;
}

static void updateState(double time) {
	uint8_t state = 0;
	if(stateA) {
		if(stateB)
			state = 15;
		else
			state = 10;
	}
	else if(stateB)
		state = 5;
	states.Add(Time::FromDouble(time*100, Time::S).ToDouble(Time::H), state);
}

/*
 * Soil Moisture drops of 1 each mn if minTemp + 2*tempInterval <= temp <= maxTemp,
 * -2 each 3mns if minTemp + tempInterval <= temp <= minTemp + 2 * tempInterval
 * -1 each 3mns if minTemp <= temp <= minTemp + tempInterval
 * +3/mn when valve A open
 * +1/mn if valve B open
 */
static void updateSM(double time) {

	double mns = Time::FromDouble(time*100, Time::S).ToDouble(Time::MIN) -
			Time::FromDouble(smLastCheckTime*100, Time::S).ToDouble(Time::MIN);
	smLastCheckTime = time;
	//Increasing
	if(stateA)
		soilMoisture += 3 * mns;
	if(stateB)
		soilMoisture += mns;
	//Decreasing
	if(!stateA && !stateB) {
		if(temp <= minTemp + tempInterval)
			soilMoisture -= mns/3;
		else if(temp >= minTemp + tempInterval && temp <= maxTemp-tempInterval)
			soilMoisture -= 2*mns/3;
		else
			soilMoisture -= mns;
	}
	//transitions
	//C->A
	if(!stateA && !stateB && soilMoisture <= minMoisture)
		stateA = true;
	else if(!stateA && !stateB && temp >= maxTemp && hum <= minH && soilMoisture <= minMoisture) // C->O
		stateA = stateB = true;
	else if(soilMoisture >= maxMoisture) // A->C, O->C
		stateA = stateB = false;
	else if(stateB && soilMoisture <= minMoisture) // B->O
		stateA = true;

	sdataset.Add(Time::FromDouble(time*100, Time::S).ToDouble(Time::H), (soilMoisture*100)/1023.0);
}

/*
 * Temperature and RH are taken from 'data.csv'
 */
static void updateTemp(double time) {
	std::string str = readData();
	std::istringstream iss(str);
	double tim;
	iss >> tim >> temp >> hum;
	tdataset.Add(Time::FromDouble(time*100, Time::S).ToDouble(Time::H), temp, hum);

	//updating states
	if(!stateA && !stateB && (temp >= maxTemp || hum <= minH) && //C->B
			(soilMoisture <= minMoisture + 2*(maxMoisture-minMoisture)/5)) {
		stateB = true;
	}
	else if(stateA && hum <= minH - (maxH - minH) / 3 && // A->O
			temp >= maxTemp + (maxTemp - minTemp) / 3) {
		stateB = true;
	}
	else if(stateA && stateB && (hum >= maxH || temp <= maxTemp) // O -> A
			&& soilMoisture < maxMoisture) {
		stateA = true;
		stateB = false;
	}
	else if(stateB && (hum >= maxH || temp <= maxTemp) && // B->C
			(soilMoisture > minMoisture)) {
		stateA = stateB = false;
	}
	else if(stateB && soilMoisture <= minMoisture) { // B -> O
		stateA = true;
	}

	updateState(time);
}

static void DataIndication(McpsDataIndicationParams params, Ptr<Packet> p) {
	received++;
	//NS_LOG_UNCOND("Received packet from = " << params.m_srcAddr);
}

int main(int argc, char* argv[]) {

	//Time::SetResolution(Time::MS);

	const uint8_t packetSize = 10;

	uint16_t nNodes = 30; //Number of nodes to deploy
	uint16_t nodesPerLine = 6;
	double fieldWidth = 9900; //meters
	double fieldHeight = 9900; //meters
	double simTime = 900.0; //As number of seconds in a day / 100
	uint8_t wakesPerDay = 48; //Number of times nodes send data to gateway
	double txPower = -27; //dBm
	uint32_t channelNumber = 11;


	//Command line options
	CommandLine cmd;
	cmd.AddValue("nNodes", "Number of nodes to deploy", nNodes);
	cmd.AddValue("nodesPerLine", "Number of nodes per line of the grid", nodesPerLine);
	cmd.AddValue("fieldWidth", "The width of the field", fieldWidth);
	cmd.AddValue("fieldHeight", "The height of the field", fieldHeight);
	cmd.AddValue("simTime", "The time of the simulation in seconds", simTime);
	cmd.AddValue("wakesPerDay", "Number of times nodes send data to gateway", wakesPerDay);
	cmd.AddValue("channelNumber", "The number of the transmission channel", channelNumber);
	cmd.Parse(argc, argv);


	//Plots
	//Gnuplot rplot = Gnuplot("received.eps");
	//Gnuplot pplot = Gnuplot("power.eps");
	Gnuplot splot = Gnuplot("SM.eps");
	Gnuplot tplot = Gnuplot("temperature.eps");
	Gnuplot stateplot = Gnuplot("states.eps");

	std::ofstream smfile ("SM.plt"); //soil moisture
	std::ofstream tempfile ("temperature.plt");
	std::ofstream statefile("states.plt");


	//Computing values required for nodes
	interval = simTime / wakesPerDay;

	uint16_t rows; //Rows of the grid
	double fw; //Scaled field width
	double fh; //Scaled field height
	double sx; //Scale on X axis
	double sy; //Scale on Y axis
	double minX; //X pos of the first node
	double minY; //Y pos of the first node
	const double SCALE_STEP = 10.0;
	const double SCALE_LIMIT = 300.0;
	//Rows
	rows = nNodes / nodesPerLine;
	if(nNodes % nodesPerLine != 0)
		rows++;
	//Scaling width
	fw = fieldWidth;
	sx = 1.0;
	while (fw > SCALE_LIMIT) {
		fw /= SCALE_STEP;
		sx *= SCALE_STEP;
	}
	minX = fw / (nodesPerLine * 2);
	//Scaling height
	fh = fieldHeight;
	sy = 1.0;
	while (fh > SCALE_LIMIT) {
		fh /= SCALE_STEP;
		sy *= SCALE_STEP;
	}
	minY = fh / (rows * 2);

	//Creation of the Gateway
	Ptr<Node> gateway = CreateObject<Node>();

	//Creation of Nodes
	NodeContainer motes;
	motes.Create(nNodes); //nodes

	NodeContainer ptpNodes;
	ptpNodes.Create(1); //Base Station
	ptpNodes.Add(gateway); //Gateway


	//Helpers
	LrWpanHelper lrwpan;
	PointToPointHelper ptp;
	MobilityHelper mobility;

	//lrwpan.EnableLogComponents();


	//Creation of the wireless channel
	Ptr<SingleModelSpectrumChannel> channel = CreateObject<SingleModelSpectrumChannel>();
	Ptr<LogDistancePropagationLossModel> propModel = CreateObject<LogDistancePropagationLossModel>();
	Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
	channel->AddPropagationLossModel(propModel);
	channel->SetPropagationDelayModel(delayModel);


	//Transmission power
	LrWpanSpectrumValueHelper svh;
	Ptr<SpectrumValue> psd = svh.CreateTxPowerSpectralDensity(txPower, channelNumber);


	//Devices
	//gateway
	Ptr<LrWpanNetDevice> gatewayDevice = CreateObject<LrWpanNetDevice>();
	Mac16Address gatewayAddress = Mac16Address::Allocate(); //We store it because we'll need it later
	gatewayDevice->SetAddress(gatewayAddress);
	gatewayDevice->SetChannel(channel);
	gateway->AddDevice(gatewayDevice);
	//Callbacks
	//gatewayDevice->GetMac()->SetMcpsDataConfirmCallback(MakeCallback(&DataConfirm));
	gatewayDevice->GetMac()->SetMcpsDataIndicationCallback(MakeCallback(&DataIndication));
	//State Changes
	//gatewayDevice->GetPhy()->TraceConnect("TrxState", std::string("phyGateway"), MakeCallback(&StateChangeNotification));

	//Motes
	NetDeviceContainer motesDevices;

	Ptr<LrWpanMac> macs[nNodes];

	//Ptr<SimpleDeviceEnergyModel> sems[nNodes];

	for(uint16_t i = 0; i < nNodes; i++) {
		Ptr<Node> actualMote = motes.Get(i);

		Ptr<LrWpanNetDevice> dev = CreateObject<LrWpanNetDevice>();
		dev->SetAddress(Mac16Address::Allocate());
		dev->SetChannel(channel);

		//Callbacks
		//dev->GetMac()->SetMcpsDataConfirmCallback(MakeCallback(&DataConfirm));
		//dev->GetMac()->SetMcpsDataIndicationCallback(MakeCallback(&DataIndication));

		dev->GetPhy()->SetTxPowerSpectralDensity(psd);

		//Trace state change
		/*char name[9];
		sprintf(name, "phy%u", i);
		dev->GetPhy()->TraceConnect("TrxState", std::string(name), MakeCallback(&StateChangeNotification));*/

		actualMote->AddDevice(dev);
		motesDevices.Add(dev);
		macs[i] = dev->GetMac();
	}

	//PTP Devices (gateway and BS)
	NetDeviceContainer ptpDevices = ptp.Install(ptpNodes);


	// Mobility
	//Nodes Placement
	mobility.SetPositionAllocator("ns3::GridPositionAllocator",
								 "MinX", DoubleValue(minX),
								 "MinY", DoubleValue(minY),
								 "DeltaX", DoubleValue(2 * minX),
								 "DeltaY", DoubleValue(2 * minY),
								 "GridWidth", UintegerValue(nodesPerLine),
								 "LayoutType", StringValue("RowFirst"));
	mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
	mobility.Install(motes);
	//Gateway placed to the middle of the field
	mobility.SetPositionAllocator("ns3::GridPositionAllocator",
								 "MinX", DoubleValue(fw/2),
								 "MinY", DoubleValue(fh/2),
								 "DeltaX", DoubleValue(1.0),
								 "DeltaY", DoubleValue(1.0),
								 "GridWidth", UintegerValue(nodesPerLine),
								 "LayoutType", StringValue("RowFirst"));
	mobility.Install(gateway);
	//Base station is outside the field
	mobility.SetPositionAllocator("ns3::GridPositionAllocator",
									 "MinX", DoubleValue(-20.0),
									 "MinY", DoubleValue(fh/2),
									 "DeltaX", DoubleValue(1.0),
									 "DeltaY", DoubleValue(1.0),
									 "GridWidth", UintegerValue(nodesPerLine),
									 "LayoutType", StringValue("RowFirst"));
	mobility.Install(ptpNodes.Get(0));


	//Tracing
	lrwpan.EnablePcapAll(std::string("tracing/wsn"), true);
	AsciiTraceHelper ascii;
	Ptr<OutputStreamWrapper> stream = ascii.CreateFileStream("tracing/wsn.tr");
	lrwpan.EnableAsciiAll (stream);


	//Animation to use with netanim
	AnimationInterface anim("wsn.xml");


	//Gateway is in blue
	anim.UpdateNodeColor(gateway, 0, 0, 255);
	anim.UpdateNodeDescription(gateway, "");
	anim.UpdateNodeSize(gateway->GetId(), 5.0, 5.0);
	//motes in green
	for(uint16_t i = 0; i < nNodes; i++) {
		Ptr<Node> n = motes.Get(i);
		anim.UpdateNodeColor(n, 0, 255, 0);
		std::ostringstream adr("");
		adr << motesDevices.Get(i)->GetAddress();
		anim.UpdateNodeDescription(n, adr.str().substr(6,5));
		anim.UpdateNodeSize(n->GetId(), 2.5, 2.5);
	}
	//Base station in red
	anim.UpdateNodeDescription(ptpNodes.Get(0), "Base Station");
	anim.UpdateNodeSize(ptpNodes.Get(0)->GetId(), 5.0, 5.0);


	//Simulator::Stop(Seconds(simTime + 1.0));


	Ptr<Packet> p;
	//double dailyPower = (78.30 + 2*5000*1000*wakesPerDay) / 1000000.0;
	for(uint16_t i = 0; i < nNodes; i++) {
		McpsDataRequestParams params;
		params.m_srcAddrMode = SHORT_ADDR;
		params.m_dstAddrMode = SHORT_ADDR;
		params.m_dstPanId = 0;
		params.m_dstAddr = gatewayAddress;
		params.m_msduHandle = 0;
		params.m_txOptions = 0;

		double time = 1.0;
		while(time < simTime + 1.0) {
			p = Create<Packet>(packetSize);
			Simulator::ScheduleWithContext(i, Seconds(time), &LrWpanMac::McpsDataRequest, macs[i], params, p);
			Simulator::Run ();
			time += interval;
		}
		//All transmission day scheduled, we update the power
		//sems[i]->SetCurrentA(dailyPower);
	}
	//We schedule data monitoring on a random node
	Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
	uint32_t randNode = rand->GetInteger(0, nNodes);
	double time = 1.0;
	while(time < simTime + 1.0) {
		Simulator::ScheduleWithContext(randNode, Seconds(time + 0.5), &updateTemp, time + 0.5);
		Simulator::ScheduleWithContext(randNode, Seconds(time + 1.0), &updateSM, time + 1.0);
		Simulator::Run ();
		time += interval;
	}

	splot.AddDataset(sdataset);
	std::ostringstream os;
	os << "Soil Moisture pover time";
	splot.SetTitle(os.str());
	splot.SetTerminal ("postscript eps color enh \"Times-BoldItalic\"");
	splot.SetLegend("time(Time::S)", "Soil moisture");
	splot.SetExtra  ("set xrange [0:200]\n\
					  set yrange [0:1600]\n\
					  set grid\n\
					  set style line 1 linewidth 5\n\
					  set style increment user");
	splot.GenerateOutput(smfile);
	smfile.close();

	tplot.AddDataset(tdataset);
	os.str("");
	os.clear();
	os << "Temperature over time";
	tplot.SetTitle(os.str());
	tplot.SetTerminal ("postscript eps color enh \"Times-BoldItalic\"");
	tplot.SetLegend("time(Time::H)", "Temperature (°C)");
	tplot.SetExtra  ("set xrange [0:200]\n\
					  set yrange [0:40]\n\
					  set grid\n\
					  set style line 1 linewidth 2\n\
					  set style increment user");
	tplot.GenerateOutput(tempfile);
	tempfile.close();

	stateplot.AddDataset(states);
	os.str("");
	os.clear();
	os << "states over time";
	stateplot.SetTitle(os.str ());
	stateplot.SetTerminal ("postscript eps color enh \"Times-BoldItalic\"");
	stateplot.SetLegend("time(Time::H)", "State");
	stateplot.SetExtra  ("set xrange [0:200]\n\
					  set yrange [0:20]\n\
					  set grid\n\
					  set style line 1 linewidth 2\n\
					  set style increment user");
	stateplot.GenerateOutput(statefile);
	statefile.close();

	inputData.close();

	Simulator::Destroy();
	return 0;
}
