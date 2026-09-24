#include "ZrpNode.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <string>

using namespace omnetpp;

Define_Module(ZrpNode);

namespace {
constexpr int UPDATE_TIMER = 100;
constexpr int PING_TIMER = 101;
const char *messageName(int kind)
{
    switch (kind) {
        case ZONE_UPDATE: return "ZONE UPDATE";
        case BORDERCAST: return "BORDERCAST";
        case ROUTE_REPLY: return "ROUTE REPLY";
        case PING_DATA: return "PING";
        default: return "ROUTE ERROR";
    }
}
}

ZrpNode::~ZrpNode()
{
    cancelAndDelete(updateTimer);
    cancelAndDelete(pingTimer);
}

ZrpNode *ZrpNode::node(int index) const
{
    return check_and_cast<ZrpNode *>(getParentModule()->getSubmodule("host", index));
}

void ZrpNode::initialize()
{
    id = par("nodeId").intValue();
    range = par("range").doubleValue();
    zoneRadius = par("zoneRadius").intValue();
    if (zoneRadius < 1 || zoneRadius >= N)
        throw cRuntimeError("zoneRadius must be between 1 and %d", N - 1);
    latestUpdate.fill(-1);
    linkTimes.fill(SimTime(-100));
    routeLearned = SimTime(-100);
    delaySignal = registerSignal("oneWayDelay");
    hopSignal = registerSignal("hopCount");
    discoverySignal = registerSignal("routeDiscoveryDelay");
    updatePosition();
    updateTimer = new cMessage("zone update timer", UPDATE_TIMER);
    scheduleAt(simTime() + SimTime(id * 0.01), updateTimer);
    if (id == 0) {
        pingTimer = new cMessage("ping timer", PING_TIMER);
        scheduleAt(SimTime(4), pingTimer);
    }
}

void ZrpNode::updatePosition()
{
    double x = par("x").doubleValue();
    double y = par("y").doubleValue();
    if (id == 2) {
        double t = simTime().dbl();
        if (t >= 18 && t < 24)
            y = 325 + (t - 18) * 175 / 6;
        else if (t >= 24 && t < 36)
            y = 500;
        else if (t >= 36 && t < 42)
            y = 500 - (t - 36) * 175 / 6;
    }
    getDisplayString().setTagArg("p", 0, std::to_string(x).c_str());
    getDisplayString().setTagArg("p", 1, std::to_string(y).c_str());
    if (id == 0) {
        getDisplayString().setTagArg("i", 1, "red");
        getDisplayString().setTagArg("t", 0, "SOURCE");
    }
    else if (id == 9) {
        getDisplayString().setTagArg("i", 1, "green");
        getDisplayString().setTagArg("t", 0, "DESTINATION");
    }
    else if (id == 2) {
        getDisplayString().setTagArg("i", 1, "yellow");
        getDisplayString().setTagArg("t", 0, "MOVING RELAY");
    }
}

bool ZrpNode::inRange(int other) const
{
    const ZrpNode *peer = node(other);
    double x1 = par("x").doubleValue();
    double y1 = par("y").doubleValue();
    double x2 = peer->par("x").doubleValue();
    double y2 = peer->par("y").doubleValue();
    // The relay's true position follows the displayed movement script.
    auto movingY = [] (double t) {
        if (t < 18) return 325.0;
        if (t < 24) return 325.0 + (t - 18) * 175 / 6;
        if (t < 36) return 500.0;
        if (t < 42) return 500.0 - (t - 36) * 175 / 6;
        return 325.0;
    };
    if (id == 2) y1 = movingY(simTime().dbl());
    if (other == 2) y2 = movingY(simTime().dbl());
    return std::hypot(x1 - x2, y1 - y2) <= range;
}

int ZrpNode::currentNeighbors() const
{
    int mask = 0;
    for (int i = 0; i < N; ++i)
        if (i != id && inRange(i))
            mask |= 1 << i;
    return mask;
}

bool ZrpNode::transmit(ZrpPacket *packet, int next)
{
    if (!inRange(next)) {
        EV_WARN << messageName(packet->getKind()) << " failed from " << id << " to " << next << "\n";
        delete packet;
        return false;
    }
    EV_INFO << messageName(packet->getKind()) << " hop " << id << " -> " << next << "\n";
    sendDirect(packet, SimTime(0.002), SIMTIME_ZERO, node(next), "radioIn");
    return true;
}

