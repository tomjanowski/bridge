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
#include <string.h>
using namespace std;
const int LEN=16384;
//unsigned char buffer[LEN];
void print_hex(unsigned char *data, int len);
bool checksum(unsigned char *etherpacket, size_t length, uint16_t *chk_found, uint16_t *chk_calc);
void * thread_forwarder(void * y);
struct thread_data {
int *fds;
int source;
char **argv;
unsigned char ** mmaps_rx;
unsigned char ** mmaps_tx;
unsigned int mmap_size_rx;
unsigned int mmap_size_tx;
};
const int MEGABYTE=1024*1024;
//  ************************** MAIN ****************************

int main(int argc , char * argv[]) try {
  pthread_t thr_id[2];
  int ret,fd[2]={};
  unsigned char * space_rx[2]={};
  unsigned char * space_tx[2]={};
  if (argc<3) throw "arguments are missing";
  sockaddr_ll sckbind;
  tpacket_req ring_parameters_rx={};
  tpacket_req ring_parameters_tx={};
  ring_parameters_rx.tp_block_size=PAGE_SIZE/2*(1024*256);
  ring_parameters_rx.tp_block_nr=2;
  ring_parameters_rx.tp_frame_size=2048;
  ring_parameters_rx.tp_frame_nr=ring_parameters_rx.tp_block_size/ring_parameters_rx.tp_frame_size*ring_parameters_rx.tp_block_nr;
  ring_parameters_tx.tp_block_size=PAGE_SIZE/2*1024*8;
  ring_parameters_tx.tp_block_nr=1;
  ring_parameters_tx.tp_frame_size=2048;
  ring_parameters_tx.tp_frame_nr=ring_parameters_tx.tp_block_size/ring_parameters_tx.tp_frame_size*ring_parameters_tx.tp_block_nr;

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

//
    sckbind.sll_family=AF_PACKET;
    sckbind.sll_protocol=htons(ETH_P_ALL);
    sckbind.sll_ifindex=netdevice.ifr_ifindex;
    ret=bind(fd[k],reinterpret_cast<sockaddr*>(&sckbind),sizeof(sckbind));
    if (ret<0) throw "Dupa 4";
//
    int ver=TPACKET_V2;
    ret=setsockopt(fd[k],SOL_PACKET,PACKET_VERSION, &ver, sizeof(ver));
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 19";
      }
    ret=setsockopt(fd[k],SOL_PACKET,PACKET_RX_RING,&ring_parameters_rx,sizeof(ring_parameters_rx));
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 10";
      }
    ret=setsockopt(fd[k],SOL_PACKET,PACKET_TX_RING,&ring_parameters_tx,sizeof(ring_parameters_tx));
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 11";
      }
    packet_mreq mreq={
    netdevice.ifr_ifindex,         // int            mr_ifindex
    PACKET_MR_PROMISC,             // unsigned short mr_type
    0,                             // unsigned short mr_alen
    0                              // unsigned char  mr_address[8]
    };
//! ret=setsockopt(fd[k],SOL_PACKET,PACKET_ADD_MEMBERSHIP,&mreq,sizeof(mreq));
//! if (ret<0) throw string("setsockopt");
//! cout << "Set " << argv[k+1] << " to PROMISCUOUS mode" << endl;
    long mem=ring_parameters_tx.tp_block_size*ring_parameters_tx.tp_block_nr/
             ring_parameters_tx.tp_frame_size*1514/10;
    mem=((mem+PAGE_SIZE-1)&(~(PAGE_SIZE-1)));
    mem=((mem<MEGABYTE)?MEGABYTE:mem);
    cout << "Initial send buffer requested " << mem << endl;
    socklen_t lmem=sizeof(mem);
    ret=setsockopt(fd[k],SOL_SOCKET,SO_SNDBUFFORCE,&mem,lmem);
    if (ret<0) {
      perror("setsockopt");
      throw "Dupa 8";
      }
    mem=0;
    ret=getsockopt(fd[k],SOL_SOCKET,SO_SNDBUF,&mem,&lmem);
    if (ret<0) {
      perror("getsockopt");
      throw "Dupa 9";
      }
    cout << "Send buffer set: " << mem << " " << lmem << endl;
    space_rx[k]=reinterpret_cast<unsigned char*>(mmap(NULL,
              ring_parameters_rx.tp_block_size*ring_parameters_rx.tp_block_nr+
              ring_parameters_tx.tp_block_size*ring_parameters_tx.tp_block_nr,
              PROT_READ|PROT_WRITE, MAP_SHARED             , fd[k], 0));
    cout <<(ring_parameters_rx.tp_block_size*ring_parameters_rx.tp_block_nr+
            ring_parameters_tx.tp_block_size*ring_parameters_tx.tp_block_nr)
    << endl;
    if (space_rx[k]==MAP_FAILED) {
      perror("mmap");
      throw "Dupa 12";
      }
    space_tx[k]=space_rx[k]+ring_parameters_rx.tp_block_size*ring_parameters_rx.tp_block_nr;
    }

