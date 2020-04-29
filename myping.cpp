/*********
 * myping.cpp v0.1 ipv4/ipv6 ping program for ubuntu linux by Songjun Na (04/18/2020)
 * g++ myping.cpp -o myping
./myping [options] <address>
 -q 		quite mode
 -c count 	number of packets to send
 -t ttl 	max hop
 -o timeout 	timeout for wait
 -s packetsize 	should be >=1 and <=512
 change log:
 	04/18/2020	v0.1 	the first working version
*********/
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <limits>
#include <cmath>

#define PING_SIZE 64
#define DEFAULT_TTL 64
#define MAX_PING 512
#define PORT_NO 0
#define MAX_PACKET (MAX_PING + 40)
#define TIMEOUT 1
#define ERR_TIMEOUT 1

//interval in microsecond between packet sends
#define USLEEP 1000000 

using namespace std;

//memory allocation for packet send/receive accoring to packet size
void buff_alloc(icmphdr*& send_buf, ip*& recv_buf, int pkt_size);
void buff_alloc(icmp6_hdr*& send_buf, ip6_hdr*& recv_buf, int pkt_size);

//perform DNS resolution with given argv, setting TTL and destination
char *ping_setup(char *host, int ttl, int sock, sockaddr_in& dest);
char *ping_setup(char *host, int ttl, int sock, sockaddr_in6& dest);

//set wait time to the socket with default or given timeout value
int ping_timeout(int sock,int timeout);

//set ICMP HEADER value according to IPv4/IPv6 specification and prepare data padding
void ping_init(icmphdr* icmp_hdr, int pkt_size, int seq);
void ping_init(icmp6_hdr* icmp_hdr, int pkt_size, int seq);

//send ICMP packet to the socket and return the number of bytes sent
int ping_send(int sock, sockaddr_in& dest, icmphdr* send_buf, int pkt_size);
int ping_send(int sock, sockaddr_in6& dest, icmp6_hdr* send_buf, int pkt_size);

//receive ICMP packet and for IPv4, put it into IP structure where ICMP is located 20byte offset
//for IPv6, ignore packets other than ICMP_ECHO_REPLY(129) and ICMP_TIME_EXCEEDED(3)
//for IPv6, can't put the received data into IPv6 structure, but only into ICMPv6 structure.
//for IPv6, the fuction recvfrom has the limitation of cutting off IPv6 header 
//return status code -1(receive fail), 0(success), 1(receive fail but timeout noticed by errno == 11)
int ping_recv(int sock, sockaddr_in& src, ip* recv_buf, int pkt_size);
int ping_recv(int sock, sockaddr_in6& src, ip6_hdr* recv_buf, int pkt_size);

//perform checksum for ICMP, some servers don't reply with bad checksum
unsigned short checksum(unsigned short *buf, int size);

//to handle control-C interrup
int escape = 1;
void intHandler(int dummy)
{
	escape = 0;
}

void usage(char *command)
{
	cout<<command<<" [options] <address>"<<endl;
	cout<<" -q 		quite mode"<<endl;
	cout<<" -c count 	number of packets to send"<<endl;
	cout<<" -t ttl 	max hop"<<endl;
	cout<<" -o timeout 	timeout for wait"<<endl;
	cout<<" -s packetsize 	should be >=1 and <="<<MAX_PING<<endl;
}

