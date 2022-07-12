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

#define ECMC_PLUGIN_DBG_PRINT_OPTION_CMD "DBG="

static int    lastEcmcError   = 0;
static char*  lastConfStr     = NULL;
static int    alreadyLoaded   = 0;
int initDone = 0;
pthread_t tid;

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

  // create timing object and register data callback
  lastConfStr = strdup(configStr);

  return 0;
}

/** Optional function.
 *  Will be called once at unload.
 **/
void timingDestruct(void)
{  
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
  return 0;
}

/** Link to data source here since all sources should be availabe at this stage
 *  (for example ecmc PLC variables are defined only at enter of realtime)
 **/
int timingEnterRT(){
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