void ZrpNode::sendUpdate()
{
    neighborMask = currentNeighbors();
    knownLinks[id] = neighborMask;
    linkTimes[id] = simTime();
    latestUpdate[id] = ++updateSequence;
    for (int i = 0; i < N; ++i) {
        if (!(neighborMask & (1 << i))) continue;
        auto *packet = new ZrpPacket("ZONE UPDATE", ZONE_UPDATE);
        packet->origin = id;
        packet->sequence = updateSequence;
        packet->linkMask = neighborMask;
        packet->ttl = zoneRadius;
        transmit(packet, i);
    }
}

std::vector<int> ZrpNode::localPath(int target) const
{
    std::array<int, N> distance, parent;
    distance.fill(-1);
    parent.fill(-1);
    std::queue<int> pending;
    distance[id] = 0;
    pending.push(id);
    while (!pending.empty()) {
        int u = pending.front();
        pending.pop();
        if (distance[u] >= zoneRadius) continue;
        if (simTime() - linkTimes[u] > SimTime(2.5)) continue;
        for (int v = 0; v < N; ++v) {
            if (!(knownLinks[u] & (1 << v)) || distance[v] >= 0) continue;
            distance[v] = distance[u] + 1;
            parent[v] = u;
            pending.push(v);
        }
    }
    if (distance[target] < 0) return {};
    std::vector<int> path;
    for (int p = target; p >= 0; p = parent[p]) path.push_back(p);
    std::reverse(path.begin(), path.end());
    return path;
}

void ZrpNode::advance(ZrpPacket *packet)
{
    if (packet->cursor + 1 >= (int)packet->segment.size()) {
        delete packet;
        return;
    }
    int next = packet->segment[++packet->cursor];
    transmit(packet, next);
}

void ZrpNode::bordercast(const ZrpPacket& prototype)
{
    // If the destination has entered the local zone, send the query to it.
    // It sends the reply, so the control exchange remains observable.
    auto destinationPath = localPath(prototype.destination);
    if (destinationPath.size() > 1) {
        auto *packet = prototype.dup();
        packet->segment = destinationPath;
        packet->cursor = 0;
        advance(packet);
        return;
    }

    int farthest = 0;
    std::array<std::vector<int>, N> paths;
    for (int i = 0; i < N; ++i) {
        if (i == id) continue;
        paths[i] = localPath(i);
        if (paths[i].size() > 1 && std::find(prototype.route.begin(), prototype.route.end(), i) == prototype.route.end())
            farthest = std::max(farthest, (int)paths[i].size() - 1);
    }
    for (int i = 0; i < N; ++i) {
        if ((int)paths[i].size() - 1 != farthest || farthest == 0) continue;
        if (std::find(prototype.route.begin(), prototype.route.end(), i) != prototype.route.end()) continue;
        auto *packet = prototype.dup();
        packet->segment = paths[i];
        packet->cursor = 0;
        ++queryCount;
        advance(packet);
    }
}

void ZrpNode::discover()
{
    if (id != 0 || discoveryPending) return;
    discoveryPending = true;
    discoveryStarted = simTime();
    cachedRoute.clear();
    auto *query = new ZrpPacket("BORDERCAST", BORDERCAST);
    query->origin = id;
    query->destination = 9;
    query->sequence = ++querySequence;
    query->route.push_back(id);
    seenBordercasts.insert({id, querySequence});
    bordercast(*query);
    delete query;
}

void ZrpNode::handleUpdate(ZrpPacket *packet)
{
    if (packet->sequence <= latestUpdate[packet->origin]) {
        delete packet;
        return;
    }
    latestUpdate[packet->origin] = packet->sequence;
    knownLinks[packet->origin] = packet->linkMask;
    linkTimes[packet->origin] = simTime();
    if (packet->ttl > 1) {
        for (int i = 0; i < N; ++i) {
            if (i == packet->origin || !(currentNeighbors() & (1 << i))) continue;
            auto *copy = packet->dup();
            copy->ttl--;
            transmit(copy, i);
        }
    }
    delete packet;
}

void ZrpNode::handleQuery(ZrpPacket *packet)
{
    if (std::find(packet->route.begin(), packet->route.end(), id) != packet->route.end()) {
        delete packet;
        return;
    }
    packet->route.push_back(id);
    if (packet->cursor + 1 < (int)packet->segment.size()) {
        advance(packet);
        return;
    }
    if (id == packet->destination) {
        auto *reply = new ZrpPacket("ROUTE REPLY", ROUTE_REPLY);
        reply->origin = packet->origin;
        reply->destination = packet->destination;
        reply->sequence = packet->sequence;
        reply->route = packet->route;
        reply->segment = packet->route;
        std::reverse(reply->segment.begin(), reply->segment.end());
        reply->cursor = 0;
        advance(reply);
    }
    else if (seenBordercasts.insert({packet->origin, packet->sequence}).second)
        bordercast(*packet);
    delete packet;
}

