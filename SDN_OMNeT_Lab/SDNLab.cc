#include <omnetpp.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace omnetpp;

class Host : public cSimpleModule
{
  private:
    cMessage *sendTimer = nullptr;
    int packetsSent = 0;
    int packetsReceived = 0;
    long receivedBytes = 0;
    simtime_t totalDelay = SIMTIME_ZERO;
    simtime_t firstPacketSent = SIMTIME_ZERO;
    simtime_t firstPacketCreation = SIMTIME_ZERO;
    simtime_t lastPacketArrival = SIMTIME_ZERO;
    cOutVector packetsSentVector;
    cOutVector packetsReceivedVector;
    cOutVector endToEndDelayVector;
    cOutVector receivedBytesVector;

  protected:
    void initialize() override
    {
        packetsSentVector.setName("packetsSent");
        packetsReceivedVector.setName("packetsReceived");
        endToEndDelayVector.setName("endToEndDelay_s");
        receivedBytesVector.setName("receivedBytes");

        if (par("isSender").boolValue()) {
            sendTimer = new cMessage("sendTimer");
            scheduleAt(simTime() + par("startTime"), sendTimer);
        }
    }

    void handleMessage(cMessage *msg) override
    {
        if (msg == sendTimer) {
            int packetCount = par("packetCount").intValue();
            if (packetsSent < packetCount) {
                const char *destination = par("destination").stringValue();
                std::string packetName = "DATA|" + std::string(destination) + "|" + std::to_string(packetsSent + 1);
                auto *packet = new cPacket(packetName.c_str());
                packet->setByteLength(par("packetBytes").intValue());

                EV_INFO << getName() << ": sending packet " << (packetsSent + 1)
                        << " (" << packet->getByteLength() << " bytes) to " << destination << "\n";
                if (packetsSent == 0)
                    firstPacketSent = simTime();
                packetsSent++;
                packetsSentVector.record(packetsSent);
                send(packet, "out");

                if (packetsSent < packetCount)
                    scheduleAt(simTime() + par("sendInterval"), sendTimer);
                else {
                    cancelAndDelete(sendTimer);
                    sendTimer = nullptr;
                }
            }
            return;
        }

        auto *packet = check_and_cast<cPacket *>(msg);
        packetsReceived++;
        receivedBytes += packet->getByteLength();
        simtime_t delay = simTime() - packet->getCreationTime();
        totalDelay += delay;
        if (packetsReceived == 1)
            firstPacketCreation = packet->getCreationTime();
        lastPacketArrival = simTime();

        packetsReceivedVector.record(packetsReceived);
        endToEndDelayVector.record(delay.dbl());
        receivedBytesVector.record(receivedBytes);
        EV_INFO << getName() << ": received " << packet->getName()
                << "; end-to-end delay=" << delay << "s\n";
        delete packet;
    }

    void finish() override
    {
        if (sendTimer != nullptr) {
            cancelAndDelete(sendTimer);
            sendTimer = nullptr;
        }

        recordScalar("packetsSent", packetsSent);
        recordScalar("packetsReceived", packetsReceived);
        if (packetsReceived > 0) {
            double averageDelay = totalDelay.dbl() / packetsReceived;
            double measurementInterval = (lastPacketArrival - firstPacketCreation).dbl();
            double throughput = measurementInterval > 0
                    ? receivedBytes * 8.0 / measurementInterval
                    : 0.0;
            recordScalar("averageEndToEndDelay_s", averageDelay);
            recordScalar("receivedThroughput_bps", throughput);
        }
    }

  public:
    long getPacketsSent() const { return packetsSent; }
    long getPacketsReceived() const { return packetsReceived; }
    long getReceivedBytes() const { return receivedBytes; }
    double getTotalDelaySeconds() const { return totalDelay.dbl(); }
    simtime_t getFirstPacketSentTime() const { return firstPacketSent; }
    simtime_t getLastPacketArrivalTime() const { return lastPacketArrival; }
};

Define_Module(Host);

