# myping
myping.cpp 

compile and execution:
g++ myping.cpp -o myping

Users should be allowed to create sockets.
./myping [options] <address>
 -q 		quite mode
 -c count 	number of packets to send
 -t ttl 	max hop
 -o timeout 	timeout for wait
 -s packetsize 	should be >=1 and <=512

backgroud:
This is my first socket program.
So I took reference of several sample ICMP ping examples for C/C++.
One is written for [winsock](https://tangentsoft.net/wskfaq/examples/rawping.html) and another is supporting only [IPv4](https://www.geeksforgeeks.org/ping-in-c/).
But they were good enough to understand how to create sockets, set socket options, send packets and receive packets.
And I aimed at the Linux platform with sudo previlege (or with the right to create sockets).

program structure:
Based on such understanding, I could figure out the program should be put into 6 steps.
1. DNS 
2. prepare socket
3. prepare packet
4. send
5. receive
6. show the results
I put the loop of calling 3,4,5 and 6. showing into main driver function.

options:
There are many options at ping utility provided by Ubuntu.
But I think the most frequently used options are. 
-c limit number of packets to send instead of infinite loop.
-q suppress per packet result.
-s set the size of the ping packet. 
-t set the TTL of IP, which limit the number of hops one IP packet can travel.
-o set how long the receiving can wait and abort.

notes for IPv6:
This version doesn't require explicit '-6' option as it detects if the input destination has IPv6 addresses.
This version of program can't access IPv6 header of received packet.
It only returns ICMPv6 header. So the program can't display the source IP address of ICMP messages returned.
Neither it can acquire TTL info. So this version can't show TTL.
Next version will implement IPv6 header parsing by using recvmsg instead of recvfrom. 
This version ignores all other IPv6 ICMP returns than echo reply and time exceeded.
I checked the packets sent/received via tcpdump and wireshark.

missing features:
This version doesn't support reverse DNS lookup for now.
This version doesn't parse the full context of ICMP types/codes only 'time exceeded'.
This version doesn't have options for interval, interface, mark, MTU, preload, pattern, TOS, send buffer, timestamp, deadline, and explicit hops.

sample output:
sudo ./myping -c5 1.1.1.1
Ping 64 bytes to 1.1.1.1...64 bytes from 1.1.1.1: icmp_seq=1 ttl=56 time=7.73989 ms
Ping 64 bytes to 1.1.1.1...64 bytes from 1.1.1.1: icmp_seq=1 ttl=56 time=0.055107 ms
Ping 64 bytes to 1.1.1.1...64 bytes from 1.1.1.1: icmp_seq=2 ttl=56 time=0.072499 ms
Ping 64 bytes to 1.1.1.1...64 bytes from 1.1.1.1: icmp_seq=2 ttl=56 time=0.096553 ms
Ping 64 bytes to 1.1.1.1...64 bytes from 1.1.1.1: icmp_seq=3 ttl=56 time=0.047938 ms
--- 1.1.1.1 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 5008.89ms
rtt min/avg/max/mdev = 0.047938/1.6024/7.73989/3.06879 ms

sudo ./myping -c5 -q www.naver.com
--- www.naver.com ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 5005.6ms
rtt min/avg/max/mdev = 0.040733/0.974472/4.68451/1.85503 ms

sudo ./myping -c5 -t 10 ipv6.google.com
from router: icmp_seq=0 (icmp time exceeded) time=149.033 ms
from router: icmp_seq=0 (icmp time exceeded) time=149.873 ms
from router: icmp_seq=0 (icmp time exceeded) time=150.384 ms
from router: icmp_seq=0 (icmp time exceeded) time=149.146 ms
from router: icmp_seq=0 (icmp time exceeded) time=149.215 ms
--- ipv6.google.com ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 5748.51ms
rtt min/avg/max/mdev = 149.033/149.53/150.384/0.517581 ms
