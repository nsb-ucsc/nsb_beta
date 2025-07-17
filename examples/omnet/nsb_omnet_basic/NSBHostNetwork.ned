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

network NSBHostNetwork
{
    @display("bgb=476,371");
    submodules:
        host0: NSBHost {
            parameters:
                hostId = "host0";
                serverAddress = "127.0.0.1";
                serverPort = 65432;
        }
        host1: NSBHost {
            parameters:
                hostId = "host1";
                serverAddress = "127.0.0.1";
                serverPort = 65432;
                @display("p=265,74");
        }

    connections allowunconnected:
        host0.out[0] --> host1.in[0];
        host1.out[0] --> host0.in[0];
}
