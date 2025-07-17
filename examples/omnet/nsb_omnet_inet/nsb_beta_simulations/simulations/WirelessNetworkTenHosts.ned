//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package simulations;

import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.contract.INetworkNode;
import inet.physicallayer.wireless.common.contract.packetlevel.IRadioMedium;
import inet.visualizer.contract.IIntegratedVisualizer;

network WirelessNetworkTenHosts
{
    parameters:
        @display("bgb=650,500;bgg=100,1,grey95");
        @figure[title](type=label; pos=0,-1; anchor=sw; color=darkblue);
    submodules:
        visualizer: <default(firstAvailableOrEmpty("IntegratedCanvasVisualizer"))> like IIntegratedVisualizer if typename != "" {
            @display("p=472.752,26.208");
        }
        configurator: Ipv4NetworkConfigurator {
            @display("p=568.512,26.208");
        }
        radioMedium: <default("UnitDiskRadioMedium")> like IRadioMedium {
            @display("p=604.8,442.51202");
        }
        host0: <default("WirelessHost")> like INetworkNode {
            @display("p=100,100");
        }
        host1: <default("WirelessHost")> like INetworkNode {
            @display("p=250,100");
        }
        
        host2: <default("WirelessHost")> like INetworkNode {
            @display("p=400,100");
        }
        host3: <default("WirelessHost")> like INetworkNode {
            @display("p=550,100");
        }
        host4: <default("WirelessHost")> like INetworkNode {
            @display("p=100,250");
        }
        host5: <default("WirelessHost")> like INetworkNode {
            @display("p=250,250");
        }
        host6: <default("WirelessHost")> like INetworkNode {
           @display("p=400,250");
        }
        host7: <default("WirelessHost")> like INetworkNode {
            @display("p=550,250");
        }
        host8: <default("WirelessHost")> like INetworkNode {
            @display("p=100,400");
        }
        host9: <default("WirelessHost")> like INetworkNode {
            @display("p=250,400");
       }

}