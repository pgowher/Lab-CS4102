#include <omnetpp.h>

#include <deque>
#include <map>
#include <sstream>
#include <string>

using namespace omnetpp;

class TrafficSource : public cSimpleModule
{
  private:
    cMessage *sendTimer = nullptr;
    int packetsSent = 0;

  protected:
    void initialize() override
    {
        sendTimer = new cMessage("sendTimer");
        scheduleAt(simTime() + par("startTime"), sendTimer);
    }

    void handleMessage(cMessage *msg) override
    {
        if (msg != sendTimer) {
            delete msg;
            return;
        }

        if (packetsSent >= par("packetCount").intValue())
            return;

        const std::string mode = par("mode").stringValue();
        const std::string sourceName = par("sourceName").stringValue();
        const std::string sourceIP = par("sourceIP").stringValue();
        const std::string destinationIP = par("destinationIP").stringValue();
        const int packetBytes = par("packetBytes").intValue();

        std::ostringstream packetName;
        packetName << "src=" << sourceIP << ";dst=" << destinationIP;
        auto *packet = new cPacket(packetName.str().c_str());
        packet->setByteLength(packetBytes);

        packetsSent++;
        EV_INFO << sourceName << " [" << mode << "] sends " << packetBytes << " bytes from " << sourceIP
                << " to " << destinationIP << "\n";
        send(packet, "out");

        if (packetsSent < par("packetCount").intValue())
            scheduleAt(simTime() + par("sendInterval"), sendTimer);
        else {
            cancelAndDelete(sendTimer);
            sendTimer = nullptr;
        }
    }

    void finish() override
    {
        if (sendTimer != nullptr) {
            cancelAndDelete(sendTimer);
            sendTimer = nullptr;
        }
        recordScalar("packetsGenerated", packetsSent);
    }
};

Define_Module(TrafficSource);

class Firewall : public cSimpleModule
{
  private:
    std::map<std::string, std::deque<simtime_t>> recentPacketsByIP;
    long packetsInspected = 0;
    long packetsForwarded = 0;
    long packetsDropped = 0;
    long dropsByIPRule = 0;
    long dropsBySizeRule = 0;
    long dropsByRateRule = 0;
    cOutVector inspectedVector;
    cOutVector forwardedVector;
    cOutVector droppedVector;

    static std::map<std::string, std::string> parseFields(const char *packetName)
    {
        std::map<std::string, std::string> fields;
        std::istringstream input(packetName);
        std::string field;
        while (std::getline(input, field, ';')) {
            size_t equals = field.find('=');
            if (equals != std::string::npos)
                fields[field.substr(0, equals)] = field.substr(equals + 1);
        }
        return fields;
    }

    void dropPacket(cPacket *packet, long& ruleCounter, const char *reason)
    {
        packetsDropped++;
        ruleCounter++;
        droppedVector.record(packetsDropped);
        EV_WARN << "FIREWALL DROP: " << reason << "; " << packet->getName()
                << "; size=" << packet->getByteLength() << " bytes\n";
        delete packet;
    }

  protected:
    void initialize() override
    {
        inspectedVector.setName("packetsInspected");
        forwardedVector.setName("packetsForwarded");
        droppedVector.setName("packetsDropped");
    }

    void handleMessage(cMessage *msg) override
    {
        auto *packet = check_and_cast<cPacket *>(msg);
        packetsInspected++;
        inspectedVector.record(packetsInspected);

        const auto fields = parseFields(packet->getName());
        const std::string sourceIP = fields.count("src") ? fields.at("src") : "unknown";
        const std::string blockedIP = par("blockedIP").stringValue();

        if (par("enableIPRule").boolValue() && sourceIP == blockedIP) {
            dropPacket(packet, dropsByIPRule, "source IP matches blockedIP rule");
            return;
        }

        if (par("enableSizeRule").boolValue() &&
                packet->getByteLength() > par("maxPacketBytes").intValue()) {
            dropPacket(packet, dropsBySizeRule, "packet exceeds maxPacketBytes");
            return;
        }

        if (par("enableRateRule").boolValue()) {
            auto& arrivals = recentPacketsByIP[sourceIP];
            const simtime_t cutoff = simTime() - par("rateWindow");
            while (!arrivals.empty() && arrivals.front() < cutoff)
                arrivals.pop_front();
            arrivals.push_back(simTime());

            if (static_cast<int>(arrivals.size()) > par("maxPacketsPerWindow").intValue()) {
                dropPacket(packet, dropsByRateRule, "source exceeds maxPacketsPerWindow");
                return;
            }
        }

        packetsForwarded++;
        forwardedVector.record(packetsForwarded);
        EV_INFO << "FIREWALL ALLOW: " << packet->getName() << "; size="
                << packet->getByteLength() << " bytes\n";
        send(packet, "out");
    }

    void finish() override
    {
        recordScalar("packetsInspected", packetsInspected);
        recordScalar("packetsForwarded", packetsForwarded);
        recordScalar("packetsDropped", packetsDropped);
        recordScalar("dropsByIPRule", dropsByIPRule);
        recordScalar("dropsBySizeRule", dropsBySizeRule);
        recordScalar("dropsByRateRule", dropsByRateRule);
        recordScalar("dropRate_percent", packetsInspected > 0
                ? 100.0 * packetsDropped / packetsInspected : 0.0);
    }
};

Define_Module(Firewall);

class ProtectedServer : public cSimpleModule
{
  private:
    long packetsReceived = 0;
    long bytesReceived = 0;
    cOutVector receivedVector;

  protected:
    void initialize() override { receivedVector.setName("packetsReceived"); }

    void handleMessage(cMessage *msg) override
    {
        auto *packet = check_and_cast<cPacket *>(msg);
        packetsReceived++;
        bytesReceived += packet->getByteLength();
        receivedVector.record(packetsReceived);
        EV_INFO << "Protected server received " << packet->getName() << "\n";
        delete packet;
    }

    void finish() override
    {
        recordScalar("packetsReceived", packetsReceived);
        recordScalar("bytesReceived", bytesReceived);
    }
};

Define_Module(ProtectedServer);
