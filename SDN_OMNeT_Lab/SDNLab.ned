simple Host
{
    parameters:
        bool isSender = default(false);
        string destination = default("");
        int packetCount = default(3);
        int packetBytes = default(1000);
        double startTime @unit(s) = default(0.2s);
        double sendInterval @unit(s) = default(1s);
        @display("i=device/pc");
    gates:
        input in;
        output out;
}

simple SdnSwitch
{
    gates:
        input hostIn[4];
        output hostOut[4];
        input ctrlIn;
        output ctrlOut;
}

simple Controller
{
    gates:
        input in[2];
        output out[2];
}

network SDNLab
{
    parameters:
        double hostLinkDelay @unit(s) = default(10ms);
        double trunkDelay @unit(s) = default(20ms);
        double controllerLinkDelay @unit(s) = default(30ms);
    @display("bgb=900,520");
    submodules:
        hostA: Host {
            @display("p=95,170");
        }
        hostB: Host {
            @display("p=95,290");
        }
        hostC: Host {
            @display("p=95,410");
        }
        hostD: Host {
            @display("p=805,170");
        }
        hostE: Host {
            @display("p=805,290");
        }
        hostF: Host {
            @display("p=805,410");
        }
        s1: SdnSwitch {
            @display("p=330,290;i=device/switch;is=vs");
        }
        s2: SdnSwitch {
            @display("p=570,290;i=device/switch;is=vs");
        }
        controller: Controller {
            @display("p=450,75;i=device/server;is=vs");
        }
    connections:
        hostA.out --> { delay = parent.hostLinkDelay; } --> s1.hostIn[0];
        s1.hostOut[0] --> { delay = parent.hostLinkDelay; } --> hostA.in;
        hostB.out --> { delay = parent.hostLinkDelay; } --> s1.hostIn[1];
        s1.hostOut[1] --> { delay = parent.hostLinkDelay; } --> hostB.in;
        hostC.out --> { delay = parent.hostLinkDelay; } --> s1.hostIn[2];
        s1.hostOut[2] --> { delay = parent.hostLinkDelay; } --> hostC.in;

        hostD.out --> { delay = parent.hostLinkDelay; } --> s2.hostIn[0];
        s2.hostOut[0] --> { delay = parent.hostLinkDelay; } --> hostD.in;
        hostE.out --> { delay = parent.hostLinkDelay; } --> s2.hostIn[1];
        s2.hostOut[1] --> { delay = parent.hostLinkDelay; } --> hostE.in;
        hostF.out --> { delay = parent.hostLinkDelay; } --> s2.hostIn[2];
        s2.hostOut[2] --> { delay = parent.hostLinkDelay; } --> hostF.in;

        // Port 3 on each switch is the inter-switch trunk.
        s1.hostOut[3] --> { delay = parent.trunkDelay; } --> s2.hostIn[3];
        s2.hostOut[3] --> { delay = parent.trunkDelay; } --> s1.hostIn[3];

        // The controller has a separate control link to each switch.
        s1.ctrlOut --> { delay = parent.controllerLinkDelay; } --> controller.in[0];
        controller.out[0] --> { delay = parent.controllerLinkDelay; } --> s1.ctrlIn;
        s2.ctrlOut --> { delay = parent.controllerLinkDelay; } --> controller.in[1];
        controller.out[1] --> { delay = parent.controllerLinkDelay; } --> s2.ctrlIn;
}