int main(int argc, char* argv[]) {
	int seq = 1;
	int pkt_size = PING_SIZE;
	int ttl = DEFAULT_TTL;
	int timeout = TIMEOUT;
	int quiet = 0;
	int count = 0;
	struct timespec timestamp_s, timestamp_e, ttm_s, ttm_e;
	icmphdr * send_buf = 0;
	icmp6_hdr * send6_buf = 0;
	ip * recv_buf = 0;
	ip6_hdr * recv6_buf = 0;
	struct sockaddr_in src;
	struct sockaddr_in6 src6;
	struct sockaddr_in dest;
	struct sockaddr_in6 dest6;
	char * ipaddr = 0;
	double rtt_ms = 0.0, total_rtt_ms = 0.0;
	int receiv_cnt = 0, send_cnt = 0;
	int sock = 0;
	int ipv = 4;

	if(argc < 2)
	{
		usage(argv[0]);
		return 0;
	}
	int c;
	char * host = argv[argc-1];
	while ((c = getopt (argc, argv, "qo:t:s:c:")) != -1){
		switch (c)
		{
			case 'q':
				quiet = 1;
				break;
			case 'o':
				timeout = atoi(optarg);
				break;
			case 'c':
				count = atoi(optarg);
				break;
			case 't':
				ttl = atoi(optarg);
				break;
			case 's':
				pkt_size = atoi(optarg);
				break;
			case '?':
				usage(argv[0]);
				return 0;
			default:
				abort ();
		}
	}
//limit the data size to be within 512
	if(pkt_size <1 or pkt_size > MAX_PING) {
		usage(argv[0]);
		return 0;
	}

//simple test if host input is IPv4 or IPv6
	struct addrinfo *res = 0;
	getaddrinfo(host,NULL,NULL,&res);
	unsigned char ipaddr6[sizeof(struct in6_addr)];

//IP version is decided based on the version info of the first response of getaddrinfo
//prepare socket in accordance with the version chosen
//with ping_setup, the socket is set with TTL(max_hop) and destination resolved from DNS lookup
//the destination ipaddr is the return value
	if(res->ai_family == AF_INET6 ){
		sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
		ipaddr = ping_setup(host,ttl,sock,dest6);
		ipv = 6;
	}
	else {
		sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
		ipaddr = ping_setup(host,ttl,sock,dest);
	}
//the socket's waiting time is set
	ping_timeout(sock,timeout);

	if(sock < 0){ 
		cerr<<"socket open error"<<endl;
	}

	if(ipaddr == NULL)
	{
		cerr<<"invalid hostname or address "<<host<<endl;
		return 0;
	}

//according to IP version, corresponding send/receive buffers are allocated
	if(ipv == 4){ 
		buff_alloc(send_buf, recv_buf, pkt_size);
	}
	if(ipv == 6){
		buff_alloc(send6_buf, recv6_buf, pkt_size);
	}
	signal(SIGINT, intHandler);

//start timestamp of the whole batch of ping transactions
	clock_gettime(CLOCK_MONOTONIC,&ttm_s);

//variables to show the statistics, xsum, xxsum, min_rtt, max_rtt are initialized
	double xsum = 0.0;
	double xxsum = 0.0;
	double min_rtt = numeric_limits<double>::max(); 
	double max_rtt = 0.0;

	char *src_ipaddr = (char *)malloc(NI_MAXHOST*sizeof(char));
	while(escape) {
		int success = 1;
		int recv_status = -1 ; 
		short ret_size = -1;
		int ret_seq = -1;
		bool time_exceeded = false;
		int byte_sent = -1;

//interval between packet sends
		usleep(USLEEP);

//start timestamp for each packet transaction
		clock_gettime(CLOCK_MONOTONIC,&timestamp_s);

//according to IP version, ping packet is prepared by ping_init, sent by ping_send and received by ping_receive
//only if sending has succeeded, receiving happens
//if not quiet mode, display the destination IP resolved via ping_setup and the return value of sending which is how many bytes are sent
//recv_status : status code -1(receive fail), 0(success), 1(receive fail but timeout noticed by errno == 11)
		if(ipv == 4) {
			ping_init(send_buf, pkt_size, seq++);
			byte_sent = ping_send(sock,dest,send_buf,pkt_size); 
			if(ping_send(sock,dest,send_buf,pkt_size) < 0)
				success = 0;
			else{
				if(!quiet)
					cout<<"Ping "<<byte_sent<<" bytes to "<<ipaddr<<"..."<<flush;
				send_cnt++;
			}
			if(success)
				recv_status = ping_recv(sock,src,recv_buf,MAX_PACKET); 
		}
		if(ipv == 6) {
	       		ping_init(send6_buf, pkt_size, seq++);
			if(ping_send(sock,dest6,send6_buf,pkt_size) < 0)
				success = 0;
			else
				send_cnt++;
			if(success)
				recv_status = ping_recv(sock,src6,recv6_buf,MAX_PACKET); 
		}
//if receive success (recv_status == 0), need to show time elapsed between send and receive, and calculate statistics variables
		if(recv_status == 0){
//end timestamp for each packet transaction
			clock_gettime(CLOCK_MONOTONIC,&timestamp_e);
			double elapsed = ((double)(timestamp_e.tv_nsec - timestamp_s.tv_nsec))/1000000.0;
			rtt_ms = (timestamp_e.tv_sec - timestamp_s.tv_sec)*1000.0 + elapsed;
			int ret_type;
			int ret_ttl = -1; 
//if IPv4, IP header can be accessed, so it can get source IP address and TTL
//For IPv6, can't show hop value and source IP address.
//For IPv6, if not TIME_EXCEEDED, then assume the reply from 'target', else from 'router' in the middle
//For IPv6, ret_ttl = -1 remains unchanged to suppress ttl display
//there are various ICMP return types, but in this version(v0.1) only ECHO_REPLY and TIME_EXCEEDED are inspected 
//if TIME_EXCEEDED, then need to show the result
			if(ipv == 4) {
				strcpy(src_ipaddr, inet_ntoa( *(struct in_addr *) &recv_buf->ip_src));
				icmphdr * rply_hdr = (icmphdr *)(recv_buf+1);
				ret_type = rply_hdr->type;
				ret_ttl = int(recv_buf->ip_ttl);
				ret_size = short(htons(recv_buf->ip_len) - sizeof(ip));
				ret_seq = int(rply_hdr->un.echo.sequence);
				if(ret_type == ICMP_TIME_EXCEEDED)
					time_exceeded = true;
			}
			else {
				strcpy(src_ipaddr, "target");
//				inet_ntop(AF_INET6,&((ip6_hdr *)((char *)recv6_buf))->ip6_src,src_ipaddr,INET6_ADDRSTRLEN);
				icmp6_hdr * rply_hdr = (icmp6_hdr *)recv6_buf;
				ret_type = rply_hdr->icmp6_type;
//				ret_ttl = int(recv6_buf->ip6_ctlun.ip6_un1.ip6_un1_hlim);
				ret_seq = short(htons(rply_hdr->icmp6_seq));
				if(ret_type == 3){
					strcpy(src_ipaddr, "router");
					time_exceeded = true;
				}
			}
			if(!quiet){
				if (ret_size >= 0)
					cout<<ret_size<<" bytes ";
				cout<<"from "<<src_ipaddr<<": icmp_seq="<<ret_seq;
				if(time_exceeded)
					cout<<" (icmp time exceeded)";
				else if(ret_ttl >= 0)
					cout<<" ttl="<<ret_ttl;
				cout<<" time="<<rtt_ms<<" ms"<<endl;
			}
			receiv_cnt++;
			xsum += rtt_ms; 
			xxsum += rtt_ms*rtt_ms; 
			min_rtt = min(rtt_ms,min_rtt);
			max_rtt = max(rtt_ms,max_rtt);
		}
//if receive timeout (recv_status == 1)
		else if(recv_status == ERR_TIMEOUT and !quiet)
			cout<<"timeout"<<endl;
//if for count option -c is set
		if(count and send_cnt >= count )
			break;
	}
//end timestamp of the whole batch of ping transactions
	clock_gettime(CLOCK_MONOTONIC,&ttm_e);
	double elapsed = ((double)(ttm_e.tv_nsec - ttm_s.tv_nsec))/1000000.0;
	rtt_ms = (ttm_e.tv_sec - ttm_s.tv_sec)*1000.0 + elapsed;

//final statistics calculation and print
	if(send_cnt > 0) {
		cout<<"--- "<<host<<" ping statistics ---"<<endl;
		cout<<send_cnt<<" packets transmitted, "<<receiv_cnt<<" received, "<<((send_cnt - receiv_cnt)*1.0/send_cnt)*100.0;
		cout<<"% packet loss, time "<<rtt_ms<<"ms"<<endl;
	}
	if (receiv_cnt > 0) {
		cout<<"rtt min/avg/max/mdev = "<<min_rtt<<"/";
		cout<<xsum/receiv_cnt<<"/"<<max_rtt<<"/";
		cout<<sqrt(xxsum/receiv_cnt - (xsum/receiv_cnt)*(xsum/receiv_cnt))<<" ms\n";
	}
	
//delete memory allocation and close socket

	if(ipv == 4){
		delete[]send_buf;
		delete[]recv_buf;
	}
	else {
		delete[]send6_buf;
		delete[]recv6_buf;
	}
	shutdown(sock,2);

	return 0;
}