class SdnSwitch : public cSimpleModule
{
  private:
    std::map<std::string, int> flowTable;
    std::map<std::string, std::vector<cMessage *>> pendingPackets;
    std::set<std::string> pendingRequests;
    long dataPacketsSeen = 0;
    long flowTableHits = 0;
    long tableMisses = 0;
    long flowRulesInstalled = 0;
    long packetsForwarded = 0;
    long bufferedPacketsForwarded = 0;
    cOutVector flowTableHitsVector;
    cOutVector tableMissesVector;
    cOutVector packetsForwardedVector;

    static std::string getDestination(const char *packetName)
    {
        std::string name(packetName);
        size_t first = name.find('|');
        size_t second = name.find('|', first == std::string::npos ? first : first + 1);
        if (first == std::string::npos)
            return "";
        return name.substr(first + 1, second == std::string::npos ? second : second - first - 1);
    }

  protected:
    void initialize() override
    {
        flowTableHitsVector.setName("flowTableHits");
        tableMissesVector.setName("tableMisses");
        packetsForwardedVector.setName("packetsForwarded");
    }

    void handleMessage(cMessage *msg) override
    {
        if (msg->getArrivalGate() == gate("ctrlIn")) {
            std::string command(msg->getName());
            delete msg;

            // FLOW_MOD|destination|output-port
            size_t first = command.find('|');
            size_t second = command.find('|', first + 1);
            std::string destination = command.substr(first + 1, second - first - 1);
            int outputPort = std::stoi(command.substr(second + 1));

            flowTable[destination] = outputPort;
            flowRulesInstalled++;
            pendingRequests.erase(destination);
            EV_INFO << getName() << ": installed rule " << destination
                    << " -> port " << outputPort << "\n";

            auto pending = pendingPackets.find(destination);
            if (pending != pendingPackets.end()) {
                for (cMessage *packet : pending->second) {
                    packetsForwarded++;
                    bufferedPacketsForwarded++;
                    packetsForwardedVector.record(packetsForwarded);
                    EV_INFO << getName() << ": forwarding buffered " << packet->getName() << "\n";
                    send(packet, "hostOut", outputPort);
                }
                pendingPackets.erase(pending);
            }
            return;
        }

        dataPacketsSeen++;
        std::string destination = getDestination(msg->getName());
        auto rule = flowTable.find(destination);
        if (rule != flowTable.end()) {
            flowTableHits++;
            flowTableHitsVector.record(flowTableHits);
            packetsForwarded++;
            packetsForwardedVector.record(packetsForwarded);
            EV_INFO << getName() << ": table hit for " << destination
                    << ", forwarding on port " << rule->second << "\n";
            send(msg, "hostOut", rule->second);
            return;
        }

        tableMisses++;
        tableMissesVector.record(tableMisses);
        EV_INFO << getName() << ": table miss for " << destination
                << "; buffering packet and sending Packet-In\n";
        pendingPackets[destination].push_back(msg);
        if (pendingRequests.insert(destination).second) {
            std::string packetIn = "PACKET_IN|" + std::string(getName()) + "|" + destination;
            send(new cMessage(packetIn.c_str()), "ctrlOut");
        }
    }

    void finish() override
    {
        recordScalar("dataPacketsSeen", dataPacketsSeen);
        recordScalar("flowTableHits", flowTableHits);
        recordScalar("tableMisses", tableMisses);
        recordScalar("flowRulesInstalled", flowRulesInstalled);
        recordScalar("packetsForwarded", packetsForwarded);
        recordScalar("bufferedPacketsForwarded", bufferedPacketsForwarded);
        double forwardingRatio = dataPacketsSeen > 0
                ? static_cast<double>(packetsForwarded) / dataPacketsSeen
                : 0.0;
        recordScalar("switchForwardingRatio", forwardingRatio);

        for (auto &entry : pendingPackets)
            for (cMessage *packet : entry.second)
                cancelAndDelete(packet);
    }
};

Define_Module(SdnSwitch);

