#include <omnetpp.h>
#include <algorithm>
#include <deque>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace omnetpp;

static std::vector<std::string> splitFields(const char *text)
{
    std::vector<std::string> fields;
    std::string value(text);
    size_t start = 0;
    while (true) {
        size_t end = value.find('|', start);
        fields.push_back(value.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            return fields;
        start = end + 1;
    }
}

static int getUeIndex(const std::string& name)
{
    if (name == "ueUrllc1") return 0;
    if (name == "ueUrllc2") return 1;
    if (name == "ueEmbb1") return 2;
    if (name == "ueEmbb2") return 3;
    return -1;
}

class UE : public cSimpleModule
{
  private:
    cMessage *registrationTimer = nullptr;
    cMessage *sendTimer = nullptr;
    int packetsSent = 0;
    cOutVector sentVector;
    cOutVector registrationDelayVector;
    cOutVector pduSetupDelayVector;
    simtime_t registrationStart = SIMTIME_ZERO;
    simtime_t pduRequestStart = SIMTIME_ZERO;
    simtime_t registrationDelay = SIMTIME_ZERO;
    simtime_t pduSetupDelay = SIMTIME_ZERO;
    bool hasRegistrationDelay = false;
    bool hasPduSetupDelay = false;

  protected:
    void initialize() override
    {
        sentVector.setName("packetsSent");
        registrationDelayVector.setName("registrationDelay_ms");
        pduSetupDelayVector.setName("pduSessionSetupDelay_ms");
        registrationTimer = new cMessage("registrationTimer");
        scheduleAt(par("startTime"), registrationTimer);
    }

    void handleMessage(cMessage *msg) override
    {
        if (msg == registrationTimer) {
            registrationStart = simTime();
            std::string request = "REG_REQ|" + std::string(getName()) + "|" + par("sliceName").stringValue();
            EV_INFO << getName() << ": Registration Request\n";
            send(new cMessage(request.c_str()), "ctrlOut");
            cancelAndDelete(registrationTimer);
            registrationTimer = nullptr;
            return;
        }

        if (msg == sendTimer) {
            if (packetsSent < par("packetCount").intValue()) {
                std::string packetName = "DATA|" + std::string(getName()) + "|" + par("sliceName").stringValue()
                        + "|" + std::to_string(par("qfi").intValue()) + "|" + std::to_string(packetsSent + 1);
                auto *packet = new cPacket(packetName.c_str());
                packet->setByteLength(par("packetBytes").intValue());
                EV_INFO << getName() << ": sending " << par("sliceName").stringValue()
                        << " QoS packet " << (packetsSent + 1) << "\n";
                send(packet, "dataOut");
                packetsSent++;
                sentVector.record(packetsSent);
                if (packetsSent < par("packetCount").intValue())
                    scheduleAt(simTime() + par("sendInterval"), sendTimer);
                else {
                    cancelAndDelete(sendTimer);
                    sendTimer = nullptr;
                }
            }
            return;
        }

        std::vector<std::string> f = splitFields(msg->getName());
        if (!f.empty() && f[0] == "REG_ACCEPT") {
            registrationDelay = simTime() - registrationStart;
            hasRegistrationDelay = true;
            registrationDelayVector.record(registrationDelay.dbl() * 1000.0);
            pduRequestStart = simTime();
            std::string request = "PDU_REQ|" + std::string(getName()) + "|" + par("sliceName").stringValue()
                    + "|" + std::to_string(par("qfi").intValue());
            EV_INFO << getName() << ": registered; requesting PDU session\n";
            send(new cMessage(request.c_str()), "ctrlOut");
        }
        else if (!f.empty() && f[0] == "PDU_ACCEPT") {
            pduSetupDelay = simTime() - pduRequestStart;
            hasPduSetupDelay = true;
            pduSetupDelayVector.record(pduSetupDelay.dbl() * 1000.0);
            EV_INFO << getName() << ": session active; starting user traffic\n";
            sendTimer = new cMessage("sendTimer");
            scheduleAt(simTime() + par("trafficStartOffset"), sendTimer);
        }
        delete msg;
    }

    void finish() override
    {
        recordScalar("packetsSent", packetsSent);
        if (hasRegistrationDelay) recordScalar("registrationDelay_ms", registrationDelay.dbl() * 1000.0);
        if (hasPduSetupDelay) recordScalar("pduSessionSetupDelay_ms", pduSetupDelay.dbl() * 1000.0);
        if (registrationTimer != nullptr) cancelAndDelete(registrationTimer);
        if (sendTimer != nullptr) cancelAndDelete(sendTimer);
    }
  public:
    long getPacketsSent() const { return packetsSent; }
};
Define_Module(UE);

class Gnb : public cSimpleModule
{
  private:
    cMessage *serviceEvent = nullptr;
    cPacket *inService = nullptr;
    std::deque<cPacket *> urgentQueue;
    std::deque<cPacket *> embbQueue;
    long packetsReceivedFromUe = 0;
    long packetsServed = 0;
    long packetsDroppedUrl = 0;
    long packetsDroppedEmbb = 0;
    long maxQueueLength = 0;
    long urlWaitSamples = 0;
    long embbWaitSamples = 0;
    simtime_t totalUrlQueueWait = SIMTIME_ZERO;
    simtime_t totalEmbbQueueWait = SIMTIME_ZERO;
    double serviceRateBps = 0;
    int queueLimit = 0;
    int priorityQfi = 0;
    cOutVector queueLengthVector;
    cOutVector urlQueueLengthVector;
    cOutVector embbQueueLengthVector;
    cOutVector urlQueueWaitVector;
    cOutVector embbQueueWaitVector;
    cOutVector servedVector;
    cOutVector droppedVector;

    int waitingCount() const { return static_cast<int>(urgentQueue.size() + embbQueue.size()); }

    bool isUrgent(const cPacket *packet) const
    {
        std::vector<std::string> f = splitFields(packet->getName());
        return f.size() > 3 && std::stoi(f[3]) == priorityQfi;
    }

    void recordQueueState()
    {
        int urgent = static_cast<int>(urgentQueue.size());
        int embb = static_cast<int>(embbQueue.size());
        queueLengthVector.record(urgent + embb + (inService != nullptr ? 1 : 0));
        urlQueueLengthVector.record(urgent);
        embbQueueLengthVector.record(embb);
        maxQueueLength = std::max(maxQueueLength, static_cast<long>(urgent + embb));
    }

    void beginNextService()
    {
        if (inService != nullptr)
            return;
        if (!urgentQueue.empty()) {
            inService = urgentQueue.front();
            urgentQueue.pop_front();
            simtime_t wait = simTime() - inService->getTimestamp();
            totalUrlQueueWait += wait;
            urlWaitSamples++;
            urlQueueWaitVector.record(wait.dbl() * 1000.0);
        }
        else if (!embbQueue.empty()) {
            inService = embbQueue.front();
            embbQueue.pop_front();
            simtime_t wait = simTime() - inService->getTimestamp();
            totalEmbbQueueWait += wait;
            embbWaitSamples++;
            embbQueueWaitVector.record(wait.dbl() * 1000.0);
        }
        if (inService != nullptr) {
            double seconds = inService->getBitLength() / serviceRateBps;
            // Quantize transmission time to the simulation's microsecond service clock.
            simtime_t serviceTime = SimTime(static_cast<int64_t>(std::llround(seconds * 1e6)), SIMTIME_US);
            scheduleAt(simTime() + serviceTime, serviceEvent);
        }
        recordQueueState();
    }

    void enqueue(cPacket *packet)
    {
        packetsReceivedFromUe++;
        packet->setTimestamp(simTime());
        bool urgent = isUrgent(packet);
        if (waitingCount() >= queueLimit) {
            // Reserve buffer room for the configured high-priority QoS flow.
            if (urgent && !embbQueue.empty()) {
                delete embbQueue.back();
                embbQueue.pop_back();
                packetsDroppedEmbb++;
                droppedVector.record(packetsDroppedUrl + packetsDroppedEmbb);
            }
            else {
                if (urgent) packetsDroppedUrl++;
                else packetsDroppedEmbb++;
                droppedVector.record(packetsDroppedUrl + packetsDroppedEmbb);
                EV_WARN << "gNB: queue full; dropping " << (urgent ? "high-priority" : "eMBB") << " packet\n";
                recordQueueState();
                delete packet;
                return;
            }
        }
        if (urgent) urgentQueue.push_back(packet);
        else embbQueue.push_back(packet);
        recordQueueState();
        beginNextService();
    }

  protected:
    void initialize() override
    {
        serviceRateBps = par("ranDataRate").doubleValueInUnit("bps");
        queueLimit = par("maxQueuePackets").intValue();
        priorityQfi = par("highPriorityQfi").intValue();
        queueLengthVector.setName("ranQueueLength_packets");
        urlQueueLengthVector.setName("urlQueueLength_packets");
        embbQueueLengthVector.setName("embbQueueLength_packets");
        urlQueueWaitVector.setName("urlQueueWait_ms");
        embbQueueWaitVector.setName("embbQueueWait_ms");
        servedVector.setName("ranPacketsServed");
        droppedVector.setName("ranPacketsDropped");
        serviceEvent = new cMessage("ranServiceComplete");
    }

    void handleMessage(cMessage *msg) override
    {
        if (msg == serviceEvent) {
            packetsServed++;
            servedVector.record(packetsServed);
            EV_INFO << "gNB: radio service complete for " << inService->getName() << "\n";
            send(inService, "dataOut");
            inService = nullptr;
            beginNextService();
            return;
        }

        cGate *arrival = msg->getArrivalGate();
        if (arrival->isName("ueCtrlIn")) {
            send(msg, "coreCtrlOut");
        }
        else if (arrival == gate("coreCtrlIn")) {
            std::vector<std::string> f = splitFields(msg->getName());
            int index = f.size() > 1 ? getUeIndex(f[1]) : -1;
            if (index < 0) {
                EV_WARN << "gNB: control message has unknown UE identity\n";
                delete msg;
            }
            else send(msg, "ueCtrlOut", index);
        }
        else enqueue(check_and_cast<cPacket *>(msg));
    }

    void finish() override
    {
        recordScalar("packetsReceivedFromUE", packetsReceivedFromUe);
        recordScalar("packetsServedToUPF", packetsServed);
        recordScalar("urlPacketsDropped", packetsDroppedUrl);
        recordScalar("embbPacketsDropped", packetsDroppedEmbb);
        recordScalar("maxWaitingQueueLength_packets", maxQueueLength);
        recordScalar("urlMeanQueueWait_ms", urlWaitSamples ? 1000.0 * totalUrlQueueWait.dbl() / urlWaitSamples : 0.0);
        recordScalar("embbMeanQueueWait_ms", embbWaitSamples ? 1000.0 * totalEmbbQueueWait.dbl() / embbWaitSamples : 0.0);
        if (serviceEvent != nullptr) cancelAndDelete(serviceEvent);
        delete inService;
        for (cPacket *packet : urgentQueue) delete packet;
        for (cPacket *packet : embbQueue) delete packet;
    }
};
Define_Module(Gnb);

class AMF : public cSimpleModule
{
  private:
    long registrationRequests = 0;
    long pduSessionRequests = 0;

  protected:
    void handleMessage(cMessage *msg) override
    {
        std::vector<std::string> f = splitFields(msg->getName());
        if (msg->getArrivalGate() == gate("gnbIn")) {
            if (!f.empty() && f[0] == "REG_REQ") {
                registrationRequests++;
                std::string reply = "REG_ACCEPT|" + f.at(1);
                send(new cMessage(reply.c_str()), "gnbOut");
            }
            else if (!f.empty() && f[0] == "PDU_REQ") {
                pduSessionRequests++;
                send(msg, "smfOut");
                return;
            }
        }
        else if (!f.empty() && f[0] == "PDU_ACCEPT") {
            send(msg, "gnbOut");
            return;
        }
        delete msg;
    }

    void finish() override
    {
        recordScalar("registrationRequests", registrationRequests);
        recordScalar("pduSessionRequests", pduSessionRequests);
    }
};
Define_Module(AMF);

class SMF : public cSimpleModule
{
  private:
    long sessionsConfigured = 0;

  protected:
    void handleMessage(cMessage *msg) override
    {
        std::vector<std::string> f = splitFields(msg->getName());
        if (msg->getArrivalGate() == gate("amfIn") && !f.empty() && f[0] == "PDU_REQ") {
            std::string install = "N4_INSTALL|" + f.at(1) + "|" + f.at(2) + "|" + f.at(3);
            EV_INFO << "SMF: configuring PDU session for " << f.at(1) << " with QFI " << f.at(3) << "\n";
            send(new cMessage(install.c_str()), "upfOut");
        }
        else if (!f.empty() && f[0] == "N4_ACK") {
            sessionsConfigured++;
            std::string accept = "PDU_ACCEPT|" + f.at(1);
            send(new cMessage(accept.c_str()), "amfOut");
        }
        delete msg;
    }

    void finish() override { recordScalar("sessionsConfigured", sessionsConfigured); }
};
Define_Module(SMF);

class UPF : public cSimpleModule
{
  private:
    struct Session { std::string slice; int qfi; };
    std::map<std::string, Session> sessions;
    long packetsForwarded = 0;
    long packetsDropped = 0;
    cOutVector forwardedVector;
    cOutVector droppedVector;

  protected:
    void initialize() override
    {
        forwardedVector.setName("upfPacketsForwarded");
        droppedVector.setName("upfPacketsDropped");
    }

    void handleMessage(cMessage *msg) override
    {
        std::vector<std::string> f = splitFields(msg->getName());
        if (msg->getArrivalGate() == gate("ctrlIn")) {
            if (!f.empty() && f[0] == "N4_INSTALL") {
                sessions[f.at(1)] = Session{f.at(2), std::stoi(f.at(3))};
                std::string ack = "N4_ACK|" + f.at(1);
                send(new cMessage(ack.c_str()), "ctrlOut");
            }
            delete msg;
            return;
        }
        if (f.size() < 5 || f[0] != "DATA" || sessions.find(f[1]) == sessions.end()
                || sessions.at(f[1]).slice != f[2] || sessions.at(f[1]).qfi != std::stoi(f[3])) {
            packetsDropped++;
            droppedVector.record(packetsDropped);
            delete msg;
            return;
        }
        packetsForwarded++;
        forwardedVector.record(packetsForwarded);
        sendDelayed(msg, par("processingDelay"), "dataOut");
    }

    void finish() override
    {
        recordScalar("sessionsInstalled", sessions.size());
        recordScalar("packetsForwarded", packetsForwarded);
        recordScalar("packetsDropped", packetsDropped);
    }
};
Define_Module(UPF);

class DataNetwork : public cSimpleModule
{
  private:
    struct FlowMetrics {
        long packets = 0;
        long bytes = 0;
        simtime_t delaySum = SIMTIME_ZERO;
        simtime_t firstCreation = SIMTIME_ZERO;
        simtime_t lastArrival = SIMTIME_ZERO;
        bool initialized = false;
    };
    std::map<std::string, FlowMetrics> flows;
    long packetsReceived = 0;
    long bytesReceived = 0;
    simtime_t delaySum = SIMTIME_ZERO;
    simtime_t firstCreation = SIMTIME_ZERO;
    simtime_t lastArrival = SIMTIME_ZERO;
    bool initialized = false;
    cOutVector delayVector;
    cOutVector urlDelayVector;
    cOutVector embbDelayVector;
    cOutVector receivedBytesVector;

  protected:
    void initialize() override
    {
        delayVector.setName("endToEndDelay_ms");
        urlDelayVector.setName("urlEndToEndDelay_ms");
        embbDelayVector.setName("embbEndToEndDelay_ms");
        receivedBytesVector.setName("cumulativeReceivedBytes");
    }

    void handleMessage(cMessage *msg) override
    {
        std::vector<std::string> f = splitFields(msg->getName());
        long byteLength = check_and_cast<cPacket *>(msg)->getByteLength();
        simtime_t delay = simTime() - msg->getCreationTime();
        packetsReceived++;
        bytesReceived += byteLength;
        delaySum += delay;
        delayVector.record(delay.dbl() * 1000.0);
        receivedBytesVector.record(bytesReceived);
        if (!initialized) {
            firstCreation = msg->getCreationTime();
            initialized = true;
        }
        lastArrival = simTime();
        std::string slice = f.size() > 2 ? f[2] : "unknown";
        FlowMetrics& m = flows[slice];
        m.packets++;
        m.bytes += byteLength;
        m.delaySum += delay;
        if (!m.initialized) {
            m.firstCreation = msg->getCreationTime();
            m.initialized = true;
        }
        m.lastArrival = simTime();
        if (slice == "URLLC") urlDelayVector.record(delay.dbl() * 1000.0);
        if (slice == "eMBB") embbDelayVector.record(delay.dbl() * 1000.0);
        delete msg;
    }

    void finish() override
    {
        recordScalar("packetsReceived", packetsReceived);
        recordScalar("bytesReceived", bytesReceived);
        long packetsSent = 0;
        for (const char *name : {"ueUrllc1", "ueUrllc2", "ueEmbb1", "ueEmbb2"}) {
            auto *ue = dynamic_cast<UE *>(getParentModule()->getSubmodule(name));
            if (ue != nullptr) packetsSent += ue->getPacketsSent();
        }
        recordScalar("packetsSentByUEs", packetsSent);
        recordScalar("packetDeliveryRatio_percent", packetsSent > 0 ? 100.0 * packetsReceived / packetsSent : 0.0);
        if (packetsReceived > 0) {
            recordScalar("meanEndToEndDelay_ms", 1000.0 * delaySum.dbl() / packetsReceived);
            double span = (lastArrival - firstCreation).dbl();
            recordScalar("aggregateThroughput_bps", span > 0 ? bytesReceived * 8.0 / span : 0.0);
        }
        for (const auto& entry : flows) {
            const std::string prefix = entry.first + "_";
            const FlowMetrics& m = entry.second;
            recordScalar((prefix + "packetsReceived").c_str(), m.packets);
            recordScalar((prefix + "bytesReceived").c_str(), m.bytes);
            recordScalar((prefix + "meanEndToEndDelay_ms").c_str(), 1000.0 * m.delaySum.dbl() / m.packets);
            double span = (m.lastArrival - m.firstCreation).dbl();
            recordScalar((prefix + "throughput_bps").c_str(), span > 0 ? m.bytes * 8.0 / span : 0.0);
        }
    }
};
Define_Module(DataNetwork);
