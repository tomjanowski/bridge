#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>
#include <iomanip>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <string.h>
#include <poll.h>
using namespace std;
const int LEN=16384;
unsigned char buffer[LEN];
struct ifreq netdevice;
void print_hex(unsigned char *data, int len) {
  cout << hex << setfill('0');
  cout << setw(3) << dec << 0 << hex << "  ";
  for (int i=0;i<len;++i) {
    cout << setw(2) << (int) buffer[i];
    if ((i+1)%16==0) { 
      cout << endl;
      cout << setw(3) << dec << i/16 << hex << "  ";
      }
    else cout << " ";
    }
  cout << dec << setfill(' ') << endl;
  cout << "--" << endl;
}
int main(int argc , char * argv[]) try {
  int ret,fd[2];
  if (argc<3) throw "arguments are missing";
  for (int k=0;k<2;++k) {
// open sockets
  fd[k]=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
  perror("socket");
  if (fd[k]<0) throw "Dupa 1a";

// assign specific interfaces
  strncpy(netdevice.ifr_name,argv[k+1],IFNAMSIZ);
  ret=ioctl(fd[k],SIOCGIFINDEX,&netdevice);
  perror("ioctl");
  if (ret<0) throw "Dupa 3";
  sockaddr_ll sckbind;
  sckbind.sll_family=AF_PACKET;
  sckbind.sll_protocol=0;
  sckbind.sll_ifindex=netdevice.ifr_ifindex;
  ret=bind(fd[k],reinterpret_cast<sockaddr*>(&sckbind),sizeof(sckbind));
  if (ret<0) throw "Dupa 4";
  }

// setup polling
  struct pollfd polfd[2];
  for (int i=0;i<2;++i) {
    polfd[i].fd=fd[i];
    polfd[i].events=POLLIN;
    polfd[i].revents=0;
    }
  for (;;) {
    ret=poll(polfd,2,-1);
    if (ret<0) throw "Dupa 5";
//  receive and print packet
    ssize_t x;
    for (int i=0;i<2;++i) {
      if (polfd[i].revents==POLLIN) {
        x=recv(fd[i],buffer,LEN,0);
        print_hex(buffer,x);
        if (x<0) {
          perror("recv");
          throw "Dupa 2";
          }
        x=send(fd[i!=1],buffer,x,0);
        if (x<0) {
          perror("send");
          throw "Dupa s";
          }
        }
      }
    }
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  }
