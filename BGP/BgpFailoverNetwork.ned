import inet.common.scenario.ScenarioManager;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.inet.Router;
import inet.node.inet.StandardHost;
import inet.visualizer.canvas.integrated.IntegratedCanvasVisualizer;

// Four autonomous systems form two paths between the source and destination.
// The upper path is preferred initially; stopping AS 65002 forces BGP to
// withdraw it and use the lower path through AS 65004.
network BgpFailoverNetwork
{
    parameters:
        @display("bgb=1100,720;bgi=background/grid");
        @figure[title](type=text; pos=550,30; anchor=c; font=Arial,20,bold; color=#17365D; text="BGP: interdomain route withdrawal and failover");
        @figure[asA](type=text; pos=355,285; anchor=c; font=Arial,12,bold; color=#174EA6; text="AS 65001");
        @figure[asB](type=text; pos=545,120; anchor=c; font=Arial,12,bold; color=#174EA6; text="AS 65002");
        @figure[asC](type=text; pos=745,285; anchor=c; font=Arial,12,bold; color=#174EA6; text="AS 65003");
        @figure[asD](type=text; pos=545,460; anchor=c; font=Arial,12,bold; color=#174EA6; text="AS 65004");
        @figure[note](type=text; pos=550,545; anchor=c; font=Arial,11; color=#444444; text="At 60 s: upper router stops. At 100 s: it restarts. Watch ping delivery and BGP route changes.");
        @figure[legendTitle](type=text; pos=40,610; font=Arial,13,bold; text="BGP legend");
        @figure[upper](type=line; points=40,640,85,640; lineColor=#1A73E8; lineWidth=5);
        @figure[upperText](type=text; pos=95,640; anchor=w; text="Upper AS path (initial)");
        @figure[lower](type=line; points=365,640,410,640; lineColor=#D48806; lineWidth=5);
        @figure[lowerText](type=text; pos=420,640; anchor=w; text="Lower AS path (backup)");
        @figure[route](type=line; points=720,640,765,640; lineColor=#111111; lineWidth=5);
        @figure[routeText](type=text; pos=775,640; anchor=w; text="Observed ping route");
    submodules:
        visualizer: IntegratedCanvasVisualizer { @display("p=90,105;is=s"); }
        configurator: Ipv4NetworkConfigurator {
            parameters:
                config = xmldoc("IPv4Config.xml");
                addStaticRoutes = false;
                addDefaultRoutes = false;
                addSubnetRoutes = false;
                @display("p=90,190;is=s");
        }
        scenarioManager: ScenarioManager { @display("p=90,275;is=s"); }
        source: StandardHost { @display("p=175,285;i=device/laptop,red"); }
        west: Router {
            parameters:
                hasBgp = true;
                hasStatus = true;
                @display("p=355,285;i=abstract/router,blue");
            gates: ethg[3];
        }
        north: Router {
            parameters:
                hasBgp = true;
                hasStatus = true;
                @display("p=545,165;i=abstract/router,blue");
            gates: ethg[2];
        }
        south: Router {
            parameters:
                hasBgp = true;
                hasStatus = true;
                @display("p=545,405;i=abstract/router,gold");
            gates: ethg[2];
        }
        east: Router {
            parameters:
                hasBgp = true;
                hasStatus = true;
                @display("p=745,285;i=abstract/router,blue");
            gates: ethg[3];
        }
        destination: StandardHost { @display("p=925,285;i=device/laptop,green"); }
    connections:
        source.ethg++ <--> EthernetLink <--> west.ethg[0];
        west.ethg[1] <--> EthernetLink <--> north.ethg[0];
        north.ethg[1] <--> EthernetLink <--> east.ethg[0];
        west.ethg[2] <--> BackupLink <--> south.ethg[0];
        south.ethg[1] <--> BackupLink <--> east.ethg[1];
        east.ethg[2] <--> EthernetLink <--> destination.ethg++;
}

channel EthernetLink extends ned.DatarateChannel
{
    delay = 50us;
    datarate = 100Mbps;
    @display("ls=blue,5");
}

channel BackupLink extends ned.DatarateChannel
{
    delay = 300us;
    datarate = 100Mbps;
    @display("ls=gold,5");
}
