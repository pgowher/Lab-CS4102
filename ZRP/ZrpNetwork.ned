simple ZrpNode
{
    parameters:
        int nodeId;
        double x @unit(m);
        double y @unit(m);
        double range @unit(m) = default(250m);
        int zoneRadius = default(2);
        @class(ZrpNode);
        @display("i=device/wifilaptop");
    gates:
        input radioIn;
}

// A compact packet-level ZRP teaching model. Nodes broadcast link-state
// updates inside a two-hop zone and bordercast queries beyond that zone.
network ZrpNetwork
{
    parameters:
        @display("bgb=1100,720;bgi=background/grid");
        @figure[title](type=text; pos=550,25; anchor=c; font=Arial,20,bold; color=#17365D; text="ZRP: proactive local zones and reactive inter-zone discovery");
        @figure[note](type=text; pos=550,55; anchor=c; font=Arial,11; color=#444444; text="Zone radius = 2 hops; radio range = 250 m. The moving relay leaves the short path at 18 s and returns at 36 s.");
        @figure[legendTitle](type=text; pos=38,610; font=Arial,13,bold; text="ZRP legend");
        @figure[hello](type=line; points=40,640,85,640; lineColor=#FF8C00; lineWidth=4);
        @figure[helloText](type=text; pos=95,640; anchor=w; text="ZONE UPDATE: periodic local link state");
        @figure[query](type=line; points=425,640,470,640; lineColor=#0000FF; lineWidth=4);
        @figure[queryText](type=text; pos=480,640; anchor=w; text="BORDERCAST: inter-zone route request");
        @figure[reply](type=line; points=835,640,880,640; lineColor=#008000; lineWidth=4);
        @figure[replyText](type=text; pos=890,640; anchor=w; text="ROUTE REPLY");
        @figure[data](type=line; points=40,680,85,680; lineColor=#111111; lineWidth=4);
        @figure[dataText](type=text; pos=95,680; anchor=w; text="PING: hop-by-hop payload");
        @figure[move](type=line; points=425,680,470,680; lineColor=#DAA520; lineWidth=4);
        @figure[moveText](type=text; pos=480,680; anchor=w; text="Gold: moving relay trajectory");
        @figure[trail](type=line; points=520,325,520,500; lineColor=#DAA520; lineWidth=2);
    submodules:
        host[10]: ZrpNode {
            parameters:
                nodeId = index;
                @display("i=device/wifilaptop");
        }
    connections allowunconnected:
}