void ZrpNode::handleReply(ZrpPacket *packet)
{
    if (id == packet->origin) {
        if (packet->sequence == querySequence) {
            if (discoveryPending) {
                double elapsed = (simTime() - discoveryStarted).dbl();
                discoveryDelays.collect(elapsed);
                emit(discoverySignal, elapsed);
                discoveryPending = false;
            }
            if (cachedRoute.empty() || packet->route.size() < cachedRoute.size()) {
                cachedRoute = packet->route;
                routeLearned = simTime();
                EV_INFO << "ZRP route discovered:";
                for (int hop : cachedRoute) EV_INFO << " " << hop;
                EV_INFO << "\n";
            }
        }
        delete packet;
    }
    else
        advance(packet);
}

void ZrpNode::sendError(const std::vector<int>& path, int failedAt)
{
    ++routeErrors;
    if (failedAt == 0) {
        cachedRoute.clear();
        discoveryPending = false;
        discover();
        return;
    }
    auto *error = new ZrpPacket("ROUTE ERROR", ROUTE_ERROR);
    error->segment.assign(path.begin(), path.begin() + failedAt + 1);
    std::reverse(error->segment.begin(), error->segment.end());
    error->cursor = 0;
    advance(error);
}

void ZrpNode::handleData(ZrpPacket *packet)
{
    if (id == packet->destination) {
        ++received;
        double delay = (simTime() - packet->born).dbl();
        int hopCount = (int)packet->route.size() - 1;
        delays.collect(delay);
        hops.collect(hopCount);
        emit(delaySignal, delay);
        emit(hopSignal, hopCount);
        delete packet;
        return;
    }
    int position = packet->cursor;
    if (position + 1 >= (int)packet->route.size()) {
        delete packet;
        return;
    }
    int next = packet->route[position + 1];
    auto path = packet->route;
    packet->cursor++;
    if (!transmit(packet, next)) sendError(path, position);
}

void ZrpNode::handleError(ZrpPacket *packet)
{
    if (id == 0) {
        cachedRoute.clear();
        discoveryPending = false;
        delete packet;
        discover();
    }
    else
        advance(packet);
}

void ZrpNode::startPing()
{
    ++sent;
    if (discoveryPending && simTime() - discoveryStarted > SimTime(2))
        discoveryPending = false;
    if (cachedRoute.empty() || simTime() - routeLearned > SimTime(4)) {
        discover();
        return;
    }
    auto *packet = new ZrpPacket("PING", PING_DATA);
    packet->origin = 0;
    packet->destination = 9;
    packet->sequence = ++pingSequence;
    packet->born = simTime();
    packet->route = cachedRoute;
    packet->cursor = 0;
    int next = cachedRoute[1];
    packet->cursor = 1;
    if (!transmit(packet, next)) sendError(cachedRoute, 0);
}

void ZrpNode::handleMessage(cMessage *message)
{
    if (message == updateTimer) {
        updatePosition();
        sendUpdate();
        scheduleAt(simTime() + SimTime(1), updateTimer);
    }
    else if (message == pingTimer) {
        startPing();
        scheduleAt(simTime() + SimTime(0.6), pingTimer);
    }
    else {
        auto *packet = check_and_cast<ZrpPacket *>(message);
        switch (packet->getKind()) {
            case ZONE_UPDATE: handleUpdate(packet); break;
            case BORDERCAST: handleQuery(packet); break;
            case ROUTE_REPLY: handleReply(packet); break;
            case PING_DATA: handleData(packet); break;
            case ROUTE_ERROR: handleError(packet); break;
            default: delete packet; throw cRuntimeError("Unknown ZRP packet kind");
        }
    }
}

void ZrpNode::finish()
{
    if (id == 0) {
        long delivered = node(9)->received;
        recordScalar("ping packets sent by source", sent);
        recordScalar("ping packets received by destination", delivered);
        recordScalar("ping packets lost before destination", sent - delivered);
        recordScalar("packet delivery ratio at destination (%)", sent ? 100.0 * delivered / sent : 0);
        recordScalar("route discovery delay mean (s)", discoveryDelays.getCount() ? discoveryDelays.getMean() : 0);
        recordScalar("bordercast branches sent", queryCount);
    }
    if (id == 9) {
        recordScalar("average one-way delay at destination (s)", delays.getCount() ? delays.getMean() : 0);
        recordScalar("average hops to destination", hops.getCount() ? hops.getMean() : 0);
    }
    recordScalar("route errors generated", routeErrors);
}