char* ping_setup(char *host, int ttl, int sock, sockaddr_in& dest) {
	int ttl_set_status = setsockopt(sock,SOL_IP, IP_TTL,(const char*) &ttl, sizeof(ttl)); 
	char *ipaddr = (char *)malloc(NI_MAXHOST*sizeof(char));

	if( ttl_set_status != 0) {
		cerr<<"socket TTL option fail"<<endl;
		return NULL;
	}

	struct hostent *host_entity;
	dest.sin_family = AF_INET;
	dest.sin_port = htons(PORT_NO);
	if( (host_entity = gethostbyname(host)) == NULL )
	{
		cerr<<"ip resolution fail for "<<host<<endl;
		return NULL;
	}
	strcpy(ipaddr, inet_ntoa( *(struct in_addr *) host_entity->h_addr));
	dest.sin_addr.s_addr = *(long*)host_entity->h_addr;
//	cout<<"PING "<<ipaddr<<endl;

	return ipaddr;
}

char* ping_setup(char *host, int ttl, int sock, sockaddr_in6& dest) {
	char *ipaddr = (char *)malloc(INET6_ADDRSTRLEN*sizeof(char));

	struct addrinfo *res = 0;
	getaddrinfo(host,NULL,NULL,&res);
	struct sockaddr_in6* a= (struct sockaddr_in6*)res->ai_addr;
	inet_ntop(AF_INET6, &a->sin6_addr, ipaddr, sizeof(ipaddr));
	int ttl_set_status = setsockopt(sock,SOL_IPV6, IPV6_UNICAST_HOPS,(const char*) &ttl, sizeof(ttl)); 
	dest.sin6_family = AF_INET6;
	dest.sin6_addr = a->sin6_addr;
	if( ttl_set_status != 0) {
		cerr<<"socket TTL option fail"<<endl;
		return NULL;
	}
	return ipaddr;
}

