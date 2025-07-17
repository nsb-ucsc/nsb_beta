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


network NSBHostNetworkTenHosts
{
    @display("bgb=1000,800");

    submodules:
        host0: NSBHost { parameters: hostId = "host0"; @display("p=100,100"); }
        host1: NSBHost { parameters: hostId = "host1"; @display("p=300,100"); }
        host2: NSBHost { parameters: hostId = "host2"; @display("p=500,100"); }
        host3: NSBHost { parameters: hostId = "host3"; @display("p=700,100"); }
        host4: NSBHost { parameters: hostId = "host4"; @display("p=900,100"); }
        host5: NSBHost { parameters: hostId = "host5"; @display("p=100,300"); }
        host6: NSBHost { parameters: hostId = "host6"; @display("p=300,300"); }
        host7: NSBHost { parameters: hostId = "host7"; @display("p=500,300"); }
        host8: NSBHost { parameters: hostId = "host8"; @display("p=700,300"); }
        host9: NSBHost { parameters: hostId = "host9"; @display("p=900,300"); }

    connections allowunconnected:
        host0.out[0] --> host0.in[0];
        host0.out[1] --> host1.in[0];
        host0.out[2] --> host2.in[0];
        host0.out[3] --> host3.in[0];
        host0.out[4] --> host4.in[0];
        host0.out[5] --> host5.in[0];
        host0.out[6] --> host6.in[0];
        host0.out[7] --> host7.in[0];
        host0.out[8] --> host8.in[0];
        host0.out[9] --> host9.in[0];
    
        host1.out[0] --> host0.in[1];
        host1.out[1] --> host1.in[1];
        host1.out[2] --> host2.in[1];
        host1.out[3] --> host3.in[1];
        host1.out[4] --> host4.in[1];
        host1.out[5] --> host5.in[1];
        host1.out[6] --> host6.in[1];
        host1.out[7] --> host7.in[1];
        host1.out[8] --> host8.in[1];
        host1.out[9] --> host9.in[1];
    
        host2.out[0] --> host0.in[2];
        host2.out[1] --> host1.in[2];
        host2.out[2] --> host2.in[2];
        host2.out[3] --> host3.in[2];
        host2.out[4] --> host4.in[2];
        host2.out[5] --> host5.in[2];
        host2.out[6] --> host6.in[2];
        host2.out[7] --> host7.in[2];
        host2.out[8] --> host8.in[2];
        host2.out[9] --> host9.in[2];
    
        host3.out[0] --> host0.in[3];
        host3.out[1] --> host1.in[3];
        host3.out[2] --> host2.in[3];
        host3.out[3] --> host3.in[3];
        host3.out[4] --> host4.in[3];
        host3.out[5] --> host5.in[3];
        host3.out[6] --> host6.in[3];
        host3.out[7] --> host7.in[3];
        host3.out[8] --> host8.in[3];
        host3.out[9] --> host9.in[3];
    
        host4.out[0] --> host0.in[4];
        host4.out[1] --> host1.in[4];
        host4.out[2] --> host2.in[4];
        host4.out[3] --> host3.in[4];
        host4.out[4] --> host4.in[4];
        host4.out[5] --> host5.in[4];
        host4.out[6] --> host6.in[4];
        host4.out[7] --> host7.in[4];
        host4.out[8] --> host8.in[4];
        host4.out[9] --> host9.in[4];
    
        host5.out[0] --> host0.in[5];
        host5.out[1] --> host1.in[5];
        host5.out[2] --> host2.in[5];
        host5.out[3] --> host3.in[5];
        host5.out[4] --> host4.in[5];
        host5.out[5] --> host5.in[5];
        host5.out[6] --> host6.in[5];
        host5.out[7] --> host7.in[5];
        host5.out[8] --> host8.in[5];
        host5.out[9] --> host9.in[5];
    
        host6.out[0] --> host0.in[6];
        host6.out[1] --> host1.in[6];
        host6.out[2] --> host2.in[6];
        host6.out[3] --> host3.in[6];
        host6.out[4] --> host4.in[6];
        host6.out[5] --> host5.in[6];
        host6.out[6] --> host6.in[6];
        host6.out[7] --> host7.in[6];
        host6.out[8] --> host8.in[6];
        host6.out[9] --> host9.in[6];
    
        host7.out[0] --> host0.in[7];
        host7.out[1] --> host1.in[7];
        host7.out[2] --> host2.in[7];
        host7.out[3] --> host3.in[7];
        host7.out[4] --> host4.in[7];
        host7.out[5] --> host5.in[7];
        host7.out[6] --> host6.in[7];
        host7.out[7] --> host7.in[7];
        host7.out[8] --> host8.in[7];
        host7.out[9] --> host9.in[7];
    
        host8.out[0] --> host0.in[8];
        host8.out[1] --> host1.in[8];
        host8.out[2] --> host2.in[8];
        host8.out[3] --> host3.in[8];
        host8.out[4] --> host4.in[8];
        host8.out[5] --> host5.in[8];
        host8.out[6] --> host6.in[8];
        host8.out[7] --> host7.in[8];
        host8.out[8] --> host8.in[8];
        host8.out[9] --> host9.in[8];
    
        host9.out[0] --> host0.in[9];
        host9.out[1] --> host1.in[9];
        host9.out[2] --> host2.in[9];
        host9.out[3] --> host3.in[9];
        host9.out[4] --> host4.in[9];
        host9.out[5] --> host5.in[9];
        host9.out[6] --> host6.in[9];
        host9.out[7] --> host7.in[9];
        host9.out[8] --> host8.in[9];
        host9.out[9] --> host9.in[9];
    
}