struct thread_data thr_dat[2];
  for (int i=0;i<2;++i) {
    thr_dat[i].fds=fd;
    thr_dat[i].source=i;
    thr_dat[i].argv=argv;
    thr_dat[i].mmaps_rx=space_rx;
    thr_dat[i].mmaps_tx=space_tx;
    thr_dat[i].mmap_size_rx=ring_parameters_rx.tp_block_size*ring_parameters_rx.tp_block_nr;
    thr_dat[i].mmap_size_tx=ring_parameters_tx.tp_block_size*ring_parameters_tx.tp_block_nr;
    pthread_create(thr_id+i,NULL,thread_forwarder,thr_dat+i);
    }
  for (int i=0;i<2;++i) {
    pthread_join(thr_id[i],NULL);
    }
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  }

//  **************** THREAD FORWARDER *********************************


void * thread_forwarder(void * y) try {
  sockaddr_ll sckbind;
  socklen_t socklen;
  ssize_t x;
  unsigned long immediate=0,delayed=0;
  unsigned char buffer[LEN];
  int *fds=reinterpret_cast<thread_data*>(y)->fds;
  int source=reinterpret_cast<thread_data*>(y)->source;
  char **argv=reinterpret_cast<thread_data*>(y)->argv;
  unsigned char **mmaps_rx=reinterpret_cast<thread_data*>(y)->mmaps_rx;
  unsigned char **mmaps_tx=reinterpret_cast<thread_data*>(y)->mmaps_tx;
  unsigned int size_rx=reinterpret_cast<thread_data*>(y)->mmap_size_rx;
  unsigned int size_tx=reinterpret_cast<thread_data*>(y)->mmap_size_tx;
  unsigned int max_fr_rx=size_rx/2048;
  unsigned int max_fr_tx=size_tx/2048;
  cout << "Thread source " << source << endl;
//if (source) sleep(360000);
  pollfd pl,plwr;
  pl.fd=fds[source];
  plwr.fd=fds[source!=1];
  pl.events=POLLIN|POLLRDNORM|POLLERR;
  plwr.events=POLLOUT;
  int offset=TPACKET_ALIGN(sizeof(tpacket2_hdr));
//  receive and print packet
  unsigned int circ_rx=0;
  unsigned int circ_tx=0;
  sockaddr_ll *addr;
  bool oneframe=true;
  int tosent=0;
  unsigned long tosent_bytes=0;
  long packets=0;
  long sends=0;
  for (;;) {
    if (!oneframe) cout << "Did not get any frame" << endl;
    pl.revents=0;
    if (tosent_bytes) {
      cout << "Error, entering read wait while writes are not finished" << endl;
      throw "Dupa 33";
      }
    poll(&pl,1,-1);
//  cout << "Have poll" << endl;
    for (;;circ_rx=(circ_rx+1)%max_fr_rx,circ_tx=(circ_tx+1)%max_fr_tx) {
      unsigned char *frame=mmaps_rx[source]+circ_rx*2048;
      tpacket2_hdr *hdr=reinterpret_cast<tpacket2_hdr*>(frame);
      if ((hdr->tp_status&TP_STATUS_USER)==TP_STATUS_USER) {
//      if (hdr->tp_status&TP_STATUS_LOSING==TP_STATUS_LOSING) cout << "Losing packets" << endl;
//      cout << "Have packet " << circ_rx << endl;
//      cout << "From " << argv[source+1] << endl;
//      print_hex(frame+hdr->tp_mac,hdr->tp_snaplen);
        if (hdr->tp_snaplen!=hdr->tp_len) throw "truncation?";
        addr=reinterpret_cast<sockaddr_ll*>(frame+TPACKET_ALIGN(sizeof(tpacket2_hdr)));
        if (hdr->tp_snaplen>1514) { // 1500 + MAC + MAC + ethtype
          cout << hdr->tp_snaplen << endl;
          throw "Packet bigger than 1500 bytes";
          }
        if (addr->sll_pkttype==PACKET_OUTGOING) {
          hdr->tp_status=TP_STATUS_KERNEL;
          circ_tx=(circ_tx-1+max_fr_tx)%max_fr_tx;
          continue;
          }
        unsigned char *dst_frame=mmaps_tx[source!=1]+circ_tx*2048;
        tpacket2_hdr *dst_hdr=reinterpret_cast<tpacket2_hdr*>(dst_frame);
        for (int kk=0;;++kk) {
          if ((dst_hdr->tp_status&0b111)!=TP_STATUS_AVAILABLE) {
            if (kk>10000) {
              cout << circ_tx << "x: " << hex << dst_hdr->tp_status << dec << endl;
              throw "Dupa 15";
              }
            else {
              cout << "TX RING buffer full " << kk << endl;
//
              if (tosent) {
                ++sends;
                x=send(fds[source!=1],NULL,0,0);
                tosent=0;
                if (x>0) tosent_bytes-=x;
//              if (tosent_bytes)
//                cout << "Bytes left " << tosent_bytes << endl;
                if (x<0) {
                  perror("send");
                  cout << "From " << argv[source+1] << endl;
                  cout << errno << endl;
                  print_hex(buffer,x);
                  if (errno==ENETDOWN) {
                    sleep(1);
                    continue;
                    }
                  throw "Dupa s1";
                  }
                }
//
              poll(&plwr,1,-1);
              cout << "Poll plwr revents: " << plwr.revents << endl;
              }
            }
          else break;
          }
/*
        if (packets%40000==0) {
          if (source) cout << "\x1b[4B";
          if (!source) cout << "\x1b[6B";
          cout << source << "        " << packets << " " << sends << " " << packets*1.0/sends << "             \r" << flush;
          if (source) cout << "\x1b[4F";
          if (!source) cout << "\x1b[6F";
          }  */
        dst_hdr->tp_len=hdr->tp_len;
        memcpy(dst_frame+offset,frame+hdr->tp_mac,hdr->tp_len);
        dst_hdr->tp_status=TP_STATUS_SEND_REQUEST;
        hdr->tp_status=TP_STATUS_KERNEL;
        ++tosent;
        tosent_bytes+=hdr->tp_len;
        ++packets;
        if (tosent_bytes && tosent%16==0) {
          ++delayed;
          ++sends;
          x=send(fds[source!=1],NULL,0,MSG_DONTWAIT);
          tosent=0;
          if (x>0) tosent_bytes-=x;
//        if (tosent_bytes)
//          cout << "Bytes left " << tosent_bytes << endl;
          if (x<0) {
            if (errno==EAGAIN) {
              continue;
              }
            perror("send");
            cout << "From " << argv[source+1] << endl;
            cout << errno << endl;
            print_hex(buffer,x);
            if (errno==ENETDOWN) {
              sleep(1);
              continue;
              }
            throw "Dupa s2";
            }
          }
        }
      else {
netdown:
        if (tosent_bytes) {
          ++immediate;
          ++sends;
          x=send(fds[source!=1],NULL,0,0);
          tosent=0;
          if (x>0) tosent_bytes-=x;
//        if (tosent_bytes)
//          cout << "Bytes left " << tosent_bytes << endl;
          if (x<0) {
            if (errno!=EAGAIN) {
              perror("send");
              cout << "From " << argv[source+1] << endl;
              cout << errno << endl;
              print_hex(buffer,x);
              if (errno==ENETDOWN) {
                sleep(2);
                cout << "Waiting for network to be up in nonblocking send()" << endl;
                goto netdown;
                }
              throw "Dupa s3";
              }
            else {
              cout << "EAGAIN" << endl;
              throw "Dupa 53";
              }
            }
          }
        break;
        }
      }
    }
  return 0;
  } catch (const char * x) {
  cout << "Error catched: \"" << x << "\"" <<  endl;
  exit(1);
  return (void*)-1;
  }

//  END OF   ******* THREAD FORWARDER *********************************

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
//********************************************
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
