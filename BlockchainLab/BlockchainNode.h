#ifndef __BLOCKCHAINLAB_BLOCKCHAINNODE_H
#define __BLOCKCHAINLAB_BLOCKCHAINNODE_H

#include <omnetpp.h>
#include <string>
#include <unordered_map>
#include <vector>

class ChainPacket : public omnetpp::cPacket
{
  public:
    enum Kind { TX = 1, BLOCK = 2, STATUS = 3 };

    int sourceNode = -1;
    int blockHeight = 0;
    int difficulty = 0;
    int nonce = 0;
    int maxWork = 0;
    std::string hash;
    std::string previousHash;
    std::string payload;
    std::string phase;
    std::vector<std::string> transactions;

    explicit ChainPacket(const char *name = "", int kind = 0) : cPacket(name) { setKind(kind); }
    ChainPacket(const ChainPacket& other) : cPacket(other), sourceNode(other.sourceNode),
        blockHeight(other.blockHeight), difficulty(other.difficulty), nonce(other.nonce),
        maxWork(other.maxWork), hash(other.hash), previousHash(other.previousHash),
        payload(other.payload), phase(other.phase), transactions(other.transactions) {}
    ChainPacket *dup() const override { return new ChainPacket(*this); }
};

class BlockchainNode : public omnetpp::cSimpleModule
{
  private:
    struct Block {
        int height = 0;
        int creator = -1;
        int nonce = 0;
        int difficulty = 1;
        int cumulativeWork = 0;
        std::string previousHash;
        std::string hash;
        std::vector<std::string> transactions;
    };

    int nodeId = 0;
    int numNodes = 0;
    int difficulty = 1;
    int maxTxPerBlock = 3;
    bool miner = false;
    bool tamperFirstBlock = false;
    bool tamperAttempted = false;
    omnetpp::simtime_t blockInterval;
    omnetpp::simtime_t txStartTime;
    omnetpp::simtime_t txInterval;
    std::vector<std::string> scriptedTransactions;
    size_t nextScriptedTransaction = 0;
    std::vector<std::string> mempool;
    std::unordered_map<std::string, Block> knownBlocks;
    std::vector<std::string> canonicalChain;
    std::string phase = "Starting";
    omnetpp::cMessage *mineTimer = nullptr;
    omnetpp::cMessage *txTimer = nullptr;

    static std::string sha256(const std::string& input);
    static std::vector<std::string> split(const std::string& input, char delimiter);
    std::string calculateHash(const Block& block) const;
    bool hasProofOfWork(const Block& block) const;
    std::string tipHash() const;
    int tipHeight() const;
    void createGenesis();
    void updateCanonicalChain();
    void broadcast(ChainPacket *packet);
    void publishStatus();
    void refreshDisplay();
    void generateTransaction();
    void mineBlock();
    bool acceptBlock(const ChainPacket *packet);
    bool alreadyHasTransaction(const std::string& tx) const;

  protected:
    void initialize() override;
    void handleMessage(omnetpp::cMessage *msg) override;
    void finish() override;
};

class ConsensusMonitor : public omnetpp::cSimpleModule
{
  private:
    struct NodeState {
        bool seen = false;
        int height = 0;
        std::string tip;
        std::string phase;
    };
    std::vector<NodeState> states;
    void refreshDisplay();

  protected:
    void initialize() override;
    void handleMessage(omnetpp::cMessage *msg) override;
};

#endif
