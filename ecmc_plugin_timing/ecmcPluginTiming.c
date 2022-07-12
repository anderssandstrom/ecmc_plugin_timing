/*************************************************************************\
* Copyright (c) 2019 European Spallation Source ERIC
* ecmc is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
*
*  ecmcPluginExample.cpp
*
*  Created on: Mar 21, 2020
*      Author: anderssandstrom
*      Credits to  https://github.com/sgreg/dynamic-loading 
*
\*************************************************************************/

// Needed to get headers in ecmc right...
#define ECMC_IS_PLUGIN
#define ECMC_EXAMPLE_PLUGIN_VERSION 2

#ifdef __cplusplus
extern "C" {
#endif  // ifdef __cplusplus

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>


#include "ecmcPluginDefs.h"
#include "ecmcPluginClient.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


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

#define ECMC_PLUGIN_DBG_PRINT_OPTION_CMD "DBG="

static int    lastEcmcError   = 0;
static char*  lastConfStr     = NULL;
static int    alreadyLoaded   = 0;
int initDone = 0;


// timing related vars
int sock;
int destination_port = 1234;
struct in_addr sourceIP;
int usehw = 1;
int type=0;
const char ifname[256] = "eno1\n";

void die(char* s)
{
    perror(s);
    //exit(1);
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

/** Optional. 
 *  Will be called once after successfull load into ecmc.
 *  Return value other than 0 will be considered error.
 *  configStr can be used for configuration parameters.
 **/
int timingConstruct(char *configStr)
{  

  // only allow one loaded module
  if(alreadyLoaded) {    
    return 1;
  }
  alreadyLoaded = 1;
  
  lastConfStr = strdup(configStr);

  return 0;
}

/** Optional function.
 *  Will be called once at unload.
 **/
void timingDestruct(void)
{  
  close(sock);

  if(lastConfStr){
    free(lastConfStr);
  }
}

/** Optional function.
 *  Will be called each realtime cycle if definded
 *  ecmcError: Error code of ecmc. Makes it posible for 
 *  this plugin to react on ecmc errors
 *  Return value other than 0 will be considered to be an error code in ecmc.
 **/
int timingRealtime(int ecmcError)
{  
  lastEcmcError = ecmcError;

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
          if(usehw) {
            timespec2str(&timestamp[0],35, &hw_ts[2]);
            fprintf(stdout,"HW: %lu s, %lu ns (%s)\n",hw_ts[2].tv_sec,hw_ts[2].tv_nsec,timestamp);
          } else {
            timespec2str(&timestamp[0],35, &hw_ts[0]);
            fprintf(stdout,"SW: %lu s, %lu ns (%s)\n",hw_ts[0].tv_sec,hw_ts[0].tv_nsec,timestamp);
          }
          //fprintf(stdout,"ts[1] - ???: %lu s, %lu ns\n",hw_ts[1].tv_sec,hw_ts[1].tv_nsec);
      }
  }

  return 0;
}

/** Link to data source here since all sources should be availabe at this stage
 *  (for example ecmc PLC variables are defined only at enter of realtime)
 **/
int timingEnterRT(){

  fprintf(stdout,"Program started.\n");
  
  // Create socket
  if ((sock = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL))) < 0) {
      die("RAW socket()");
      return 3;
  }
  struct ifreq ifindexreq;
  struct sockaddr_ll si_server;
  int ifindex=-1;

  // Get interface index
  strncpy(ifindexreq.ifr_name,ifname,IFNAMSIZ);
  if(ioctl(sock,SIOCGIFINDEX,&ifindexreq)!=-1) {
    ifindex=ifindexreq.ifr_ifindex;
  } else {
    die("SIOCGIFINDEX ioctl()");
    return 4;
  }
  
  memset(&si_server, 0, sizeof(si_server));
  si_server.sll_ifindex=ifindex;
  si_server.sll_family=PF_PACKET;
  si_server.sll_protocol=htons(ETH_P_ALL);
 // bind() to interface
  if(bind(sock,(struct sockaddr *) &si_server,sizeof(si_server))<0) {
    die("bind()");
    return 5;
  }
  // end of main...

  //now start     run_test(argc,argv,usehw,sock,(void *)&si_server);
  //struct sockaddr_ll si_server=*(struct sockaddr_ll *) si_server_ptr;
  fprintf(stdout,"Test started.\n");
  
  int flags;
  
  if(usehw) {
    struct ifreq hwtstamp;
    struct hwtstamp_config hwconfig;
    // Set hardware timestamping
    memset(&hwtstamp,0,sizeof(hwtstamp));
    memset(&hwconfig,0,sizeof(hwconfig));
    // Set ifr_name and ifr_data (see: man7.org/linux/man-pages/man7/netdevice.7.html)
    strncpy(hwtstamp.ifr_name,ifname,sizeof(hwtstamp.ifr_name));
    hwtstamp.ifr_data=(void *)&hwconfig;
    hwconfig.tx_type=HWTSTAMP_TX_ON;
    hwconfig.rx_filter=HWTSTAMP_FILTER_ALL;
    // Issue request to the driver
    if (ioctl(sock,SIOCSHWTSTAMP,&hwtstamp)<0) {
      die("ioctl()");
      return 6;
    }
    flags=SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE;
    type = SO_TIMESTAMPING;
    if(setsockopt(sock,SOL_SOCKET,SO_TIMESTAMPING,&flags,sizeof(flags))<0) {
      die("setsockopt()");
      return 7;
    }
  } else {
    flags=SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE;// | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE;
    type = SCM_TIMESTAMPNS;       
    if(setsockopt(sock,SOL_SOCKET,SO_TIMESTAMPNS,&flags,sizeof(flags))<0) {
      die("setsockopt()");
      return 8;
    }
  }

  return 0;
}

/** Optional function.
 *  Will be called once just before leaving realtime mode
 *  Return value other than 0 will be considered error.
 **/
int timingExitRT(void){
  return 0;
}

// Register data for plugin so ecmc know what to use
struct ecmcPluginData pluginDataDef = {
  // Allways use ECMC_PLUG_VERSION_MAGIC
  .ifVersion = ECMC_PLUG_VERSION_MAGIC, 
  // Name 
  .name = "ecmcPluginTiming",
  // Description
  .desc = "timing plugin for use with ecmc.",
  // Option description
  .optionDesc = "\n    "ECMC_PLUGIN_DBG_PRINT_OPTION_CMD"<1/0>    : Enables/disables printouts from plugin, default = disabled (=0).\n",
  // Plugin version
  .version = ECMC_EXAMPLE_PLUGIN_VERSION,
  // Optional construct func, called once at load. NULL if not definded.
  .constructFnc = timingConstruct,
  // Optional destruct func, called once at unload. NULL if not definded.
  .destructFnc = timingDestruct,
  // Optional func that will be called each rt cycle. NULL if not definded.
  .realtimeFnc = timingRealtime,
  // Optional func that will be called once just before enter realtime mode
  .realtimeEnterFnc = timingEnterRT,
  // Optional func that will be called once just before exit realtime mode
  .realtimeExitFnc = timingExitRT,
  // PLC funcs
  .funcs[0] = {0},  // last element set all to zero..
  // PLC consts
  .consts[0] = {0}, // last element set all to zero..
};

ecmc_plugin_register(pluginDataDef);

# ifdef __cplusplus
}
# endif  // ifdef __cplusplus