int ping_timeout(int sock,int timeout) {
	struct timeval tv_out;
	tv_out.tv_sec = timeout;
	tv_out.tv_usec = 0;
	if(setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(const char*)&tv_out, sizeof(tv_out)) < 0){
		cerr<<"socket timeout fail"<<endl;
		return -1;
	}
	return 0;
}

void buff_alloc(icmphdr*& send_buf, ip*& recv_buf, int pkt_size) {
	send_buf = (icmphdr *)new char[pkt_size];
	recv_buf = (ip *)new char[MAX_PACKET];
	return;
}
void buff_alloc(icmp6_hdr*& send_buf, ip6_hdr*& recv_buf, int pkt_size) {
	send_buf = (icmp6_hdr *)new char[pkt_size];
	recv_buf = (ip6_hdr *)new char[MAX_PACKET];
	return;
}
void ping_init(icmphdr* icmp_hdr, int pkt_size, int seq) {
	icmp_hdr->type = ICMP_ECHO;
	icmp_hdr->code = 0;
	icmp_hdr->un.echo.id = getpid();
	icmp_hdr->un.echo.sequence = seq;
	icmp_hdr->checksum= 0;

	char* data = (char*)icmp_hdr + sizeof(icmphdr);
	int bytes_to_fill = pkt_size - sizeof(icmphdr);
	for(int i = 0; i < bytes_to_fill; i++)
		data[i] = i+'0';
	data[bytes_to_fill] = 0;
	icmp_hdr->checksum = checksum((unsigned short*)icmp_hdr,pkt_size);
}
void ping_init(icmp6_hdr* icmp_hdr, int pkt_size, int seq) {
	icmp_hdr->icmp6_type = ICMP6_ECHO_REQUEST;
	icmp_hdr->icmp6_code = 0;
	icmp_hdr->icmp6_id = getpid();
	icmp_hdr->icmp6_seq = htons(seq);
	icmp_hdr->icmp6_cksum = 0;

	char* data = (char*)icmp_hdr + sizeof(icmp6_hdr);
	int bytes_to_fill = pkt_size - sizeof(icmp6_hdr);
	for(int i = 0; i < bytes_to_fill; i++)
		data[i] = i+'0';
	data[bytes_to_fill] = 0;
	icmp_hdr->icmp6_cksum = checksum((unsigned short*)icmp_hdr,pkt_size);
}

