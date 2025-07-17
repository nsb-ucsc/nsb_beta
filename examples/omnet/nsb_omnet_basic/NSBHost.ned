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

//
// TODO auto-generated module
//
simple NSBHost
{
    parameters:
        string hostId;
        @display("i=device/pc");
        
        //NSBSimClient support 
        string serverAddress  = default("127.0.0.1");
        int serverPort = default(65432);

        double sendInterval @unit(s) = default(1s);
        
        int inGateSize = default(10);
        int outGateSize = default(10);

    gates:
        input in[inGateSize]; //in
        output out[outGateSize]; //out
        //input radioIn @directIn;
        //output radioOut @directOut;
}

