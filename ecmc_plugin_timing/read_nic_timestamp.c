#include <arpa/inet.h>
#include <linux/net_tstamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
//#include <linux/errqueue.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
//#include <net/if.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <linux/if.h>

#define RAW_SOCKET 1 // Set to 0 to use an UDP socket, set to 1 to use raw socket

#if RAW_SOCKET
#include <linux/if_packet.h>
#include <net/ethernet.h>
#endif

void die(char* s)
{
    perror(s);
    //exit(1);
}

static inline void timespec_diff(struct timespec *a, struct timespec *b,
    struct timespec *result) {
    result->tv_sec  = a->tv_sec  - b->tv_sec;
    result->tv_nsec = a->tv_nsec - b->tv_nsec;
    if (result->tv_nsec < 0) {
        --result->tv_sec;
        result->tv_nsec += 1000000000L;
    }
}

int timespec2str(char *buf, uint len, struct timespec *ts) {
    buf[0]=0;
    int ret;
    struct tm t;

    tzset();
    if (localtime_r(&(ts->tv_sec), &t) == NULL)
        return 1;

    ret = strftime(buf, len, "%F %T", &t);
    if (ret == 0)
        return 2;
    len -= ret - 1;

    ret = snprintf(&buf[strlen(buf)], len, ".%09ld", ts->tv_nsec);
    if (ret >= len)
        return 3;

    return 0;
}

// Wait for data to be available on the socket error queue, as detailed in https://www.kernel.org/doc/Documentation/networking/timestamping.txt
int pollErrqueueWait(int sock,uint64_t timeout_ms) {
    struct pollfd errqueueMon;
    int poll_retval;

    errqueueMon.fd=sock;
    errqueueMon.revents=0;
    errqueueMon.events=0;

    while((poll_retval=poll(&errqueueMon,1,timeout_ms))>0 && errqueueMon.revents!=POLLERR);

    return poll_retval;
}

