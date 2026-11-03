simple TrafficSource
{
    parameters:
        string sourceId;
        string destinationSubnet = default("dmz");
        string mode = default("normal"); // normal, flood, scan, bruteForce
        int packetCount = default(10);
        int packetBytes = default(500);
        double startTime @unit(s) = default(0.2s);
        double sendInterval @unit(s) = default(0.5s);
        int basePort = default(80);
        @display("i=device/pc");
    gates:
        output out;
}

simple IDPS
{
    parameters:
        double rateWindow @unit(s) = default(1s);
        int floodThreshold = default(5);
        int scanThreshold = default(5);
        double scanWindow @unit(s) = default(2s);
        int authThreshold = default(5);
        double authWindow @unit(s) = default(3s);
        double blockDuration @unit(s) = default(10s);
        @display("i=device/firewall");
    gates:
        input in[];
        output out;
}

simple ProtectedServer
{
    parameters:
        @display("i=device/server");
    gates:
        input in;
}

simple AccessRouter
{
    parameters:
        string subnetLabel;
        @display("i=device/switch");
    gates:
        input in[];
        output out;
}

simple CoreRouter
{
    parameters:
        @display("i=device/switch");
    gates:
        input in;
        output toDMZ;
        output toLAN;
}

simple NetworkSegment
{
    parameters:
        string subnetLabel;
        @display("i=device/switch");
    gates:
        input in;
        output out;
}

network IDPSLab
{
    parameters:
        double accessDelay @unit(s) = default(10ms);
        double inspectionDelay @unit(s) = default(2ms);
    @display("bgb=1120,610");
    submodules:
        clientA: TrafficSource {
            @display("p=85,100");
        }
        clientB: TrafficSource {
            @display("p=85,235");
        }
        attackerWAN: TrafficSource {
            @display("p=85,465;i=device/pc2");
        }
        attackerLANB: TrafficSource {
            @display("p=85,330;i=device/pc2");
        }
        routerLANA: AccessRouter {
            subnetLabel = "LAN-A 10.10.1.0/24";
            @display("p=310,100");
        }
        routerLANB: AccessRouter {
            subnetLabel = "LAN-B 10.10.2.0/24";
            @display("p=310,285");
        }
        edgeRouter: AccessRouter {
            subnetLabel = "WAN 198.51.100.0/24";
            @display("p=310,465");
        }
        idps: IDPS {
            @display("p=535,285");
        }
        coreRouter: CoreRouter {
            @display("p=735,285");
        }
        dmz: NetworkSegment {
            subnetLabel = "DMZ 10.10.10.0/24";
            @display("p=930,160");
        }
        internalLAN: NetworkSegment {
            subnetLabel = "Server LAN 10.10.20.0/24";
            @display("p=930,410");
        }
        webServer: ProtectedServer {
            @display("p=1070,160");
        }
        fileServer: ProtectedServer {
            @display("p=1070,410");
        }
    connections:
        clientA.out --> { delay = parent.accessDelay; } --> routerLANA.in++;
        routerLANA.out --> { delay = parent.accessDelay; } --> idps.in++;
        clientB.out --> { delay = parent.accessDelay; } --> routerLANB.in++;
        attackerLANB.out --> { delay = parent.accessDelay; } --> routerLANB.in++;
        routerLANB.out --> { delay = parent.accessDelay; } --> idps.in++;
        attackerWAN.out --> { delay = parent.accessDelay; } --> edgeRouter.in++;
        edgeRouter.out --> { delay = parent.accessDelay; } --> idps.in++;
        idps.out --> { delay = parent.inspectionDelay; } --> coreRouter.in;
        coreRouter.toDMZ --> { delay = parent.accessDelay; } --> dmz.in;
        coreRouter.toLAN --> { delay = parent.accessDelay; } --> internalLAN.in;
        dmz.out --> { delay = parent.accessDelay; } --> webServer.in;
        internalLAN.out --> { delay = parent.accessDelay; } --> fileServer.in;
}
