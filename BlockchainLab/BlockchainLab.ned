simple BlockchainNode
{
    parameters:
        int nodeId;
        int numNodes;
        bool miner = default(false);
        bool tamperFirstBlock = default(false);
        int difficulty = default(1);
        double blockInterval @unit(s) = default(8s);
        double txStartTime @unit(s) = default(-1s);
        double txInterval @unit(s) = default(3s);
        string txScript = default("");
        int maxTxPerBlock = default(3);
        @display("i=device/server");
    gates:
        input in[];
        output out[];
        output statusOut;
}

simple ConsensusMonitor
{
    parameters:
        int numNodes;
        @display("i=block/circle");
    gates:
        input in[];
}

channel BlockchainLink extends ned.DelayChannel
{
    parameters:
        delay = default(250ms);
}

network BlockchainLab
{
    parameters:
        int numNodes = default(5);
        int difficulty = default(1);
        double blockInterval @unit(s) = default(8s);
        double txStartTime @unit(s) = default(-1s);
        double txInterval @unit(s) = default(3s);
        string txScript = default("");
        int maxTxPerBlock = default(3);
        double linkDelay @unit(s) = default(250ms);
        @display("bgb=1080,720");
    submodules:
        monitor: ConsensusMonitor {
            parameters:
                numNodes = parent.numNodes;
                @display("p=540,80");
        }
        node[numNodes]: BlockchainNode {
            parameters:
                nodeId = index;
                numNodes = parent.numNodes;
                difficulty = parent.difficulty;
                blockInterval = parent.blockInterval;
                txStartTime = parent.txStartTime;
                txInterval = parent.txInterval;
                txScript = parent.txScript;
                maxTxPerBlock = parent.maxTxPerBlock;
        }
    connections allowunconnected:
        for i = 0..numNodes-1 {
            node[i].statusOut --> monitor.in++;
            node[i].out++ --> BlockchainLink { delay = parent.linkDelay; } --> node[(i+1) % numNodes].in++;
            node[i].out++ --> BlockchainLink { delay = parent.linkDelay; } --> node[(i+numNodes-1) % numNodes].in++;
        }
}
