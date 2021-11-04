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
using namespace std;
const int LEN=2048;
unsigned char buffer[LEN];
struct ifreq netdevice;
int main(int argc , char * argv[]) try {
  int ret;
  if (argc<2) throw "argument is missing";
  strncpy(netdevice.ifr_name,argv[1],IFNAMSIZ);
  int fd1=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
  perror("socket");
  if (fd1<0) throw "Dupa 1";
  ret=ioctl(fd1,SIOCGIFINDEX,&netdevice);
  perror("ioctl");
  if (ret<0) throw "Dupa 3";
  sockaddr_ll sckbind;
  sckbind.sll_family=AF_PACKET;
  sckbind.sll_protocol=0;
  sckbind.sll_ifindex=netdevice.ifr_ifindex;
  int bret=bind(fd1,reinterpret_cast<sockaddr*>(&sckbind),sizeof(sckbind));
  ssize_t x=recv(fd1,buffer,LEN,0);
  if (x<0) throw "Dupa 2";
  for (int i=0;i<x;++i) {
    cout << hex << setw(2) << setfill('0') << (int) buffer[i];
    if ((i+1)%16==0) cout << endl;
    else cout << " ";
    }
  cout << dec << endl;
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  }
