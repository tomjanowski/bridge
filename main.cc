#include <sys/types.h>
#include <sys/socket.h>
#include <iostream>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
using namespace std;
int main(int argc , char * argv[]) try {
  if (argc<2) throw "argument is missing";
  int fd1=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
  perror("socket");
  if (fd1<0) throw "Dupa 1";
  
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  }
