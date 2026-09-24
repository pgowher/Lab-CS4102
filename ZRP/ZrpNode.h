#ifndef ZRPNODE_H
#define ZRPNODE_H

#include <omnetpp.h>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

enum ZrpKind { ZONE_UPDATE = 1, BORDERCAST = 2, ROUTE_REPLY = 3, PING_DATA = 4, ROUTE_ERROR = 5 };

class ZrpPacket : public omnetpp::cPacket
{
  public:
    int origin = -1;
    int destination = -1;
    int sequence = 0;
    int linkMask = 0;
    int ttl = 0;
    int cursor = 0;
    omnetpp::simtime_t born;
    std::vector<int> route;
    std::vector<int> segment;
    ZrpPacket(const char *name, int kind) : cPacket(name, kind) {}
    ZrpPacket(const ZrpPacket& other) = default;
    ZrpPacket *dup() const override { return new ZrpPacket(*this); }
};

class ZrpNode : public omnetpp::cSimpleModule
{
  private:
    static constexpr int N = 10;
    int id = -1;
    int zoneRadius = 2;
    double range = 250;
    int updateSequence = 0;
    int querySequence = 0;
    int pingSequence = 0;
    int neighborMask = 0;
    bool discoveryPending = false;
    omnetpp::simtime_t discoveryStarted;
    std::array<int, N> knownLinks{};
    std::array<omnetpp::simtime_t, N> linkTimes{};
    std::array<int, N> latestUpdate{};
    std::set<std::pair<int, int>> seenBordercasts;
    std::vector<int> cachedRoute;
    omnetpp::simtime_t routeLearned;
    omnetpp::cMessage *updateTimer = nullptr;
    omnetpp::cMessage *pingTimer = nullptr;
    omnetpp::simsignal_t delaySignal;
    omnetpp::simsignal_t hopSignal;
    omnetpp::simsignal_t discoverySignal;
    long sent = 0, received = 0, replies = 0, queryCount = 0, routeErrors = 0;
    omnetpp::cStdDev delays, hops, discoveryDelays;

    ZrpNode *node(int index) const;
    void updatePosition();
    int currentNeighbors() const;
    bool inRange(int other) const;
    bool transmit(ZrpPacket *packet, int next);
    void sendUpdate();
    std::vector<int> localPath(int target) const;
    void discover();
    void bordercast(const ZrpPacket& prototype);
    void advance(ZrpPacket *packet);
    void handleUpdate(ZrpPacket *packet);
    void handleQuery(ZrpPacket *packet);
    void handleReply(ZrpPacket *packet);
    void handleData(ZrpPacket *packet);
    void handleError(ZrpPacket *packet);
    void startPing();
    void sendError(const std::vector<int>& path, int failedAt);

  protected:
    void initialize() override;
    void handleMessage(omnetpp::cMessage *message) override;
    void finish() override;
    ~ZrpNode() override;
};

#endif
