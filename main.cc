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
#include <pthread.h>
#include <unistd.h>
#include <sys/user.h>
#include <sys/mman.h>
using namespace std;
const int LEN=16384;
//unsigned char buffer[LEN];
void print_hex(unsigned char *data, int len) {
  cout << hex << setfill('0');
  cout << setw(3) << dec << 0 << hex << "  ";
  for (int i=0;i<len;++i) {
    cout << setw(2) << (int) data[i];
    if ((i+1)%16==0) { 
      cout << endl;
      cout << setw(3) << dec << i/16 << hex << "  ";
      }
    else if ((i+1)%8==0) cout << " | ";
    else cout << " ";
    }
  cout << dec << setfill(' ') << endl;
  cout << "--" << endl;
}
bool checksum(unsigned char *etherpacket, size_t length, uint16_t *chk_found, uint16_t *chk_calc) {
  bool test=true;
//ios_base::fmtflags fl=cout.flags();
  if (*reinterpret_cast<uint16_t*>(etherpacket+12)!=8) test=false;
  if ((*(etherpacket+14)&0xf0)!=0x40) test=false;
  int size=(*(etherpacket+14)&0x0f)*2;
  unsigned int chksum=0;
  for (int i=0;i<size;++i) {
    uint16_t item=ntohs(*reinterpret_cast<uint16_t*>(etherpacket+14+i*2));
    if (i==5) { // skip checksum
      *chk_found=item;
      continue;
      }
    chksum+=item;
    chksum+=(chksum>>16);
    chksum=chksum&0x0000ffff;
    }
  chksum=chksum^0x0000ffff;
//cout.flags(fl);
  *chk_calc=chksum;
  return test;
  }
struct thread_data {
int *fds;
int source;
char **argv;
unsigned char ** mmaps_rx;
unsigned int mmap_size;
};
void * thread_forwarder(void * y) {
  sockaddr_ll sckbind;
  socklen_t socklen;
  ssize_t x;
  unsigned char buffer[LEN];
  int *fds=reinterpret_cast<thread_data*>(y)->fds;
  int source=reinterpret_cast<thread_data*>(y)->source;
  char **argv=reinterpret_cast<thread_data*>(y)->argv;
  unsigned char **mmaps_rx=reinterpret_cast<thread_data*>(y)->mmaps_rx;
  unsigned int size=reinterpret_cast<thread_data*>(y)->mmap_size;
  unsigned int max_fr=size/2048;
  cout << "source " << source << endl;
//if (source) sleep(360000);
  pollfd pl;
  pl.fd=fds[source];
  pl.events=POLLIN|POLLRDNORM|POLLERR;
//  receive and print packet
  unsigned int circ_rx=0;
  sockaddr_ll *addr;
  bool oneframe=true;
  for (;;) {
    if (!oneframe) cout << "Did not get any frame" << endl;
    pl.revents=0;
    poll(&pl,1,-1);
    oneframe=false;
//  cout << "Have poll" << endl;
    for (;;circ_rx=(circ_rx+1)%max_fr) {
      unsigned char *frame=mmaps_rx[source]+circ_rx*2048;
      tpacket_hdr *hdr=reinterpret_cast<tpacket_hdr*>(frame);
      if ((hdr->tp_status&TP_STATUS_USER)==TP_STATUS_USER) {
        oneframe=true;
//      if (hdr->tp_status&TP_STATUS_LOSING==TP_STATUS_LOSING) cout << "Losing packets" << endl;
//      cout << "Have packet " << circ_rx << endl;
//      cout << "From " << argv[source+1] << endl;
//      print_hex(frame+hdr->tp_mac,hdr->tp_snaplen);
        if (hdr->tp_snaplen!=hdr->tp_len) throw "truncation?";
        addr=reinterpret_cast<sockaddr_ll*>(frame+TPACKET_ALIGN(sizeof(tpacket_hdr)));
        if (hdr->tp_snaplen>1514) { // 1500 + MAC + MAC + ethtype
          cout << hdr->tp_snaplen << endl;
          throw "Packet bigger than 1500 bytes";
          }
        if (addr->sll_pkttype==PACKET_OUTGOING) {
          hdr->tp_status=TP_STATUS_KERNEL;
          continue;
          }
        x=send(fds[source!=1],frame+hdr->tp_mac,hdr->tp_snaplen,0);
        if (x<0) {
          perror("send");
          cout << "From " << argv[source+1] << endl;
          print_hex(buffer,x);
          throw "Dupa s";
          }
        hdr->tp_status=TP_STATUS_KERNEL;
        }
      else break;
      }
    }
  return 0;
  }
int main(int argc , char * argv[]) try {
  pthread_t thr_id[2];
  int ret,fd[2]={};
  unsigned char * space_rx[2]={};
  if (argc<3) throw "arguments are missing";
  sockaddr_ll sckbind;
  tpacket_req ring_parameters={};
  ring_parameters.tp_block_size=PAGE_SIZE*1024*128;
  ring_parameters.tp_block_nr=4;
  ring_parameters.tp_frame_size=2048;
  ring_parameters.tp_frame_nr=ring_parameters.tp_block_size/ring_parameters.tp_frame_size*ring_parameters.tp_block_nr;

// SETUP LOOP:

  struct ifreq netdevice;
  for (int k=0;k<2;++k) {
//  open sockets
    fd[k]=socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL));
    perror("socket");
    if (fd[k]<0) throw "Dupa 1a";

// assign specific interfaces
    strncpy(netdevice.ifr_name,argv[k+1],IFNAMSIZ);
    ret=ioctl(fd[k],SIOCGIFINDEX,&netdevice);
    perror("ioctl");
    if (ret<0) throw "Dupa 3";
    ret=setsockopt(fd[k],SOL_PACKET,PACKET_RX_RING,&ring_parameters,sizeof(ring_parameters));
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 10";
      }
/*
    long mem=16777216;
    socklen_t lmem=sizeof(mem);
    ret=setsockopt(fd[k],SOL_SOCKET,SO_RCVBUFFORCE,&mem,lmem);
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 8";
      }
    mem=0;
    ret=getsockopt(fd[k],SOL_SOCKET,SO_RCVBUF,&mem,&lmem);
    if (ret<0) {
      perror("getsockopt");
      throw "Dupa 9";
      }
    cout << "Socket buffer: " << mem << " " << lmem << endl;
*/
    space_rx[k]=reinterpret_cast<unsigned char*>(mmap(NULL, ring_parameters.tp_block_size*ring_parameters.tp_block_nr, PROT_READ|PROT_WRITE, MAP_SHARED, fd[k], 0));
    if (space_rx[k]==MAP_FAILED) {
      perror("mmap");
      throw "Dupa 11";
      }
//
    sckbind.sll_family=AF_PACKET;
    sckbind.sll_protocol=0;
    sckbind.sll_ifindex=netdevice.ifr_ifindex;
    ret=bind(fd[k],reinterpret_cast<sockaddr*>(&sckbind),sizeof(sckbind));
    if (ret<0) throw "Dupa 4";
    }

struct thread_data thr_dat[2];
  for (int i=0;i<2;++i) {
    thr_dat[i].fds=fd;
    thr_dat[i].source=i;
    thr_dat[i].argv=argv;
    thr_dat[i].mmaps_rx=space_rx;
    thr_dat[i].mmap_size=ring_parameters.tp_block_size*ring_parameters.tp_block_nr;
    pthread_create(thr_id+i,NULL,thread_forwarder,thr_dat+i);
    }
  for (int i=0;i<2;++i) {
    pthread_join(thr_id[i],NULL);
    }
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  }
