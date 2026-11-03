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
    int sent = 0;
    int nextPort = 0;

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

        int count = par("packetCount").intValue();
        if (sent >= count)
            return;

        std::string mode = par("mode").stringValue();
        std::string sourceId = par("sourceId").stringValue();
        std::string destination = par("destinationSubnet").stringValue();
        int port = par("basePort").intValue();
        bool authAttempt = false;

        if (mode == "scan")
            port += nextPort++;
        else if (mode == "bruteForce")
            authAttempt = true;

        // The packet exposes ordinary observable fields, not its generating mode.
        std::ostringstream name;
        name << "src=" << sourceId << ";port=" << port << ";auth=" << (authAttempt ? 1 : 0)
             << ";dst=" << destination;
        auto *packet = new cPacket(name.str().c_str());
        packet->setByteLength(par("packetBytes").intValue());

        sent++;
        EV_INFO << sourceId << " sends packet " << sent << " to destination port " << port
                << (authAttempt ? " (authentication attempt)" : "") << "\n";
        send(packet, "out");

        if (sent < count)
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
        recordScalar("packetsGenerated", sent);
    }
};

Define_Module(TrafficSource);

class IDPS : public cSimpleModule
{
  private:
    struct SourceState {
        std::deque<std::pair<simtime_t, int>> recentPackets;
        std::deque<std::pair<simtime_t, int>> recentAuth;
        std::map<int, simtime_t> recentPorts;
        simtime_t blockedUntil = SIMTIME_ZERO;
    };

    std::map<std::string, SourceState> sources;
    long packetsInspected = 0;
    long packetsForwarded = 0;
    long packetsDropped = 0;
    long floodDetections = 0;
    long scanDetections = 0;
    long bruteForceDetections = 0;
    long blockEvents = 0;
    cOutVector inspectedVector;
    cOutVector forwardedVector;
    cOutVector droppedVector;

    static std::map<std::string, std::string> parseFields(const char *packetName)
    {
        std::map<std::string, std::string> fields;
        std::istringstream input(packetName);
        std::string item;
        while (std::getline(input, item, ';')) {
            size_t equals = item.find('=');
            if (equals != std::string::npos)
                fields[item.substr(0, equals)] = item.substr(equals + 1);
        }
        return fields;
    }

    template <typename T>
    static void discardOlderThan(std::deque<T>& events, simtime_t cutoff)
    {
        while (!events.empty() && events.front().first < cutoff)
            events.pop_front();
    }

    void blockSource(const std::string& source, SourceState& state, const char *reason)
    {
        state.blockedUntil = simTime() + par("blockDuration");
        blockEvents++;
        EV_WARN << "IDPS ALERT: " << reason << " detected from " << source
                << "; source blocked until " << state.blockedUntil << "\n";
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

        auto fields = parseFields(packet->getName());
        std::string source = fields["src"];
        int port = fields.count("port") ? std::stoi(fields["port"]) : -1;
        bool authAttempt = fields["auth"] == "1";
        SourceState& state = sources[source];

        if (simTime() < state.blockedUntil) {
            packetsDropped++;
            droppedVector.record(packetsDropped);
            EV_INFO << "IDPS: dropped packet from blocked source " << source << "\n";
            delete packet;
            return;
        }

        // Expire entries on a rolling window so old behavior does not accumulate forever.
        discardOlderThan(state.recentPackets, simTime() - par("rateWindow"));
        state.recentPackets.push_back({simTime(), 0});
        discardOlderThan(state.recentAuth, simTime() - par("authWindow"));
        simtime_t scanCutoff = simTime() - par("scanWindow");
        for (auto it = state.recentPorts.begin(); it != state.recentPorts.end(); ) {
            if (it->second < scanCutoff)
                it = state.recentPorts.erase(it);
            else
                ++it;
        }

        bool detected = false;
        if (port >= 0) {
            state.recentPorts[port] = simTime();
            if (static_cast<int>(state.recentPorts.size()) >= par("scanThreshold").intValue()) {
                scanDetections++;
                blockSource(source, state, "port scan");
                detected = true;
            }
        }

        if (!detected && authAttempt) {
            state.recentAuth.push_back({simTime(), port});
            if (static_cast<int>(state.recentAuth.size()) >= par("authThreshold").intValue()) {
                bruteForceDetections++;
                blockSource(source, state, "repeated authentication attempts");
                detected = true;
            }
        }

        if (!detected && static_cast<int>(state.recentPackets.size()) >= par("floodThreshold").intValue()) {
            floodDetections++;
            blockSource(source, state, "high packet rate");
            detected = true;
        }

        if (detected) {
            packetsDropped++;
            droppedVector.record(packetsDropped);
            delete packet;
            return;
        }

        packetsForwarded++;
        forwardedVector.record(packetsForwarded);
        send(packet, "out");
    }

    void finish() override
    {
        recordScalar("packetsInspected", packetsInspected);
        recordScalar("packetsForwarded", packetsForwarded);
        recordScalar("packetsDropped", packetsDropped);
        recordScalar("floodDetections", floodDetections);
        recordScalar("scanDetections", scanDetections);
        recordScalar("bruteForceDetections", bruteForceDetections);
        recordScalar("sourceBlockEvents", blockEvents);
        recordScalar("preventionRate_percent", packetsInspected > 0
                ? 100.0 * packetsDropped / packetsInspected : 0.0);
    }
};

Define_Module(IDPS);

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

class AccessRouter : public cSimpleModule
{
  protected:
    void handleMessage(cMessage *msg) override
    {
        EV_INFO << par("subnetLabel").stringValue() << " router forwards " << msg->getName() << "\n";
        send(msg, "out");
    }
};

Define_Module(AccessRouter);

class CoreRouter : public cSimpleModule
{
  private:
    static std::string getDestinationSubnet(const char *packetName)
    {
        std::istringstream input(packetName);
        std::string item;
        while (std::getline(input, item, ';')) {
            size_t equals = item.find('=');
            if (equals != std::string::npos && item.substr(0, equals) == "dst")
                return item.substr(equals + 1);
        }
        return "";
    }

  protected:
    void handleMessage(cMessage *msg) override
    {
        std::string destination = getDestinationSubnet(msg->getName());
        if (destination == "dmz") {
            EV_INFO << "Core router selects DMZ subnet\n";
            send(msg, "toDMZ");
        }
        else if (destination == "internal") {
            EV_INFO << "Core router selects internal server LAN\n";
            send(msg, "toLAN");
        }
        else {
            EV_WARN << "Core router drops packet with unknown destination subnet: " << destination << "\n";
            delete msg;
        }
    }
};

Define_Module(CoreRouter);

class NetworkSegment : public cSimpleModule
{
  protected:
    void handleMessage(cMessage *msg) override
    {
        EV_INFO << "Forwarding through " << par("subnetLabel").stringValue() << "\n";
        send(msg, "out");
    }
};

Define_Module(NetworkSegment);