int ping_send(int sock, sockaddr_in& dest, icmphdr* send_buf, int pkt_size) {

	int byte_written = sendto(sock,(char *)send_buf, pkt_size, 0, (sockaddr*)&dest, sizeof(dest));
	if (byte_written <= 0) {
		cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
		cerr <<"send fail"<<endl;
		return -1;
	}
	return byte_written;
}

int ping_send(int sock, sockaddr_in6& dest, icmp6_hdr* send_buf, int pkt_size) {

	char *ipaddr = (char *)malloc(INET6_ADDRSTRLEN*sizeof(char));
	inet_ntop(AF_INET6,&dest.sin6_addr,ipaddr,INET6_ADDRSTRLEN);

	int byte_written = sendto(sock,(char *)send_buf, pkt_size, 0, (sockaddr*)&dest, sizeof(struct sockaddr_in6));
	if (byte_written <= 0) {
		cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
		cerr <<"send fail"<<endl;
		return -1;
	}
	return byte_written;
}

int ping_recv(int sock, sockaddr_in& src, ip* recv_buf, int pkt_size) {
	unsigned int fromlen = sizeof(src);
	int byte_read = recvfrom(sock, (char *)recv_buf, pkt_size + sizeof(ip),0,(sockaddr*) &src, &fromlen);
	if(byte_read <= 0) {
		if (errno == 11) {
			return ERR_TIMEOUT;
		}
		cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
		cerr << "Ping receive error"<<endl;
		return -1;
	}
	return 0;
}
int ping_recv(int sock, sockaddr_in6& src, ip6_hdr * recv_buf, int pkt_size) {
	unsigned int fromlen = sizeof(src);
	int ret_type = -1;
	while(ret_type != 129 and ret_type != 3) {
		int byte_read = recvfrom(sock, (char *)recv_buf, pkt_size + sizeof(ip6_hdr),0,(sockaddr*) &src, &fromlen);
		icmp6_hdr * rply_hdr = (icmp6_hdr *)recv_buf;
		ret_type = rply_hdr->icmp6_type;
		if(byte_read <= 0) {
			if (errno == 11) {
				return ERR_TIMEOUT;
			}
			cerr<<" (errno=" << strerror(errno) << " (" << errno << ")).";
			cerr << "Ping receive error"<<endl;
			return -1;
		}
	}
	return 0;
}
unsigned short checksum(unsigned short* buf, int size) {
	unsigned int sum = 0;

	while(size>1)
	{
		sum += *buf++;
		size -= sizeof(unsigned short);
	}
	if(size)
		sum += *(unsigned char*)buf;
	sum = (sum>>16) + (sum & 0xfffF);
	sum += (sum >> 16);
	return (unsigned short)(~sum);
}
