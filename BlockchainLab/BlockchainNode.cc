#include "BlockchainNode.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>

using namespace omnetpp;

Define_Module(BlockchainNode);
Define_Module(ConsensusMonitor);

std::string BlockchainNode::sha256(const std::string& input)
{
    static const std::uint32_t constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    auto rotateRight = [](std::uint32_t value, unsigned int count) {
        return (value >> count) | (value << (32 - count));
    };

    std::vector<unsigned char> bytes(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56)
        bytes.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<unsigned char>((bitLength >> shift) & 0xff));

    std::uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    for (size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::uint32_t words[64] = {};
        for (int i = 0; i < 16; ++i) {
            const size_t at = offset + static_cast<size_t>(i) * 4;
            words[i] = (static_cast<std::uint32_t>(bytes[at]) << 24) |
                       (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
                       (static_cast<std::uint32_t>(bytes[at + 2]) << 8) |
                       static_cast<std::uint32_t>(bytes[at + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
            const std::uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    std::ostringstream out;
    for (std::uint32_t word : state)
        out << std::hex << std::setw(8) << std::setfill('0') << word;
    return out.str();
}

std::vector<std::string> BlockchainNode::split(const std::string& input, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream stream(input);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        auto first = part.find_first_not_of(" \t\r\n");
        auto last = part.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            parts.push_back(part.substr(first, last - first + 1));
    }
    return parts;
}

std::string BlockchainNode::calculateHash(const Block& block) const
{
    std::ostringstream input;
    input << block.height << '|' << block.previousHash << '|' << block.creator << '|'
          << block.difficulty << '|' << block.nonce;
    for (const auto& tx : block.transactions)
        input << '|' << tx;
    return sha256(input.str());
}

bool BlockchainNode::hasProofOfWork(const Block& block) const
{
    if (block.difficulty < 0 || block.difficulty > 6)
        return false;
    return block.hash == calculateHash(block) &&
           block.hash.compare(0, block.difficulty, std::string(block.difficulty, '0')) == 0;
}

std::string BlockchainNode::tipHash() const
{
    return canonicalChain.empty() ? std::string() : canonicalChain.back();
}

int BlockchainNode::tipHeight() const
{
    return canonicalChain.empty() ? 0 : knownBlocks.at(canonicalChain.back()).height;
}

void BlockchainNode::createGenesis()
{
    Block genesis;
    genesis.height = 0;
    genesis.creator = -1;
    genesis.difficulty = 0;
    genesis.cumulativeWork = 0;
    genesis.previousHash = "0000000000000000";
    genesis.transactions = {"Genesis: Alice=10, Bob=10, Cara=10"};
    genesis.hash = calculateHash(genesis);
    knownBlocks[genesis.hash] = genesis;
    canonicalChain.push_back(genesis.hash);
}

void BlockchainNode::initialize()
{
    nodeId = par("nodeId").intValue();
    numNodes = par("numNodes").intValue();
    difficulty = par("difficulty").intValue();
    maxTxPerBlock = par("maxTxPerBlock").intValue();
    miner = par("miner").boolValue();
    tamperFirstBlock = par("tamperFirstBlock").boolValue();
    blockInterval = par("blockInterval");
    txStartTime = par("txStartTime");
    txInterval = par("txInterval");
    scriptedTransactions = split(par("txScript").stdstringValue(), ';');
    mineTimer = new cMessage("mineTimer");
    txTimer = new cMessage("txTimer");

    createGenesis();
    phase = miner ? "Miner ready" : "Peer ready";
    refreshDisplay();
    publishStatus();

    if (miner)
        scheduleAt(simTime() + exponential(blockInterval), mineTimer);
    if (nodeId == 0 && !scriptedTransactions.empty() && txStartTime >= SIMTIME_ZERO)
        scheduleAt(simTime() + txStartTime, txTimer);
}

void BlockchainNode::handleMessage(cMessage *msg)
{
    if (msg == mineTimer) {
        mineBlock();
        scheduleAt(simTime() + exponential(blockInterval), mineTimer);
        return;
    }
    if (msg == txTimer) {
        generateTransaction();
        if (nextScriptedTransaction < scriptedTransactions.size())
            scheduleAt(simTime() + txInterval, txTimer);
        return;
    }

    auto *packet = check_and_cast<ChainPacket *>(msg);
    if (packet->getKind() == ChainPacket::TX) {
        if (!packet->payload.empty() && !alreadyHasTransaction(packet->payload)) {
            mempool.push_back(packet->payload);
            phase = "Transaction received";
            broadcast(packet->dup());
            refreshDisplay();
            publishStatus();
        }
        delete packet;
        return;
    }
    if (packet->getKind() == ChainPacket::BLOCK) {
        if (tamperFirstBlock && !tamperAttempted && packet->blockHeight > 0 && !packet->transactions.empty()) {
            tamperAttempted = true;
            packet->transactions[0] += " [altered in transit]";
            phase = "Tamper attempt: altered data, kept old hash";
            refreshDisplay();
        }
        const bool accepted = acceptBlock(packet);
        if (accepted) {
            phase = "Block received; checking work";
            // Relay valid blocks so every peer can learn about the same branch.
            auto *relay = packet->dup();
            broadcast(relay);
        }
        else if (tamperAttempted && phase == "Tamper attempt: altered data, kept old hash") {
            phase = "Rejected tamper: hash does not match data";
        }
        delete packet;
        refreshDisplay();
        publishStatus();
        return;
    }
    delete packet;
}

void BlockchainNode::generateTransaction()
{
    if (nextScriptedTransaction >= scriptedTransactions.size())
        return;
    const std::string tx = scriptedTransactions[nextScriptedTransaction++];
    auto *packet = new ChainPacket("transaction", ChainPacket::TX);
    packet->sourceNode = nodeId;
    packet->payload = tx;
    if (!alreadyHasTransaction(tx))
        mempool.push_back(tx);
    phase = "Broadcasting transaction";
    broadcast(packet);
    refreshDisplay();
    publishStatus();
}

void BlockchainNode::mineBlock()
{
    Block block;
    block.height = tipHeight() + 1;
    block.creator = nodeId;
    block.difficulty = difficulty;
    block.cumulativeWork = knownBlocks.at(tipHash()).cumulativeWork + 1;
    block.previousHash = tipHash();
    const int txCount = std::min(maxTxPerBlock, static_cast<int>(mempool.size()));
    for (int i = 0; i < txCount; ++i)
        block.transactions.push_back(mempool[i]);

    phase = "Mining proof-of-work puzzle";
    refreshDisplay();
    while (block.nonce < 2000000) {
        block.hash = calculateHash(block);
        if (block.hash.compare(0, difficulty, std::string(difficulty, '0')) == 0)
            break;
        ++block.nonce;
    }
    if (block.nonce >= 2000000) {
        phase = "Mining limit reached; lower difficulty";
        refreshDisplay();
        publishStatus();
        return;
    }

    knownBlocks[block.hash] = block;
    updateCanonicalChain();
    for (const auto& tx : block.transactions)
        mempool.erase(std::remove(mempool.begin(), mempool.end(), tx), mempool.end());

    auto *packet = new ChainPacket("newBlock", ChainPacket::BLOCK);
    packet->sourceNode = nodeId;
    packet->blockHeight = block.height;
    packet->difficulty = block.difficulty;
    packet->nonce = block.nonce;
    packet->maxWork = block.cumulativeWork;
    packet->hash = block.hash;
    packet->previousHash = block.previousHash;
    packet->transactions = block.transactions;
    phase = "Block mined; broadcasting to peers";
    broadcast(packet);
    refreshDisplay();
    publishStatus();
}

bool BlockchainNode::acceptBlock(const ChainPacket *packet)
{
    if (knownBlocks.find(packet->hash) != knownBlocks.end())
        return false;
    auto parentIt = knownBlocks.find(packet->previousHash);
    if (parentIt == knownBlocks.end() || packet->blockHeight != parentIt->second.height + 1)
        return false;

    Block block;
    block.height = packet->blockHeight;
    block.creator = packet->sourceNode;
    block.nonce = packet->nonce;
    block.difficulty = packet->difficulty;
    block.cumulativeWork = parentIt->second.cumulativeWork + 1;
    block.previousHash = packet->previousHash;
    block.transactions = packet->transactions;
    block.hash = packet->hash;
    if (block.difficulty != difficulty || !hasProofOfWork(block))
        return false;

    knownBlocks[block.hash] = block;
    const std::string previousTip = tipHash();
    updateCanonicalChain();
    if (tipHash() != previousTip) {
        for (const auto& tx : block.transactions)
            mempool.erase(std::remove(mempool.begin(), mempool.end(), tx), mempool.end());
        phase = "Accepted more-work chain";
    }
    else {
        phase = "Valid competing block; waiting for more work";
    }
    return true;
}

void BlockchainNode::updateCanonicalChain()
{
    std::string best = tipHash();
    int bestWork = best.empty() ? -1 : knownBlocks.at(best).cumulativeWork;
    for (const auto& item : knownBlocks) {
        const auto& block = item.second;
        if (block.cumulativeWork > bestWork) {
            best = item.first;
            bestWork = block.cumulativeWork;
        }
    }

    std::vector<std::string> path;
    std::string cursor = best;
    while (!cursor.empty()) {
        path.push_back(cursor);
        auto it = knownBlocks.find(cursor);
        if (it == knownBlocks.end() || it->second.height == 0)
            break;
        cursor = it->second.previousHash;
    }
    std::reverse(path.begin(), path.end());
    canonicalChain = std::move(path);
}

bool BlockchainNode::alreadyHasTransaction(const std::string& tx) const
{
    if (std::find(mempool.begin(), mempool.end(), tx) != mempool.end())
        return true;
    for (const auto& hash : canonicalChain) {
        const auto& block = knownBlocks.at(hash);
        if (std::find(block.transactions.begin(), block.transactions.end(), tx) != block.transactions.end())
            return true;
    }
    return false;
}

void BlockchainNode::broadcast(ChainPacket *packet)
{
    for (int gate = 0; gate < gateSize("out"); ++gate)
        send(packet->dup(), "out", gate);
    delete packet;
}

void BlockchainNode::publishStatus()
{
    auto *status = new ChainPacket("nodeStatus", ChainPacket::STATUS);
    status->sourceNode = nodeId;
    status->blockHeight = tipHeight();
    status->hash = tipHash();
    status->phase = phase;
    std::ostringstream ledger;
    ledger << "P" << nodeId << " ledger | height " << tipHeight() << "\n";
    for (size_t i = 0; i < canonicalChain.size(); ++i) {
        const auto& block = knownBlocks.at(canonicalChain[i]);
        ledger << "#" << block.height << " " << block.hash.substr(0, 8);
        if (block.height == 0)
            ledger << " Genesis";
        else
            ledger << " by P" << block.creator;
        if (block.height > 0) {
            int confirmations = tipHeight() - block.height;
            ledger << " [" << confirmations << " conf]";
        }
        for (const auto& tx : block.transactions)
            ledger << " | " << tx;
        ledger << "\n";
    }
    if (mempool.empty())
        ledger << "Pending: none";
    else {
        ledger << "Pending: ";
        for (size_t i = 0; i < mempool.size(); ++i) {
            if (i) ledger << "; ";
            ledger << mempool[i];
        }
    }
    status->payload = ledger.str();
    send(status, "statusOut");
}

void BlockchainNode::refreshDisplay()
{
    const int columns = 3;
    const double x = 190 + (nodeId % columns) * 350;
    const double y = 270 + (nodeId / columns) * 310;
    getDisplayString().setTagArg("p", 0, x);
    getDisplayString().setTagArg("p", 1, y);
    std::ostringstream ledger;
    ledger << "P" << nodeId << (miner ? " (miner)" : "") << " | " << phase << "\n"
           << "LOCAL LEDGER | height " << tipHeight() << " | tip " << tipHash().substr(0, 10) << "\n";
    for (const auto& hash : canonicalChain) {
        const auto& block = knownBlocks.at(hash);
        ledger << "#" << block.height << " " << block.hash.substr(0, 8);
        if (block.height == 0)
            ledger << " Genesis";
        else
            ledger << " n=" << block.nonce << " P" << block.creator << " (" << tipHeight() - block.height << " conf)";
        for (const auto& tx : block.transactions)
            ledger << " | " << tx;
        ledger << "\n";
    }
    ledger << "MEMPOOL: ";
    if (mempool.empty())
        ledger << "empty";
    else {
        for (size_t i = 0; i < mempool.size(); ++i) {
            if (i) ledger << "; ";
            ledger << mempool[i];
        }
    }
    getDisplayString().setTagArg("t", 0, ledger.str().c_str());
    getDisplayString().setTagArg("t", 1, "#f1f5f9");
    getDisplayString().setTagArg("t", 2, "Arial,8");
    getDisplayString().setTagArg("t", 3, "c");
    getDisplayString().setTagArg("t", 5, "#152235");
}

void BlockchainNode::finish()
{
    cancelAndDelete(mineTimer);
    cancelAndDelete(txTimer);
}

void ConsensusMonitor::initialize()
{
    states.resize(par("numNodes").intValue());
    getDisplayString().setTagArg("t", 0, "CONSENSUS MONITOR\nWaiting for peer state...");
    getDisplayString().setTagArg("t", 1, "#f8fafc");
    getDisplayString().setTagArg("t", 2, "Arial,12,bold");
    getDisplayString().setTagArg("t", 3, "c");
    getDisplayString().setTagArg("t", 5, "#1d3557");
}

void ConsensusMonitor::handleMessage(cMessage *msg)
{
    auto *status = check_and_cast<ChainPacket *>(msg);
    if (status->getKind() == ChainPacket::STATUS && status->sourceNode >= 0 &&
        status->sourceNode < static_cast<int>(states.size())) {
        auto& state = states[status->sourceNode];
        state.seen = true;
        state.height = status->blockHeight;
        state.tip = status->hash;
        state.phase = status->phase;
        refreshDisplay();
    }
    delete status;
}

void ConsensusMonitor::refreshDisplay()
{
    int largestGroup = 0;
    int highestHeight = 0;
    std::unordered_map<std::string, int> tipCounts;
    for (const auto& state : states) {
        if (state.seen) {
            tipCounts[state.tip]++;
            highestHeight = std::max(highestHeight, state.height);
        }
    }
    for (const auto& group : tipCounts)
        largestGroup = std::max(largestGroup, group.second);

    std::ostringstream text;
    text << "CONSENSUS MONITOR | Nakamoto-style longest-work rule\n"
         << "Each block has equal work in this teaching model; highest cumulative work wins.\n"
         << "TIP AGREEMENT: [";
    for (int i = 0; i < static_cast<int>(states.size()); ++i)
        text << (i < largestGroup ? '#' : '-');
    text << "] " << largestGroup << "/" << states.size() << " peers share a tip"
         << " | Highest known height: " << highestHeight << "\n";
    if (largestGroup == static_cast<int>(states.size()))
        text << "STATE: CONVERGED — all peers show the same ledger tip\n";
    else if (largestGroup > 1)
        text << "STATE: PROPAGATING / FORK — peers still have different ledger tips\n";
    else
        text << "STATE: PROPAGATING — no shared tip yet\n";
    for (size_t i = 0; i < states.size(); ++i) {
        const auto& state = states[i];
        text << "P" << i << ": ";
        if (!state.seen)
            text << "waiting";
        else {
            text << "height " << state.height << " | " << state.tip.substr(0, 10)
                 << (tipCounts[state.tip] == largestGroup && (largestGroup > 1 || states.size() == 1)
                     ? " | ON SHARED TIP" : " | DIVERGED")
                 << " | " << state.phase;
        }
        text << "\n";
    }
    getDisplayString().setTagArg("t", 0, text.str().c_str());
}
