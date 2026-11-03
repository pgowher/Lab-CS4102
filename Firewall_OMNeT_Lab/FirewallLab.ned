simple TrafficSource
{
    parameters:
        string sourceName;
        string sourceIP;
        string destinationIP = default("10.0.0.10");
        string mode = default("normal"); // normal, flood, oversized
        int packetCount = default(10);
        int packetBytes = default(500);
        double startTime @unit(s) = default(0.2s);
        double sendInterval @unit(s) = default(0.8s);
        @display("i=device/pc");
    gates:
        output out;
}

simple Firewall
{
    parameters:
        bool enableIPRule = default(true);
        bool enableSizeRule = default(true);
        bool enableRateRule = default(true);
        string blockedIP = default("203.0.113.99");
        int maxPacketBytes = default(1200);
        int maxPacketsPerWindow = default(4);
        double rateWindow @unit(s) = default(1s);
        @display("i=device/firewall");
    gates:
        input in[];
        output out;
}

simple ProtectedServer
{
    parameters:
        string serverIP = default("10.0.0.10");
        @display("i=device/server");
    gates:
        input in;
}

network FirewallLab
{
    parameters:
        double accessDelay @unit(s) = default(10ms);
        double firewallDelay @unit(s) = default(2ms);
        @display("bgb=920,470");
    submodules:
        client: TrafficSource {
            @display("p=110,105");
        }
        attacker: TrafficSource {
            @display("p=110,235;i=device/pc2");
        }
        largePacketAttacker: TrafficSource {
            @display("p=110,345;i=device/pc2");
        }
        blockedIPAttacker: TrafficSource {
            @display("p=345,390;i=device/pc2");
        }
        firewall: Firewall {
            @display("p=490,235");
        }
        server: ProtectedServer {
            @display("p=770,235");
        }
    connections:
        client.out --> { delay = parent.accessDelay; } --> firewall.in++;
        attacker.out --> { delay = parent.accessDelay; } --> firewall.in++;
        largePacketAttacker.out --> { delay = parent.accessDelay; } --> firewall.in++;
        blockedIPAttacker.out --> { delay = parent.accessDelay; } --> firewall.in++;
        firewall.out --> { delay = parent.firewallDelay; } --> server.in;
}
