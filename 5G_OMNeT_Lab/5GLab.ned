simple UE
{
    parameters:
        string sliceName;
        int qfi;
        int packetCount = default(40);
        int packetBytes = default(1200);
        double startTime @unit(s) = default(0.1s);
        double sendInterval @unit(s) = default(10ms);
        double trafficStartOffset @unit(s) = default(10ms);
    gates:
        input ctrlIn;
        output ctrlOut;
        output dataOut;
}

simple Gnb
{
    parameters:
        int highPriorityQfi = default(7);
        int maxQueuePackets = default(160);
        double ranDataRate @unit(bps) = default(5Mbps);
    gates:
        input ueCtrlIn[4];
        output ueCtrlOut[4];
        input ueDataIn[4];
        output dataOut;
        input coreCtrlIn;
        output coreCtrlOut;
}

simple AMF
{
    gates:
        input gnbIn;
        output gnbOut;
        input smfIn;
        output smfOut;
}

simple SMF
{
    gates:
        input amfIn;
        output amfOut;
        input upfIn;
        output upfOut;
}

simple UPF
{
    parameters:
        double processingDelay @unit(s) = default(1ms);
    gates:
        input ctrlIn;
        output ctrlOut;
        input dataIn;
        output dataOut;
}

simple DataNetwork
{
    gates:
        input in;
}

network FiveGStandaloneLab
{
    parameters:
        double radioLinkDelay @unit(s) = default(2ms);
        double n3LinkDelay @unit(s) = default(2ms);
        double coreLinkDelay @unit(s) = default(2ms);
        double controlLinkDelay @unit(s) = default(5ms);
    @display("bgb=1050,570");
    submodules:
        ueUrllc1: UE {
            parameters:
                sliceName = "URLLC";
                qfi = 7;
                packetCount = default(40);
                packetBytes = default(128);
                sendInterval = default(10ms);
            @display("p=80,130;i=device/phone");
        }
        ueUrllc2: UE {
            parameters:
                sliceName = "URLLC";
                qfi = 7;
                packetCount = default(40);
                packetBytes = default(128);
                startTime = default(0.12s);
                sendInterval = default(10ms);
            @display("p=80,230;i=device/phone");
        }
        ueEmbb1: UE {
            parameters:
                sliceName = "eMBB";
                qfi = 9;
                packetCount = default(30);
                packetBytes = default(1200);
                startTime = default(0.11s);
                sendInterval = default(15ms);
            @display("p=80,350;i=device/phone");
        }
        ueEmbb2: UE {
            parameters:
                sliceName = "eMBB";
                qfi = 9;
                packetCount = default(30);
                packetBytes = default(1200);
                startTime = default(0.13s);
                sendInterval = default(15ms);
            @display("p=80,450;i=device/phone");
        }
        gnb: Gnb {
            @display("p=330,290;i=device/antennatower");
        }
        amf: AMF {
            @display("p=535,100;i=device/server");
        }
        smf: SMF {
            @display("p=740,100;i=device/server");
        }
        upf: UPF {
            @display("p=740,355;i=device/server");
        }
        dataNetwork: DataNetwork {
            @display("p=965,355;i=device/cloud");
        }
    connections:
        ueUrllc1.ctrlOut --> { delay = parent.radioLinkDelay; } --> gnb.ueCtrlIn[0];
        gnb.ueCtrlOut[0] --> { delay = parent.radioLinkDelay; } --> ueUrllc1.ctrlIn;
        ueUrllc2.ctrlOut --> { delay = parent.radioLinkDelay; } --> gnb.ueCtrlIn[1];
        gnb.ueCtrlOut[1] --> { delay = parent.radioLinkDelay; } --> ueUrllc2.ctrlIn;
        ueEmbb1.ctrlOut --> { delay = parent.radioLinkDelay; } --> gnb.ueCtrlIn[2];
        gnb.ueCtrlOut[2] --> { delay = parent.radioLinkDelay; } --> ueEmbb1.ctrlIn;
        ueEmbb2.ctrlOut --> { delay = parent.radioLinkDelay; } --> gnb.ueCtrlIn[3];
        gnb.ueCtrlOut[3] --> { delay = parent.radioLinkDelay; } --> ueEmbb2.ctrlIn;

        ueUrllc1.dataOut --> { delay = parent.radioLinkDelay; } --> gnb.ueDataIn[0];
        ueUrllc2.dataOut --> { delay = parent.radioLinkDelay; } --> gnb.ueDataIn[1];
        ueEmbb1.dataOut --> { delay = parent.radioLinkDelay; } --> gnb.ueDataIn[2];
        ueEmbb2.dataOut --> { delay = parent.radioLinkDelay; } --> gnb.ueDataIn[3];
        gnb.dataOut --> { delay = parent.n3LinkDelay; } --> upf.dataIn;
        upf.dataOut --> { delay = parent.n3LinkDelay; } --> dataNetwork.in;

        gnb.coreCtrlOut --> { delay = parent.controlLinkDelay; } --> amf.gnbIn;
        amf.gnbOut --> { delay = parent.controlLinkDelay; } --> gnb.coreCtrlIn;
        amf.smfOut --> { delay = parent.coreLinkDelay; } --> smf.amfIn;
        smf.amfOut --> { delay = parent.coreLinkDelay; } --> amf.smfIn;
        smf.upfOut --> { delay = parent.coreLinkDelay; } --> upf.ctrlIn;
        upf.ctrlOut --> { delay = parent.coreLinkDelay; } --> smf.upfIn;
}