int run_test(int argc, char* argv[], int hw_stamps, int sock, void *si_server_ptr)
{
    #if RAW_SOCKET
        struct sockaddr_ll si_server=*(struct sockaddr_ll *) si_server_ptr;
    #else
        struct sockaddr_in si_server=*(struct sockaddr_in *) si_server_ptr;
    #endif
    fprintf(stdout,"Test started.\n");

    int flags;
    int type=0;
    if(hw_stamps) {
        struct ifreq hwtstamp;
        struct hwtstamp_config hwconfig;

        // Set hardware timestamping
        memset(&hwtstamp,0,sizeof(hwtstamp));
        memset(&hwconfig,0,sizeof(hwconfig));

        // Set ifr_name and ifr_data (see: man7.org/linux/man-pages/man7/netdevice.7.html)
        strncpy(hwtstamp.ifr_name,argv[1],sizeof(hwtstamp.ifr_name));
        hwtstamp.ifr_data=(void *)&hwconfig;

        hwconfig.tx_type=HWTSTAMP_TX_ON;
        hwconfig.rx_filter=HWTSTAMP_FILTER_ALL;

        // Issue request to the driver
        if (ioctl(sock,SIOCSHWTSTAMP,&hwtstamp)<0) {
            die("ioctl()");
        }

        flags=SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
        type = SO_TIMESTAMPING;
        if(setsockopt(sock,SOL_SOCKET,SO_TIMESTAMPING,&flags,sizeof(flags))<0) {
          die("setsockopt()");
        }
    } else {
       flags=SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE;// | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE;
       type = SCM_TIMESTAMPNS;       
        if(setsockopt(sock,SOL_SOCKET,SO_TIMESTAMPNS,&flags,sizeof(flags))<0) {
          die("setsockopt()");
        }
    }


    const int buffer_len = 256;
    char buffer[buffer_len];

    // Send 10 packets
    const int n_packets = 100000;
    int i=0;
    //for (i = 0; i < n_packets; ++i) {
    while(1) {
        //sprintf(buffer, "Packet %d", i);
        //if (sendto(sock, buffer, buffer_len, 0, (struct sockaddr*) &si_server, sizeof(si_server)) < 0) {
        //    die("sendto()");
        //}

        //fprintf(stdout,"Sent packet number %d/%d\n",i,n_packets);
        fflush(stdout);

        // Obtain the sent packet timestamp.
        char data[256];
        struct msghdr msg;
        struct iovec entry;
        char ctrlBuf[CMSG_SPACE(sizeof(struct timespec)*3)];

        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &entry;
        msg.msg_iovlen = 1;
        entry.iov_base = data;
        entry.iov_len = sizeof(data);
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_control = &ctrlBuf;
        msg.msg_controllen = sizeof(ctrlBuf);
        // Wait for data to be available on the error queue
        //printf("Wait for data\n");
        //pollErrqueueWait(sock,-1); // -1 = no timeout is set
        //if (recvmsg(sock, &msg, MSG_ERRQUEUE) < 0) {
        if (recvmsg(sock, &msg, MSG_DONTWAIT) < 0) {
            //die("recvmsg()");
        }
        //printf("After wait for data\n");

        // Extract and print ancillary data (SW or HW tx timestamps)
        struct cmsghdr *cmsg = NULL;
        struct timespec *hw_ts;
        char timestamp[35];
        timestamp[0]='\0';

        for(cmsg=CMSG_FIRSTHDR(&msg);cmsg!=NULL;cmsg=CMSG_NXTHDR(&msg, cmsg)) {            
            if(cmsg->cmsg_level==SOL_SOCKET && cmsg->cmsg_type==type) {
                hw_ts=((struct timespec *)CMSG_DATA(cmsg));
                if(hw_stamps) {
                  timespec2str(&timestamp[0],35, &hw_ts[2]);
                  fprintf(stdout,"HW: %lu s, %lu ns (%s)\n",hw_ts[2].tv_sec,hw_ts[2].tv_nsec,timestamp);
                } else {
                  timespec2str(&timestamp[0],35, &hw_ts[0]);
                  fprintf(stdout,"SW: %lu s, %lu ns (%s)\n",hw_ts[0].tv_sec,hw_ts[0].tv_nsec,timestamp);
                }
                //fprintf(stdout,"ts[1] - ???: %lu s, %lu ns\n",hw_ts[1].tv_sec,hw_ts[1].tv_nsec);
            }
        }

        // if(hw_stamps) {
        //   if (recvmsg(sock, &msg, MSG_ERRQUEUE) < 0) {
        //       continue;
        //   }
        //   for(cmsg=CMSG_FIRSTHDR(&msg);cmsg!=NULL;cmsg=CMSG_NXTHDR(&msg, cmsg)) {            
        //     if(cmsg->cmsg_level==SOL_SOCKET && cmsg->cmsg_type==SCM_TIMESTAMPING) {
        //         hw_ts=((struct timespec *)CMSG_DATA(cmsg));
        //         if(hw_stamps) {
        //           timespec2str(&timestamp[0],35, &hw_ts[2]);
        //           fprintf(stdout,"HW TX: %lu s, %lu ns (%s)\n",hw_ts[2].tv_sec,hw_ts[2].tv_nsec,timestamp);
        //         }
        //     }
        //   }
        // }

        // Wait 1s before sending next packet
        usleep(100);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    int sock;
    char* destination_ip = "192.168.1.211";
    int destination_port = 1234;
    struct in_addr sourceIP;
    int usehw=0;
    fprintf(stdout,"Program started.\n");

    if(argc!=3) {
        fprintf(stderr,"Error. readNicTs <if> <hw_clock 1/0>.\n");
        exit(1);
    }
    
    usehw=atoi(argv[2]);
    // Create socket
    #if RAW_SOCKET
//        if ((sock = socket(AF_PACKET,SOCK_RAW,htons(ETH_P_ALL))) < 0) {
//            printf("BAD\n");
//            die("RAW socket()");
//        }
        if ((sock = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL))) < 0) {
            printf("BAD 1\n");
            die("RAW socket()");
        }
    #else
        if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
            die("UDP socket()");
        }
    #endif

    struct ifreq ifindexreq;
    #if RAW_SOCKET
        struct sockaddr_ll si_server;
        int ifindex=-1;

        // Get interface index
        strncpy(ifindexreq.ifr_name,argv[1],IFNAMSIZ);
        if(ioctl(sock,SIOCGIFINDEX,&ifindexreq)!=-1) {
                ifindex=ifindexreq.ifr_ifindex;
        } else {
            die("SIOCGIFINDEX ioctl()");
        }

        memset(&si_server, 0, sizeof(si_server));
        si_server.sll_ifindex=ifindex;
        si_server.sll_family=PF_PACKET;
        si_server.sll_protocol=htons(ETH_P_ALL);
    #else
        struct sockaddr_in si_server;

        // Get source IP address
        strncpy(ifindexreq.ifr_name,argv[1],IFNAMSIZ);
        ifindexreq.ifr_addr.sa_family = AF_INET;
        if(ioctl(sock,SIOCGIFADDR,&ifindexreq)!=-1) {
            sourceIP=((struct sockaddr_in*)&ifindexreq.ifr_addr)->sin_addr;
        } else {
            die("SIOCGIFADDR ioctl()");
        }

        bzero(&si_server,sizeof(si_server));
        si_server.sin_family = AF_INET;
        si_server.sin_port = htons(destination_port);
        si_server.sin_addr.s_addr = sourceIP.s_addr;
        fprintf(stdout,"source IP: %s\n",inet_ntoa(sourceIP));
    #endif

    // bind() to interface
    if(bind(sock,(struct sockaddr *) &si_server,sizeof(si_server))<0) {
        die("bind()");
    }

    #if !RAW_SOCKET
        // Set destination IP (re-using si_server)
        if (inet_aton(destination_ip, &si_server.sin_addr) == 0) {
            die("inet_aton()");
        }
    #endif
    int i=0;

    run_test(argc,argv,usehw,sock,(void *)&si_server);

    close(sock);

    return 0;
}