class Controller : public cSimpleModule
{
  private:
    struct HostLocation {
        std::string switchName;
        int port;
    };
    long packetInsReceived = 0;
    long flowModsSent = 0;
    cOutVector packetInsVector;
    cOutVector flowModsVector;

  protected:
    void initialize() override
    {
        packetInsVector.setName("packetInsReceived");
        flowModsVector.setName("flowModsSent");
    }

    void handleMessage(cMessage *msg) override
    {
        std::string packetIn(msg->getName());
        delete msg;
        packetInsReceived++;
        packetInsVector.record(packetInsReceived);

        // PACKET_IN|switch-name|destination-host
        size_t first = packetIn.find('|');
        size_t second = packetIn.find('|', first + 1);
        std::string sourceSwitch = packetIn.substr(first + 1, second - first - 1);
        std::string destination = packetIn.substr(second + 1);

        const std::map<std::string, HostLocation> hostLocations = {
            {"hostA", {"s1", 0}}, {"hostB", {"s1", 1}}, {"hostC", {"s1", 2}},
            {"hostD", {"s2", 0}}, {"hostE", {"s2", 1}}, {"hostF", {"s2", 2}}
        };
        auto location = hostLocations.find(destination);
        if (location == hostLocations.end()) {
            EV_WARN << "Controller: unknown destination " << destination << "; no rule installed\n";
            return;
        }

        int outputPort = location->second.switchName == sourceSwitch ? location->second.port : 3;
        int controlPort = sourceSwitch == "s1" ? 0 : 1;
        EV_INFO << "Controller: installing on " << sourceSwitch << " rule "
                << destination << " -> port " << outputPort << "\n";
        std::string flowMod = "FLOW_MOD|" + destination + "|" + std::to_string(outputPort);
        send(new cMessage(flowMod.c_str()), "out", controlPort);
        flowModsSent++;
        flowModsVector.record(flowModsSent);
    }

    void finish() override
    {
        recordScalar("packetInsReceived", packetInsReceived);
        recordScalar("flowModsSent", flowModsSent);

        const char *hostNames[] = {"hostA", "hostB", "hostC", "hostD", "hostE", "hostF"};
        long totalPacketsSent = 0;
        long totalPacketsReceived = 0;
        long totalBytesReceived = 0;
        double totalDelaySeconds = 0;
        simtime_t firstTransmission = SIMTIME_ZERO;
        simtime_t lastArrival = SIMTIME_ZERO;
        bool sawTransmission = false;
        bool sawArrival = false;

        for (const char *hostName : hostNames) {
            auto *host = check_and_cast<Host *>(getSystemModule()->getSubmodule(hostName));
            totalPacketsSent += host->getPacketsSent();
            totalPacketsReceived += host->getPacketsReceived();
            totalBytesReceived += host->getReceivedBytes();
            totalDelaySeconds += host->getTotalDelaySeconds();

            if (host->getPacketsSent() > 0 && (!sawTransmission || host->getFirstPacketSentTime() < firstTransmission)) {
                firstTransmission = host->getFirstPacketSentTime();
                sawTransmission = true;
            }
            if (host->getPacketsReceived() > 0 && (!sawArrival || host->getLastPacketArrivalTime() > lastArrival)) {
                lastArrival = host->getLastPacketArrivalTime();
                sawArrival = true;
            }
        }

        recordScalar("totalPacketsSent", totalPacketsSent);
        recordScalar("totalPacketsReceived", totalPacketsReceived);
        recordScalar("totalBytesReceived", totalBytesReceived);
        recordScalar("packetDeliveryRatio_percent", totalPacketsSent > 0
                ? 100.0 * totalPacketsReceived / totalPacketsSent
                : 0.0);
        if (totalPacketsReceived > 0) {
            recordScalar("networkMeanEndToEndDelay_s", totalDelaySeconds / totalPacketsReceived);
            double interval = sawTransmission && sawArrival ? (lastArrival - firstTransmission).dbl() : 0.0;
            recordScalar("aggregateThroughput_bps", interval > 0
                    ? totalBytesReceived * 8.0 / interval
                    : 0.0);
        }
    }
};

Define_Module(Controller);
