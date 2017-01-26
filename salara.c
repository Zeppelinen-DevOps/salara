#include <ctype.h>
#include <netdb.h>
#include <jansson.h>
#include <curl/curl.h>

#include <endian.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <syslog.h>

#include <asterisk.h>
#include "asterisk/ast_version.h"
#include <asterisk/module.h>
#include <asterisk/cli.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/manager.h>
#include <asterisk/paths.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/app.h>
#include <asterisk/threadstorage.h>
#include <asterisk/test.h>
#include <asterisk/tcptls.h>
//#include "asterisk/format_cache.h"

#undef DO_SSL
#define ver13

#define TIME_STR_LEN 128
#define AST_MODULE "salara"
#define AST_MODULE_DESC "Features: transfer call; make call; get status: exten.,peer,channel; send: command,message,post"
#define DEF_DEST_NUMBER "1234"
#define SALARA_VERSION "3.1"//20.01.2017
//"3.0"//26.12.2016
//"2.9"//23.12.2016
//"2.8"//22.12.2016
//"2.7"//21.12.2016
//"2.6"//20.12.2016
//"2.5"//18.12.2016
//"2.4"//17.12.2016
//"2.3"//16.12.2016
//"2.2"//14.12.2016
//"2.1"//12.12.2016
//"2.0"//10.12.2016
//"1.9"//09.12.2016
//"1.8"//09.12.2016
//"1.7"//08.12.2016
//"1.6"//07.12.2016

#define DEF_SALARA_CURLOPT_TIMEOUT 3
#define MAX_ANSWER_LEN 1024
#define CMD_BUF_LEN 512
#define MAX_STATUS 8
#define MAX_ACT_TYPE 6
#define DEFAULT_SRV_ADDR "127.0.0.1:5058"	/* Default address & port for Salara management via TCP */
#define SIZE_OF_RESP 64
#define max_param_rest 7
#define max_buf_size 4096	//2048
#define MAX_CHAN_STATE 11
#define MAX_EVENT_NAME 5
#define MAX_EVENT_TYPE 4
#define max_param 3
//------------------------------------------------------------------------

ASTERISK_FILE_VERSION(__FILE__, "$Revision: $");

//------------------------------------------------------------------------
struct MemoryStruct {
  char *memory;
  size_t size;
};
//------------------  send chan_event by hangup  -----------------------
typedef struct {
    int type;//0-HookResponse, 1-Hangup, 2-Newchannel, 3-Newexten, 4-AgentConnect
    char *chan;//[AST_CHANNEL_NAME];
    char *exten;//[AST_MAX_EXTENSION];
    char *caller;//[AST_MAX_EXTENSION];
    char *app;//[AST_MAX_EXTENSION];
    int state;//status
} s_chan_event;
//                     chan_event list
typedef struct self_evt {
    struct self_evt * before;
    struct self_evt * next;
    s_chan_event *event;
} s_event_list;

typedef struct {
    struct self_evt * first;
    struct self_evt * end;
    unsigned int counter;
} s_event_hdr;
//------------------  for List of channel_name  -----------------------
typedef struct self {
    struct self * before;
    struct self * next;
    char *chan;//[AST_CHANNEL_NAME];
    char *exten;//[AST_MAX_EXTENSION];
    char *caller;//[AST_MAX_EXTENSION];
    void *ast;
    unsigned char update;//0-update alow, !=0 - update deny
} s_chan_record;

typedef struct {
    struct self * first;
    struct self * end;
    unsigned int counter;
} s_chan_hdr;

//-----------------------------------------------------------
typedef struct self_rec {
    struct self_rec * before;
    struct self_rec * next;
    char caller[AST_MAX_EXTENSION];
    char called[AST_MAX_EXTENSION];
} s_route_record;

typedef struct {
    struct self_rec * first;
    struct self_rec * end;
    unsigned int counter;
} s_route_hdr;

//------------------  for MakeAction  -----------------------
typedef struct {
    unsigned int id;
    int status;
    char resp[SIZE_OF_RESP];
} s_act;

typedef struct act_rec {
    struct act_rec * before;
    struct act_rec * next;
    s_act *act;
} s_act_list;

typedef struct {
    struct act_rec *first;
    struct act_rec *end;
    unsigned int counter;
} s_act_hdr;
//------------------------------------------------------------------------
static s_event_hdr event_hdr = {NULL,NULL,0};
static s_chan_hdr chan_hdr = {NULL,NULL,0};
static s_act_hdr act_hdr = {NULL,NULL,0};
static char hook_tmp_str[max_buf_size]={0};
static int reload=0;
static int unload=0;
static int watch_makecall = 0;
static unsigned char newchannel = 0;
static unsigned char hangup = 0;
static unsigned char newexten = 0;
static unsigned char console = 0;
static unsigned char dirty=0;//clear lost chan_records
static unsigned char start_http_nitka = 0;
static unsigned char stop_http_nitka = 0;
static pthread_t http_tid;
static pthread_attr_t threadAttr;

static int salara_atexit_registered = 0;
static int salara_cli_registered = 0;
static int salara_manager_registered = 0;
static int salara_app_registered = 0;

static char *app_salara = "salara";
static char *app_salara_synopsys = "Route++ salara";
static char *app_salara_description = "Salara REST functions";

static char dest_number[AST_MAX_EXTENSION] = "00";

static char dest_url[PATH_MAX] = "https://Alarm:3000/call_center/incoming_call/check_is_org_only?phone=";
static char dest_url_event[PATH_MAX] = "https://Alarm:3000/post";

static char rest_server[PATH_MAX] = {0};

static char key_word[PATH_MAX] = {0};
static const char *def_key_word = "personal_manager_internal_phone";

static char names_rest[max_param_rest][PATH_MAX] = {"","","","","","",""};
static const char *def_names_rest[max_param_rest] = {"operator", "phone", "msg", "extension", "peer", "channel", "context"};

static char context[PATH_MAX] = "alnik";
static int good_status[MAX_STATUS] = {0};

static int SALARA_CURLOPT_TIMEOUT = DEF_SALARA_CURLOPT_TIMEOUT;

static char time_str[TIME_STR_LEN]={0};

static struct timeval salara_start_time = {0, 0};

static const char *salara_config_file = "salara.conf";
static int salara_config_file_len = 0;

static int salara_verbose  = 0;//0 - off, 1 - on, 2 - debug, 3 - dump

static unsigned int Act_ID = 0;

static s_route_hdr route_hdr = {NULL,NULL,0};

static unsigned int srv_time_cnt=0;

static char Tech[] = "sip";

static char *ActType[MAX_ACT_TYPE] = {"Make call","Send message", "Get status peer", "Get status channel", "Get status exten", "Unknown"};

static const char *S_ActionID = "ActionID:";
static const char *S_Status = "\nStatus:";
static const char *S_Response = "Response:";
static const char *S_ResponseF = "Response: Follows";
static const char *S_State = "State:";
static char S_ChanOff[] = "Channel not present\n";
static const char *S_StatusText ="StatusText:";
static const char *S_Success = "Success";
static const char *S_PeerStatus = "PeerStatus:";
static const char *S_Channel = "Channel:";
static const char *S_Exten = "Exten:";
static const char *S_Extension = "Extension:";
static const char *S_CallerIDNum = "CallerIDNum:";
static const char *S_ChannelState = "ChannelState:";
static const char *S_Application ="Application:";

static const char *ChanStateName[MAX_CHAN_STATE] = {
"DOWN",			/*!< Channel is down and available */
"RESERVED",		/*!< Channel is down, but reserved */
"OFFHOOK",		/*!< Channel is off hook */
"DIALING",		/*!< Digits (or equivalent) have been dialed */
"RING",			/*!< Line is ringing */
"RINGING",		/*!< Remote end is ringing */
"UP",			/*!< Line is up */
"BUSY",			/*!< Line is busy */
"DIALING_OFFHOOK",	/*!< Digits (or equivalent) have been dialed while offhook */
"PRERING",		/*!< Channel has detected an incoming call and is waiting for ring */
"UNKNOWN"};

static const char *EventName[MAX_EVENT_NAME] = {
"HookResponse", "Hangup", "Newchannel", "Newexten", "AgentConnect"};

AST_MUTEX_DEFINE_STATIC(salara_lock);//global mutex

AST_MUTEX_DEFINE_STATIC(resp_event_lock);//event_list mutex

AST_MUTEX_DEFINE_STATIC(route_lock);//route_table mutex

AST_MUTEX_DEFINE_STATIC(act_lock);//actionID mutex

AST_MUTEX_DEFINE_STATIC(status_lock);//actionID mutex

AST_MUTEX_DEFINE_STATIC(chan_lock);//chan_list mutex

AST_MUTEX_DEFINE_STATIC(event_lock);//event_list mutex

/*********************************************************************************
unsigned int get_timer_sec(unsigned int t)
{
    return ((unsigned int)time(NULL) + t);
}
int check_delay_sec(unsigned int t)
{
    if ((unsigned int)time(NULL) >= t)  return 1; else return 0;
}
*********************************************************************************/
static char *TimeNowPrn();
static void remove_chan_records();
static void init_chan_records();
static s_chan_record *add_chan_record(const char *nchan, const char *caller, const char *ext, void *data);
static int del_chan_record(s_chan_record *rcd, int withlock);
static s_chan_record *update_chan(const char *nchan, const char *caller, const char *ext);
static s_chan_record *find_chan(const char *nchan, const char *caller, const char *ext, int with_del);
static void *send_by_event(void *arg);
//------------------------------------------------------------------------
inline static s_chan_event *make_chan_event(int type, const char *nchan, const char *caller, const char *exten, int stat, const char *app)
{
s_chan_event *ret=NULL, *evt=NULL;
char *tmp_chan=NULL, *tmp_caller=NULL, *tmp_exten=NULL, *tmp_app=NULL;
int err=1, lg = salara_verbose;

    if ((!nchan) || (!caller) || (!exten) || (!app)) return ret;

    evt = (s_chan_event *)calloc(1, sizeof(s_chan_event));
    if (evt) {
	evt->type = type;
	evt->chan = NULL;
	evt->exten = NULL;
	evt->caller = NULL;
	evt->app = NULL;
	evt->state = stat;
	tmp_chan = (char *)calloc(1,strlen(nchan)+1);
	if (tmp_chan) {
	    memcpy(tmp_chan, nchan, strlen(nchan));
	    evt->chan = tmp_chan;
	    tmp_exten = (char *)calloc(1,strlen(exten)+1);
	    if (tmp_exten) {
		memcpy(tmp_exten, exten, strlen(exten));
		evt->exten = tmp_exten;
		tmp_caller = (char *)calloc(1,strlen(caller)+1);
		if (tmp_caller) {
		    memcpy(tmp_caller, caller, strlen(caller));
		    evt->caller = tmp_caller;
		    tmp_app = (char *)calloc(1, strlen(app)+1);
		    if (tmp_app) {
			memcpy(tmp_app, app, strlen(app));
			evt->app = tmp_app;
			err=0;
		    }
		}
	    }
	}
	if (!err) ret = evt;
	else {
	    if (evt->chan) free(evt->chan);
	    if (evt->exten) free(evt->exten);
	    if (evt->caller) free(evt->caller);
	    if (evt->app) free(evt->app);
	}
    }

    if (ret)
	if (lg>2) ast_verbose("[%s %s] make_chan_event : ret=%p\n", AST_MODULE, TimeNowPrn(), (void *)ret);

    return ret;

}
//------------------------------------------------------------------------
static int rm_chan_event(s_chan_event *evt)
{
int lg = salara_verbose;

    if (!evt) return 0;

    if (evt->chan) free(evt->chan);
    if (evt->exten) free(evt->exten);
    if (evt->caller) free(evt->caller);
    if (evt->app) free(evt->app);

    free(evt);

    if (lg>2) ast_verbose("[%s %s] rm_chan_event : ret=%p\n", AST_MODULE, TimeNowPrn(), (void *)evt);

    return 0;

}
//------------------------------------------------------------------------
static s_event_list *add_event_list(s_chan_event *evt)
{
s_event_list *ret=NULL, *rec=NULL, *tmp=NULL;

    if (!evt) return ret;

    int lg = salara_verbose;

    ast_mutex_lock(&event_lock);

	rec = (s_event_list *)calloc(1,sizeof(s_event_list));
	if (rec) {
	    rec->event = evt;
	    rec->before = rec->next = NULL;
	    if (event_hdr.first == NULL) {//first record
		event_hdr.first = event_hdr.end = rec;
	    } else {//add to tail
		tmp = event_hdr.end;
		rec->before = tmp;
		event_hdr.end = rec;
		tmp->next = rec;
	    }
	    event_hdr.counter++;
	    ret=rec;
	}

	if (lg>2) {
	    if (ret) ast_verbose("[%s %s] add_event_list (%d) : rec=%p before=%p next=%p\n\t-- type(%d)='%s' chan='%s' exten='%s' caller='%s' app='%s' state=%d\n",
			AST_MODULE, TimeNowPrn(),
			event_hdr.counter, (void *)ret,
			(void *)ret->before,
			(void *)ret->next,
			ret->event->type,
			ChanStateName[ret->event->type],
			ret->event->chan,
			ret->event->exten,
			ret->event->caller,
			ret->event->app,
			ret->event->state);
	    else ast_verbose("[%s %s] add_event_list (%d) : Error -> rec=%p\n", AST_MODULE, TimeNowPrn(), event_hdr.counter, (void *)ret);
	}

    ast_mutex_unlock(&event_lock);

    return ret;
}
//------------------------------------------------------------------------
static int del_event_list(s_event_list *rcd, int withlock)
{
int ret=-1, lg;
s_event_list *bf=NULL, *nx=NULL;

    if (!rcd) return ret;

    lg = salara_verbose;

    if (withlock) ast_mutex_lock(&event_lock);

	bf = rcd->before;
	nx = rcd->next;
	if (bf) {
	    if (nx) {
		bf->next = nx;
		nx->before = bf;
	    } else {
		bf->next = NULL;
		event_hdr.end = bf;
	    }
	} else {
	    if (nx) {
		event_hdr.first = nx;
		nx->before = NULL;
	    } else {
		event_hdr.first = NULL;
		event_hdr.end = NULL;
	    }
	}
	rm_chan_event(rcd->event);
	if (event_hdr.counter>0) event_hdr.counter--;
	free(rcd); rcd = NULL;
	ret=0;

	if (lg) {
	    if ((unload) || (lg>2))
		ast_verbose("[%s %s] del_event_list : first=%p end=%p counter=%d\n",
			AST_MODULE, TimeNowPrn(),
			(void *)event_hdr.first,
			(void *)event_hdr.end,
			event_hdr.counter);
	}

    if (withlock) ast_mutex_unlock(&event_lock);

    return ret;
}
//------------------------------------------------------------------------
static void remove_event_list()
{
    ast_mutex_lock(&event_lock);

	while (event_hdr.first) {
	    del_event_list(event_hdr.first, 0);
	}

    ast_mutex_unlock(&event_lock);
}
//------------------------------------------------------------------------
static void init_event_list()
{
    ast_mutex_lock(&event_lock);

	if (event_hdr.first) {
	    while (event_hdr.first) del_event_list(event_hdr.first, 0);
	}
	event_hdr.first = event_hdr.end = NULL;
	event_hdr.counter = 0;

    ast_mutex_unlock(&event_lock);
}
//------------------------------------------------------------------------
static void remove_chan_records()
{
    ast_mutex_lock(&chan_lock);

	while (chan_hdr.first) {
	    del_chan_record(chan_hdr.first, 0);
	}

    ast_mutex_unlock(&chan_lock);
}
//------------------------------------------------------------------------
static void init_chan_records()
{
    ast_mutex_lock(&chan_lock);

	if (chan_hdr.first) {
	    while (chan_hdr.first) del_chan_record(chan_hdr.first, 0);
	}
	chan_hdr.first = chan_hdr.end = NULL;
	chan_hdr.counter = 0;

    ast_mutex_unlock(&chan_lock);
}
//------------------------------------------------------------------------
static s_chan_record *add_chan_record(const char *nchan, const char *caller, const char *ext, void *data)
{
int len=0, lg;
s_chan_record *ret=NULL, *tmp=NULL;
char *stc=NULL, *ste=NULL, *stcaller=NULL;

    if ((!nchan) || (!caller) || (!ext)) return ret;
    else
    if ((!strlen(nchan)) || (!strlen(caller)) || (!strlen(ext))) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&chan_lock);

	s_chan_record *rec = (s_chan_record *)calloc(1,sizeof(s_chan_record));
	if (rec) {
	    len = strlen(nchan);
	    if (len >= AST_CHANNEL_NAME) len = AST_CHANNEL_NAME-1;
	    stc = (char *)calloc(1,len+1);
	    if (stc) {
		memcpy(stc,nchan,len);
		len = strlen(ext);
		if (len >= AST_MAX_EXTENSION) len = AST_MAX_EXTENSION-1;
		ste = (char *)calloc(1,len+1);
		if (ste) {
		    memcpy(ste,ext,len);
		    len = strlen(caller);
		    if (len >= AST_MAX_EXTENSION) len = AST_MAX_EXTENSION-1;
		    stcaller = (char *)calloc(1,len+1);
		    memcpy(stcaller,caller,len);
		}
	    }
	    if ((stc) && (ste) && (stcaller)) {
		rec->chan = stc;
		rec->exten = ste;
		rec->caller = stcaller;
		rec->ast = data;
		rec->update=0;
		rec->before = rec->next = NULL;
		if (chan_hdr.first == NULL) {//first record
		    chan_hdr.first = chan_hdr.end = rec;
		} else {//add to tail
		    tmp = chan_hdr.end;
		    rec->before = tmp;
		    chan_hdr.end = rec;
		    tmp->next = rec;
		}
		chan_hdr.counter++;
		ret=rec;
	    } else {
		if (stc) free(stc);
		if (ste) free(ste);
		if (stcaller) free(stcaller);
		free(rec);
	    }
	}

	if (lg>1) {//>1
	    if (lg>2) ast_verbose("[%s %s] ADD_CHAN : first=%p end=%p counter=%d (chan='%s' ext='%s' caller='%s' ast=%p)\n",//>2
			AST_MODULE, TimeNowPrn(), (void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller, data);
	    if (ret) ast_verbose("[%s %s] ADD_CHAN : rec=%p before=%p next=%p chan='%s' ext='%s' caller='%s' ast=%p\n",
			AST_MODULE, TimeNowPrn(), (void *)ret, (void *)ret->before, (void *)ret->next,
			ret->chan, ret->exten, ret->caller, ret->ast);
	}

    ast_mutex_unlock(&chan_lock);

    return ret;
}
//------------------------------------------------------------------------
static int del_chan_record(s_chan_record *rcd, int withlock)
{
int ret=-1, lg;
s_chan_record *bf=NULL, *nx=NULL;

    if (!rcd) return ret;

    lg = salara_verbose;

    if (withlock) ast_mutex_lock(&chan_lock);

	bf = rcd->before;
	nx = rcd->next;
	if (bf) {
	    if (nx) {
		bf->next = nx;
		nx->before = bf;
	    } else {
		bf->next = NULL;
		chan_hdr.end = bf;
	    }
	} else {
	    if (nx) {
		chan_hdr.first = nx;
		nx->before = NULL;
	    } else {
		chan_hdr.first = NULL;
		chan_hdr.end = NULL;
	    }
	}
	if (chan_hdr.counter>0) chan_hdr.counter--;
	free(rcd->chan);
	free(rcd->exten);
	free(rcd->caller);
	free(rcd); //rcd = NULL;
	ret=0;

	if ((lg>1) || (chan_hdr.counter>0))
		ast_verbose("[%s %s] DEL_CHAN : rec=%p first=%p end=%p counter=%d\n",
			AST_MODULE,
			TimeNowPrn(),
			(void *)rcd,
			(void *)chan_hdr.first,
			(void *)chan_hdr.end,
			chan_hdr.counter);
	//}

    if (withlock) ast_mutex_unlock(&chan_lock);

    return ret;
}
//------------------------------------------------------------------------
static s_chan_record *update_chan(const char *nchan, const char *caller, const char *ext)
{
int lg;
s_chan_record *ret=NULL, *temp=NULL, *tmp=NULL;
char *nc=NULL;

    if ( (!ext) || (!caller) || (!nchan) ) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&chan_lock);

	if (chan_hdr.first) {
	    tmp = chan_hdr.first;
	    while (tmp) {
		if (!tmp->update) {
		    if ( (strcmp(tmp->chan, nchan)) && (!strcmp(tmp->exten, ext)) && (!strcmp(tmp->caller, caller)) ) {
			nc = (char *)calloc(1, strlen(nchan) + 1);
			if (nc) {
			    if (tmp->chan) free(tmp->chan);// tmp->chan=NULL;
			    strcat(nc, nchan);
			    tmp->chan = nc;
			    tmp->update = 1;
			}
			ret = tmp;
			break;
		    }
		}
		temp = tmp->next;
		tmp = temp;
	    }
	}

	if (lg>1) {//>1
	    if (ret)
		ast_verbose("[%s %s] UPDATE_CHAN : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s' ast=%p, record found %p\n",
				AST_MODULE, TimeNowPrn(),
				(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
				nchan, ext, caller, ret->ast,
				(void *)ret);
	    else
		if (lg>2) ast_verbose("[%s %s] UPDATE_CHAN : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s', no valid record found\n",
				AST_MODULE, TimeNowPrn(),
				(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
				nchan, ext, caller);
	}

    ast_mutex_unlock(&chan_lock);

    return ret;
}
//------------------------------------------------------------------------
static s_chan_record *find_chan(const char *nchan, const char *caller, const char *ext, int with_del)
{
int lg;//, stat;
s_chan_record *ret=NULL, *temp=NULL, *tmp=NULL;

    if (!ext) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&chan_lock);

	if (chan_hdr.first) {
	    tmp = chan_hdr.first;
	    while (tmp) {
		if ( (!strcmp(tmp->chan, nchan)) && (!strcmp(tmp->exten, ext)) && (!strcmp(tmp->caller, caller)) ) {
		    ret = tmp;
		    break;
		} else {
		    temp = tmp->next;
		    tmp = temp;
		}
	    }
	}

	if (ret) {
	    if (lg>1) ast_verbose("[%s %s] FIND_CHAN : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s' ast=%p, record found %p (with_del=%d)\n",
			AST_MODULE, TimeNowPrn(),
			(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller, ret->ast,
			(void *)ret,
			with_del);
	    if (with_del) del_chan_record(ret, 0);
	} else {
	    if (lg>2) ast_verbose("[%s %s] FIND_CHAN : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s', record not found (with_del=%d)\n",
			AST_MODULE, TimeNowPrn(),
			(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller, with_del);
	}
/*
	if (ret) {
	    if (ret->ast) {
		if (lg>2) ast_verbose("[%s %s] FIND_CHAN : chan=[%s] exten=[%s] caller=[%s] ast=%p (with_del=%d)\n",
			AST_MODULE,
			TimeNowPrn(),
			nchan,
			ext,
			caller,
			(void *)ret->ast,
			with_del);
	    } else {
		if (lg>2) ast_verbose("[%s %s] FIND_CHAN : record found at %p, but ast=NULL -> delete record ! (with_del=%d)\n",
			AST_MODULE, TimeNowPrn(), (void *)ret, with_del);
	    }
	    //if (with_del) del_chan_record(ret, 0);
	}
*/

    ast_mutex_unlock(&chan_lock);

    return ret;
}
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------
static int delete_act(s_act_list *arcd, int withlock)
{
int ret=-1, lg;
s_act_list *bf=NULL, *nx=NULL;

    if (!arcd) return ret;

    lg = salara_verbose;

    if (withlock) ast_mutex_lock(&act_lock);

	bf = arcd->before;
	nx = arcd->next;
	if (bf) {
	    if (nx) {
		bf->next = nx;
		nx->before = bf;
	    } else {
		bf->next = NULL;
		act_hdr.end = bf;
	    }
	} else {
	    if (nx) {
		act_hdr.first = nx;
		nx->before = NULL;
	    } else {
		act_hdr.first = NULL;
		act_hdr.end = NULL;
	    }
	}
	if (act_hdr.counter>0) act_hdr.counter--;
	if (arcd->act) free(arcd->act);
	free(arcd); arcd = NULL;
	ret=0;

	if (lg>2) {//>=2
	    ast_verbose("[%s %s] delete_act : first=%p end=%p counter=%u\n",
			AST_MODULE, TimeNowPrn(),
			(void *)act_hdr.first,
			(void *)act_hdr.end,
			act_hdr.counter);
	}

    if (withlock) ast_mutex_unlock(&act_lock);

    return ret;
}
//------------------------------------------------------------------------
static void delete_act_list()
{
    ast_mutex_lock(&act_lock);

	while (act_hdr.first) delete_act(act_hdr.first,0);

    ast_mutex_unlock(&act_lock);
}
//------------------------------------------------------------------------
static void init_act_list()
{
    ast_mutex_lock(&act_lock);

	act_hdr.first = act_hdr.end = NULL;
	act_hdr.counter = 0;

    ast_mutex_unlock(&act_lock);
}
//------------------------------------------------------------------------
static int update_act_by_index(unsigned int act_ind, int status, char *resp)
{
int lg, ret=-1, len;
s_act_list *temp=NULL, *tmp=NULL;

    if (!act_ind) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&act_lock);

	if (act_hdr.first) {
	    tmp = act_hdr.first;
	    while (tmp) {
		if ((tmp->act) && (tmp->act->id == act_ind)) {
		    ret = 0;
		    break;
		} else {
		    temp = tmp->next;
		    tmp = temp;
		}
	    }
	}
	if (!ret) {
	    if (tmp->act) {
		tmp->act->status = status;
		len = strlen(resp); if (len>=SIZE_OF_RESP) len=SIZE_OF_RESP-1;
		memset((char *)tmp->act->resp, 0, SIZE_OF_RESP);
		if (len>0) memcpy((char *)tmp->act->resp, resp, len);
		if (lg>2)//>=2
		    ast_verbose("[%s %s] update_act_by_index : adr=%p act_id=%u status=%d resp='%s'\n",
			AST_MODULE, TimeNowPrn(),
			(void *)tmp,
			tmp->act->id,
			tmp->act->status,
			(char *)tmp->act->resp);
	    } else ret=-1;
	}

    ast_mutex_unlock(&act_lock);

    return ret;

}
//------------------------------------------------------------------------
static int update_act(s_act_list *arcd, int status, char *resp)
{
int ret=-1, lg, len;

    if (!arcd) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&act_lock);

	if (arcd->act) {
	    arcd->act->status = status;
	    len = strlen(resp); if (len>=SIZE_OF_RESP) len=SIZE_OF_RESP-1;
	    memset((char *)arcd->act->resp, 0, SIZE_OF_RESP);
	    if (len>0) memcpy((char *)arcd->act->resp, resp, len);
	    ret=0;
	    if (lg>2)//>=2
		ast_verbose("[%s %s] update_act : adr=%p ind=%u status=%d resp='%s'\n",
			AST_MODULE, TimeNowPrn(),
			(void *)arcd,
			arcd->act->id,
			arcd->act->status,
			(char *)arcd->act->resp);
	}

    ast_mutex_unlock(&act_lock);

    return ret;
}
//------------------------------------------------------------------------
static s_act_list *find_act(unsigned int act_ind)
{
int lg;
s_act_list *ret=NULL, *temp=NULL, *tmp=NULL;

    if (!act_ind) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&act_lock);

	if (act_hdr.first) {
	    tmp = act_hdr.first;
	    while (tmp) {
		if ((tmp->act) && (tmp->act->id == act_ind)) {
		    ret = tmp;
		    break;
		} else {
		    temp = tmp->next;
		    tmp = temp;
		}
	    }
	}

	if (lg>2) {//>=2
	    if (ret)
		ast_verbose("[%s %s] find_act : first=%p end=%p counter=%u\n"
			    "\t-- rec=%p before=%p next=%p ind=%u status=%d resp='%s'\n",
			AST_MODULE, TimeNowPrn(),
			(void *)act_hdr.first,
			(void *)act_hdr.end,
			act_hdr.counter,
			(void *)ret,
			(void *)ret->before,
			(void *)ret->next,
			ret->act->id,
			ret->act->status,
			(char *)ret->act->resp);
	    else
		ast_verbose("[%s %s] find_act : first=%p end=%p counter=%u, record with ind=%u not found\n",
			AST_MODULE, TimeNowPrn(),
			(void *)act_hdr.first,
			(void *)act_hdr.end,
			act_hdr.counter,
			act_ind);
	}

    ast_mutex_unlock(&act_lock);

    return ret;
}
//------------------------------------------------------------------------
static s_act_list *add_act(unsigned int act_ind)
{
int lg;
s_act_list *ret=NULL, *tmp=NULL;
s_act *str=NULL;

    if (act_ind <= 0) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&act_lock);

	s_act_list *rec = (s_act_list *)calloc(1,sizeof(s_act_list));
	if (rec) {
	    str = (s_act *)calloc(1,sizeof(s_act));
	    if (str) {
		rec->act = str;
		rec->act->id = act_ind;
		rec->act->status = -1;
		memset((char *)rec->act->resp, 0, SIZE_OF_RESP);
		rec->before = rec->next = NULL;
		if (act_hdr.first == NULL) {//first record
		    act_hdr.first = act_hdr.end = rec;
		} else {//add to tail
		    tmp = act_hdr.end;
		    rec->before = tmp;
		    act_hdr.end = rec;
		    tmp->next = rec;
		}
		act_hdr.counter++;
		ret=rec;
	    } else free(rec);
	}

	if (lg>2) {//>=2
	    ast_verbose("[%s %s] add_act : first=%p end=%p counter=%u\n",
			AST_MODULE, TimeNowPrn(),
			(void *)act_hdr.first,
			(void *)act_hdr.end,
			act_hdr.counter);
	    if (ret) ast_verbose("\t-- rec=%p before=%p next=%p ind=%u status=%d resp='%s'\n",
			(void *)ret,
			(void *)ret->before,
			(void *)ret->next,
			ret->act->id,
			ret->act->status,
			(char *)rec->act->resp);
	}

    ast_mutex_unlock(&act_lock);

    return ret;
}
//---------------------------------------------------------------------
//---------------------------------------------------------------------
//---------------------------------------------------------------------

static int msg_send(char * cmd_line);

//---------------------------------------------------------------------
char *StrUpr(char *st)
{
    int len = strlen(st); if (!len) return st;

    for (int i=0; i<len; i++) *(st+i) = toupper(*(st+i));

    return st;
}
//---------------------------------------------------------------------
char *StrLwr(char *st)
{
    int len = strlen(st); if (!len) return st;

    for (int i=0; i<len; i++) *(st+i) = tolower(*(st+i));

    return st;
}
//---------------------------------------------------------------------
static int check_stat(int stat)
{
int ret=-1, i;

    for (i=0; i<MAX_STATUS; i++) {
	if (stat == good_status[i]) {
	    ret=0;
	    break;
	}
    }

    return ret;
}
//*********************************************************************************
static s_route_record *add_record(char *from, char *to)
{
int len=0, lg;
s_route_record *ret=NULL, *tmp=NULL;

    if ((!from) || (!to)) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&route_lock);

	s_route_record *rec = (s_route_record *)calloc(1,sizeof(s_route_record));
	if (rec) {
	    len = strlen(from);
	    if (len >= AST_MAX_EXTENSION) len = AST_MAX_EXTENSION - 1;
	    memcpy(rec->caller, from, len);
	    len = strlen(to);
	    if (len >= AST_MAX_EXTENSION) len = AST_MAX_EXTENSION - 1;
	    memcpy(rec->called, to, len);

	    rec->before = rec->next = NULL;
	    if (route_hdr.first == NULL) {//first record
		route_hdr.first = route_hdr.end = rec;
	    } else {//add to tail
		tmp = route_hdr.end;
		rec->before = tmp;
		route_hdr.end = rec;
		tmp->next = rec;
	    }
	    route_hdr.counter++;
	    ret=rec;
	}

	if (lg > 2) {
	    ast_verbose("[%s %s] add_record : first=%p end=%p counter=%d (from='%s' to='%s')\n",
			AST_MODULE, TimeNowPrn(),
			(void *)route_hdr.first,
			(void *)route_hdr.end,
			route_hdr.counter,
			from,
			to);
	    if (ret) ast_verbose("\t-- rec=%p before=%p next=%p caller='%s' called='%s'\n",
			(void *)ret,
			(void *)ret->before,
			(void *)ret->next,
			ret->caller,
			ret->called);
	}

    ast_mutex_unlock(&route_lock);

    return ret;
}
//------------------------------------------------------------------------
static s_route_record *find_record(char *from)
{
int lg;
s_route_record *ret=NULL, *temp=NULL, *tmp=NULL;

    if (!from) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&route_lock);

	if (route_hdr.first) {
	    tmp = route_hdr.first;
	    while (tmp != NULL) {
		if ( !strcmp(tmp->caller,from) ) {
		    ret = tmp;
		    break;
		} else {
		    temp = tmp->next;
		    tmp = temp;
		}
	    }
	}

	if (lg > 2) {
	    if (ret)
		ast_verbose("[%s %s] find_record : first=%p end=%p counter=%d from='%s', record found %p\n",
			AST_MODULE, TimeNowPrn(),
			(void *)route_hdr.first,
			(void *)route_hdr.end,
			route_hdr.counter,
			from,
			(void *)ret);
	    else
		ast_verbose("[%s %s] find_record : first=%p end=%p counter=%d from='%s', record not found\n",
			AST_MODULE, TimeNowPrn(),
			(void *)route_hdr.first,
			(void *)route_hdr.end,
			route_hdr.counter,
			from);
	}

    ast_mutex_unlock(&route_lock);

    return ret;
}
//------------------------------------------------------------------------
static int del_record(s_route_record *rcd, int withlock)
{
int ret=-1, lg;
s_route_record *bf=NULL, *nx=NULL;

    if (!rcd) return ret;

    lg = salara_verbose;

    if (withlock) ast_mutex_lock(&route_lock);

	bf = rcd->before;
	nx = rcd->next;
	if (bf) {
	    if (nx) {
		bf->next = nx;
		nx->before = bf;
	    } else {
		bf->next = NULL;
		route_hdr.end = bf;
	    }
	} else {
	    if (nx) {
		route_hdr.first = nx;
		nx->before = NULL;
	    } else {
		route_hdr.first = NULL;
		route_hdr.end = NULL;
	    }
	}
	if (route_hdr.counter>0) route_hdr.counter--;
	free(rcd); rcd = NULL;
	ret=0;

	if (lg > 2) {
	    ast_verbose("[%s %s] del_record : first=%p end=%p counter=%d\n",
			AST_MODULE, TimeNowPrn(),
			(void *)route_hdr.first,
			(void *)route_hdr.end,
			route_hdr.counter);
	}

    if (withlock) ast_mutex_unlock(&route_lock);

    return ret;
}
//------------------------------------------------------------------------
static void remove_records()
{
    ast_mutex_lock(&route_lock);

	while (route_hdr.first) {
	    del_record(route_hdr.first, 0);
	}

    ast_mutex_unlock(&route_lock);
}
//------------------------------------------------------------------------
static void init_records()
{
    ast_mutex_lock(&route_lock);

	route_hdr.first = route_hdr.end = NULL;
	route_hdr.counter = 0;

    ast_mutex_unlock(&route_lock);
}
//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------
static char *TimeNowPrn()
{
struct tm *ctimka;
time_t it_ct;
int i_hour, i_min, i_sec;
struct timeval tvl;

    gettimeofday(&tvl,NULL);
    it_ct=tvl.tv_sec;
    ctimka=localtime(&it_ct);
    i_hour=ctimka->tm_hour;	i_min=ctimka->tm_min;	i_sec=ctimka->tm_sec;
    memset(time_str,0,TIME_STR_LEN);
    sprintf(time_str,"%02d:%02d:%02d.%03d", i_hour, i_min, i_sec, (int)(tvl.tv_usec/1000));

    return (&time_str[0]);
}
//------------------------------------------------------------------------------
static char *seconds_to_date(char *buf, time_t sec)
{
char *ret = buf;
int day=0, min=0, hour=0, seconda=0;

    day = sec / (60*60*24);// get days
    sec = sec % (60*60*24);

    hour = sec / (60*60);// get hours
    sec = sec % (60*60);

    min = sec / (60);// get minutes
    sec = sec % 60;

    seconda = sec;// get seconds

    sprintf(buf, "%04d:%02d:%02d:%02d", day, hour, min, seconda);

    return ret;
}
//----------------------------------------------------------------------
static int salara_cleanup(void);
static void salara_atexit(void);
static int read_config(const char * file_name, int prn);
static int write_config(const char * file_name, int prn);
static int check_dest(char *from, char *to);
static int MakeAction(int type, char *from, char *to, char *mess, char *ctext);
static int send_to_crm(s_chan_event *evt);
static int send_curl_event(char *url, char *body, int wait, char *str, int str_len, CURLcode *err, int crt, int ctype, int prn);
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------

static const char * const global_useragent = "libcurl-agent/1.0";

//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------

static int hook_callback(int category, const char *event, char *body);

static struct manager_custom_hook hook = {
    .file = __FILE__,
    .helper = &hook_callback,
};

//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
static char *cli_salara_info(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_set_verbose(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_set_route(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_send_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_get_status_exten(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_get_status_chan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_get_status_peer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_send_msg(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
static char *cli_salara_send_post(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
//----------------------------------------------------------------------
static struct ast_cli_entry cli_salara[] = {
    AST_CLI_DEFINE(cli_salara_info, "Show Salara module information/configuration/route/chan_records"),
    AST_CLI_DEFINE(cli_salara_set_verbose, "Off/On/Debug/Dump verbose level"),
    AST_CLI_DEFINE(cli_salara_set_route, "Add caller:called to route table"),
    AST_CLI_DEFINE(cli_salara_send_cmd, "Send AMI Command"),
    AST_CLI_DEFINE(cli_salara_get_status_exten, "Get extension status"),
    AST_CLI_DEFINE(cli_salara_get_status_chan, "Get channel status"),
    AST_CLI_DEFINE(cli_salara_get_status_peer, "Get peer status"),
    AST_CLI_DEFINE(cli_salara_send_msg, "Send AMI Message"),
    AST_CLI_DEFINE(cli_salara_send_post, "Send POST request to http server"),
};
//----------------------------------------------------------------------
static size_t WrMemCallBackFunc(void *contents, size_t size, size_t nmemb, void *userp)
{
size_t realsize = size *nmemb;
struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) return 0;

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
//----------------------------------------------------------------------
static size_t WrMemEventCallBackFunc(void *contents, size_t size, size_t nmemb, void *userp)
{
size_t realsize = size *nmemb;
struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) return 0;

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
//----------------------------------------------------------------------
static int CheckCurlAnswer(char *buk, char *exten)
{
int ret=-1;
json_error_t errj;
json_t *obj=NULL, *tobj=NULL;

    obj = json_loads(buk, 0, &errj);
    if (obj) {
	tobj = json_object_get(obj, key_word);
	if (json_is_string(tobj)) {
	    sprintf(exten,"%s", json_string_value(tobj));
	    ret=0;
	} else if (json_is_integer(tobj)) {
	    sprintf(exten,"%lld", json_integer_value(tobj));
	    ret=0;
	}
	json_decref(obj);
    }

    return ret;
}
//----------------------------------------------------------------------
static int send_curl(char *url, int wait, char *str, CURLcode *err, int crt)
{
int ret=-1, lg = salara_verbose;
CURL *curl;
CURLcode res = CURLE_OK;
struct MemoryStruct chunk;

    chunk.memory = (char *)malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if (curl) {
	struct curl_slist *headers=NULL;
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	curl_easy_setopt(curl, CURLOPT_URL, url);
	//if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	if (crt) {//disable certificates
	    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WrMemCallBackFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, global_useragent);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, wait);
	res = curl_easy_perform(curl);
	*err = res;
	if (res == CURLE_OK) {
	    ret = CheckCurlAnswer((char *)chunk.memory, str);
	    if (lg>1)
		ast_verbose("[%s %s] Curl answer :%.*s\n", AST_MODULE, TimeNowPrn(), chunk.size, (char *)chunk.memory);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(chunk.memory);
    }

    return ret;
}
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
static int app_salara_exec(struct ast_channel *ast, const char *data)
{
int lg;
int ret_curl=-1, stat=-1, res_transfer;
char *cid=NULL, *info=NULL, *buf=NULL;
//unsigned int aid=0;
//s_act_list *abc=NULL;
int ssl=0;
CURLcode er;


    if (!data) return -1;
    else
    if (!strlen(data)) return -1;

    buf = (char *)calloc(1,MAX_ANSWER_LEN);

    if (!buf) {
	ast_verbose("[%s %s] calloc memory error !\n", AST_MODULE, TimeNowPrn());
	return -1;
    }

    lg = salara_verbose;

    if (lg) ast_verbose("[%s %s] application start.\n", AST_MODULE, TimeNowPrn());

    cid = ast_channel_caller(ast)->id.number.str;

    //----------------   send Curl   -------------------------------------
    info = (char *)calloc(1, strlen(dest_url) + strlen(cid) + 1);
    if (info) {
	sprintf(info,"%s%s", dest_url, cid);
	if (strstr(dest_url,"https")) ssl=1;
	ret_curl = send_curl(info, SALARA_CURLOPT_TIMEOUT, buf, &er, ssl);
	if (er != CURLE_OK) {
	    ret_curl = 1;
	    if (lg) ast_verbose("[%s %s] Send_request_to '%s'\n\t--buf=[%s] err='%s'\n", AST_MODULE, TimeNowPrn(), info, buf, curl_easy_strerror(er));
	} else if (lg) ast_verbose("[%s %s] Send_request_to '%s'\n", AST_MODULE, TimeNowPrn(), info);
	free(info);
    } else {
	ast_verbose("[%s %s] Error: calloc memory\n", AST_MODULE, TimeNowPrn());
	return -1;
    }
    //--------------------------------------------------------------------

    if (ret_curl!=0) {
	memset(dest_number,0,AST_MAX_EXTENSION);
	strcpy(dest_number, DEF_DEST_NUMBER);
	check_dest(cid, dest_number);
	if (lg) ast_verbose("[%s %s] Curl failure, route call to default dest '%s'\n",
		    AST_MODULE,
		    TimeNowPrn(),
		    dest_number);
    } else {
	memset(dest_number,0,AST_MAX_EXTENSION);
	strcpy(dest_number, buf);
    }
    //aid = MakeAction(2, dest_number, "", "", context);
    stat = ast_extension_state(NULL, context, dest_number);//(struct ast_channel *c, const char *context, const char *exten)
    //ast_cli(a->fd, "Status=%d (%s)\n", status, ast_extension_state2str(status));

//    if (aid>=0) {
//	abc = find_act(aid);
//	if (abc) {
//	    stat = abc->act->status;
//	    delete_act(abc,1);
//	}
	if (!check_stat(stat)) {
	    if (lg>1) ast_verbose("[%s %s] Extension '%s' status (%d) OK ! (%s)\n", AST_MODULE, TimeNowPrn(), dest_number, stat, ast_extension_state2str(stat));
	} else {
	    if (lg) ast_verbose("[%s %s] Extension '%s' status (%d) BAD ! (%s)\n", AST_MODULE, TimeNowPrn(), dest_number, stat, ast_extension_state2str(stat));
	    memset(dest_number,0,AST_MAX_EXTENSION);
	    strcpy(dest_number, DEF_DEST_NUMBER);
	    check_dest(cid, dest_number);
	    if (lg) ast_verbose("[%s %s] Route call to default dest '%s'\n", AST_MODULE, TimeNowPrn(), dest_number);
	}
//    }


    res_transfer = ast_transfer(ast, dest_number);

    if (res_transfer>0) add_chan_record(ast_channel_name(ast), cid, dest_number, (void *)ast);


    if (lg) {
	stat = ast_channel_state(ast); if (stat > MAX_CHAN_STATE-1) stat=MAX_CHAN_STATE-1;
	ast_verbose("[%s %s] CallerID=[%s] called=[%s] transfer(%d) to '%s' status=%d (%s)\n",
		AST_MODULE, TimeNowPrn(),
		ast_channel_name(ast),
		data,
		res_transfer,
		dest_number,
		stat,
		ast_extension_state2str(stat));
		//&ChanStateName[stat][0]);
	ast_verbose("[%s %s] application stop.\n", AST_MODULE, TimeNowPrn());
    }

    if (buf) free(buf);

    return 0;
}
//----------------------------------------------------------------------
static int hook_callback(int category, const char *event, char *body)
{
int lg, id=-1, sti=-1, done=0, dl, rdy=0, tp=-1;
char *uk=NULL, *uk2=NULL;
char stx[SIZE_OF_RESP]={0};
unsigned char i=0;

//if ( (strstr(event,"RTCP")) || (strstr(event,"Cdr")) || (strstr(event,"VarSet")) ) return 0;

//if (salara_verbose) ast_verbose("[%s %s] cat=%d event='%s' body=[\n%s]\n", AST_MODULE, TimeNowPrn(), category, event, body);

    while (i<MAX_EVENT_NAME) {
	if (!strcmp(event,EventName[i])) {
	    tp=i;
	    break;
	}
	i++;
    }
    if (tp == -1) return 0;

    lg = salara_verbose;

//    if (lg) ast_verbose("[%s %s] cat=%d event='%s' body=[\n%s]\n", AST_MODULE, TimeNowPrn(), category, event, body);
//    return 0;

    if ( (strlen(hook_tmp_str) + strlen(body)) >= max_buf_size ) {
	if (lg) ast_verbose("hook_callback error :<%s>\n<%s>\n",hook_tmp_str,body);
	memset(hook_tmp_str,0,max_buf_size);
    }
    strcat(hook_tmp_str, body);
    if (strstr(hook_tmp_str, "\r\n\r\n")) done=1;

    if (done) {

//if (lg) ast_verbose("[%s %s] event='%s' body=[\n%s]\n",AST_MODULE,TimeNowPrn(),event,hook_tmp_str);
//if (tp == -1) return 0;

	switch (tp) {
	    case 0 ://HookResponse
		if (console) ast_verbose("%s",hook_tmp_str);
//if (lg) ast_verbose("[%s %s] event='%s' body=[\n%s]\n",AST_MODULE,TimeNowPrn(),event,hook_tmp_str);
		uk = strstr(hook_tmp_str, S_ActionID);
		if (uk) {
		    uk += strlen(S_ActionID); uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			memset(stx,0,SIZE_OF_RESP); dl = uk2 - uk; if (dl>=SIZE_OF_RESP) dl = SIZE_OF_RESP-1;
			memcpy(stx, uk, dl); id = atoi(stx); uk = strstr(hook_tmp_str,S_Status);
			if (uk) {
			    uk += strlen(S_Status); uk2 = strstr(uk, "\r\n"); if (uk2 == NULL) uk2 = strchr(uk,'\0');
			    if (uk2) {
				memset(stx,0,SIZE_OF_RESP); dl = uk2 - uk; if (dl>=SIZE_OF_RESP) dl = SIZE_OF_RESP-1;
				memcpy(stx, uk, dl); sti = atoi(stx); rdy=1;
				uk = strstr(hook_tmp_str, S_StatusText);
				if (uk) { uk += strlen(S_StatusText); if (*uk == ' ') uk++; }
			    }
			} else {
			    uk = strstr(hook_tmp_str,S_PeerStatus);
			    if (uk) {
				uk += strlen(S_PeerStatus); if (*uk == ' ') uk++; sti=0; rdy=1;
			    } else {
				uk = strstr(hook_tmp_str, S_ResponseF);
				if (uk) {
				    uk = strstr(hook_tmp_str, S_State);
				    if (uk) { uk += strlen(S_State); if (*uk == ' ') uk++; sti=0; } 
				       else { uk = S_ChanOff; sti=-1; }
				    rdy=1;
				} else {
				    uk = strstr(hook_tmp_str, S_Response);
				    if (uk) {
					uk += strlen(S_Response);
					if (*uk == ' ') uk++; sti=-1; if (strstr(uk, S_Success)) sti=0; rdy=1;
				    }
				}
			    }
			}
			if (rdy) {
			    memset(stx,0,SIZE_OF_RESP);
			    if (uk) {
				uk2 = strchr(uk, '\n'); if (uk2) { if (*(uk2-1) == '\r') uk2--; } else uk2 = strchr(uk,'\0');
				if (uk2) {
				    dl = uk2 - uk; if (dl>=SIZE_OF_RESP) dl = SIZE_OF_RESP-1;
				    memcpy(stx, uk, dl);
				}
			    }
//if (lg) ast_verbose("[hook] event='%s' action_id=%d stat=%d stat_text='%s'\n", event, id, sti, stx);
			    if (update_act_by_index(id, sti, stx) != 0)
				if (lg>=2) ast_verbose("[hook] event='%s' action_id=%d not found in act_list\n", event, id);
			}
		    }
		} else if (lg) ast_verbose("[%s %s] event='%s' ActionID not found\n", AST_MODULE, TimeNowPrn(), event);
	    break;
	    case 1://Hangup
	    case 2://Newchannel
	    case 3://Newexten
		if (lg>2) ast_verbose("%s",hook_tmp_str);
		int cs = MAX_CHAN_STATE-1;
		char chan[AST_CHANNEL_NAME]; char exten[AST_MAX_EXTENSION]; 
		char caller[AST_MAX_EXTENSION]; char chan_state[AST_MAX_EXTENSION]; char app[AST_MAX_EXTENSION];
		memset(chan,0,AST_CHANNEL_NAME); memset(exten,0,AST_MAX_EXTENSION);
		memset(caller,0,AST_MAX_EXTENSION); memset(chan_state,0,AST_MAX_EXTENSION); memset(app,0,AST_MAX_EXTENSION);
		uk = strstr(hook_tmp_str, S_Channel);
		if (uk) {
		    uk += strlen(S_Channel); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_CHANNEL_NAME) dl=AST_CHANNEL_NAME-1;
			memcpy(chan, uk, dl);
		    }
		}
		uk2=NULL;
		uk = strstr(hook_tmp_str, S_Exten);
		if (uk) {
		    uk += strlen(S_Exten); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		} else {
		    uk = strstr(hook_tmp_str, S_Extension);
		    if (uk) {
			uk += strlen(S_Extension); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    }
		}
		if (uk && uk2) {
		    dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1;
		    memcpy(exten, uk, dl);
		}

		uk = strstr(hook_tmp_str, S_CallerIDNum);
		if (uk) {
		    uk += strlen(S_CallerIDNum); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1;
			memcpy(caller, uk, dl);
		    }
		}
		uk = strstr(hook_tmp_str, S_ChannelState);
		if (uk) {
		    uk += strlen(S_ChannelState); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1;
			memcpy(chan_state, uk, dl); cs = atoi(chan_state);
		    }
		}
		uk = strstr(hook_tmp_str, S_Application);
		if (uk) {
		    uk += strlen(S_Application); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1;
			memcpy(app, uk, dl);
		    }
		}

		if ((cs<0) || (cs>=MAX_CHAN_STATE)) cs = MAX_CHAN_STATE-1;

		if (lg>=2) ast_verbose("[%s %s] '%s' event : chan='%s' caller='%s' exten='%s' state=%d(%s) app='%s'\n",//>=2
				AST_MODULE, TimeNowPrn(), event, chan, caller, exten, cs, &ChanStateName[cs][0], app);
		if (tp==1) {//Hangup
		    if ( (strlen(chan)) && (strlen(exten)) && (strlen(caller)) ) {
			switch (hangup) {
			    case 1://used for transfer calls only
				if (find_chan(chan, caller, exten, 1)) add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			    case 2://used for all calls
				add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			}
		    }
		} else if (tp==2) {//Newchannel
//if (lg) ast_verbose("[%s %s] event='%s' body=[\n%s]\n",AST_MODULE,TimeNowPrn(),event,hook_tmp_str);
		    if ( (strlen(chan)) && (strlen(exten)) && (strlen(caller)) ) {
			update_chan(chan, caller, exten);
			switch (newchannel) {
			    case 1://used for transfer calls only
				if (find_chan(chan, caller, exten, 0))
				    add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			    case 2://used for all calls
				add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			}
		    }
		} else if (tp==3) {//Newexten
		    if ( (strlen(chan)) && (strlen(exten)) && (strlen(caller)) ) {
			if (cs==6) update_chan(chan, caller, exten);
			switch (newexten) {
			    case 1://used for transfer call with status UP
				if (cs==6) {//when state=UP
				    if (find_chan(chan, caller, exten, 0))//when find caller:exten in chan_list
					add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
				}
			    break;
			    case 2://used for transfer call with all status
				if (find_chan(chan, caller, exten, 0))//when find caller:exten in chan_list
					add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			    case 3://used for all call with all status
				add_event_list(make_chan_event(tp, chan, caller, exten, cs, app));
			    break;
			}
		    }
		}
	    break;
	    case 4://AgentConnect
		if (lg) ast_verbose("%s",hook_tmp_str);
	    break;
		default : if (lg) ast_verbose("[%s %s] Unknown event='%s' body=[\n%s]\n",AST_MODULE,TimeNowPrn(),event,hook_tmp_str);
	}//switch

	if ((lg>2) && (!console)) ast_verbose("[%s %s] event='%s' body=[\n%s]\n",AST_MODULE,TimeNowPrn(),event,hook_tmp_str);
	memset(hook_tmp_str,0,max_buf_size);
    }//if (done)

    return 0;
}
//------------------------------------------------------------------------------
static char *cli_salara_info(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
struct timeval c_t;
char buf[64]={0};
s_route_record *rt=NULL, *nx=NULL;
unsigned char i;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara show {info|conf|route|chan_records}";
	    e->usage = "Usage: salara show {info|conf|route|chan_records}\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:
	    break;
	default:
	    ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
	    return CLI_FAILURE;
    }

    if (a->argc < 3) return CLI_SHOWUSAGE;

    int lg = salara_verbose;

    if (!strcmp(a->argv[2],"conf")) {
	salara_config_file_len = read_config(salara_config_file, 1);
    } else if (!strcmp(a->argv[2],"info")) {
	gettimeofday(&c_t, NULL);
	ast_cli(a->fd, "\tSalara module info:\n");
	if (salara_config_file_len>0) ast_cli(a->fd, "\t-- configuration file '%s'\n", salara_config_file);
	ast_cli(a->fd, "\t-- default routing dest '%s'\n", dest_number);
	ast_cli(a->fd, "\t-- rest server listen on: '%s'\n", rest_server);
	ast_cli(a->fd, "\t-- default url '%s'\n", dest_url);
	ast_cli(a->fd, "\t-- default url_event '%s'\n", dest_url_event);
	ast_cli(a->fd, "\t-- curl timeout %d\n", SALARA_CURLOPT_TIMEOUT);
	ast_cli(a->fd, "\t-- default context: %s\n", context);
	ast_cli(a->fd, "\t-- module version: %s\n", SALARA_VERSION);
	ast_cli(a->fd, "\t-- asterisk version: %s\n", ast_get_version());
	ast_cli(a->fd, "\t-- events verbose level: %d ", lg);

	switch (lg) {
	    case 0 : ast_cli(a->fd, "(off)\n"); break;
	    case 1 : ast_cli(a->fd, "(on)\n"); break;
	    case 2 : ast_cli(a->fd, "(debug)\n"); break;
	    case 3 : ast_cli(a->fd, "(dump)\n"); break;
		default: ast_cli(a->fd, "(unknown)\n");
	}
	ast_cli(a->fd, "\t-- watch makecall: %d\n", watch_makecall);
	ast_cli(a->fd, "\t-- Hangup event: %d", hangup);
	switch (hangup) {
	    case 1: ast_cli(a->fd, " (used for transfer call only)\n"); break;
	    case 2: ast_cli(a->fd, " (used for all calls)\n"); break;
		default : ast_cli(a->fd, " (not used)\n");
	}
	ast_cli(a->fd, "\t-- Newchannel event: %d", newchannel);
	switch (newchannel) {
	    case 1: ast_cli(a->fd, " (used for transfer call only)\n"); break;
	    case 2: ast_cli(a->fd, " (used for all calls)\n"); break;
		default : ast_cli(a->fd, " (not used)\n");
	}
	ast_cli(a->fd, "\t-- Newexten event: %d", newexten);
	switch (newexten) {
	    case 1: ast_cli(a->fd, " (used for transfer call with status UP)\n"); break;
	    case 2: ast_cli(a->fd, " (used for transfer call with all status)\n"); break;
	    case 3: ast_cli(a->fd, " (used for all call with all status)\n"); break;
		default : ast_cli(a->fd, " (not used)\n");
	}
	ast_cli(a->fd, "\t-- started: %s", ctime(&salara_start_time.tv_sec));
	c_t.tv_sec -= salara_start_time.tv_sec;
	ast_cli(a->fd, "\t-- uptime: %s (%lu sec)\n", seconds_to_date(buf, c_t.tv_sec), c_t.tv_sec);

	ast_mutex_lock(&route_lock);
	    ast_cli(a->fd, "\t-- routing table: %d records",route_hdr.counter);
	    if (lg > 2) {
		if (route_hdr.first)
		    ast_cli(a->fd," (first=%p, end=%p)", (void *)route_hdr.first, (void *)route_hdr.end);
	    }
	    ast_cli(a->fd,"\n");
	ast_mutex_unlock(&route_lock);

	ast_cli(a->fd, "\t-- key_word: '%s'\n",key_word);
	ast_cli(a->fd, "\t-- rest keys:");
	for (i=0; i<max_param_rest; i++) ast_cli(a->fd, " '%s'",&names_rest[i][0]);
	ast_cli(a->fd,"\n");

    } else if (!strcmp(a->argv[2],"route")) {

	ast_mutex_lock(&route_lock);
	    rt = route_hdr.first;
	    if (rt) {
		ast_cli(a->fd, "\tSalara route table (caller:called):\n");
		if (lg > 2) ast_cli(a->fd,"\tHDR: first=%p end=%p counter=%d\n",
				    (void *)route_hdr.first,
				    (void *)route_hdr.end,
				    route_hdr.counter);
		while (rt) {
		    if (lg > 2) ast_cli(a->fd,"\trec=%p before=%p next=%p caller='%s' called='%s'\n",
				    (void *)rt,
				    (void *)rt->before,
				    (void *)rt->next,
				    rt->caller,
				    rt->called);
			    else ast_cli(a->fd,"\t%s:%s\n", rt->caller, rt->called);
		    nx = rt->next;
		    rt = nx;
		}
	    } else ast_cli(a->fd, "\tSalara route table is Empty\n");
	ast_mutex_unlock(&route_lock);

    } else if (!strcmp(a->argv[2],"chan_records")) {

	s_chan_record *temp=NULL, *tmp=NULL;

	ast_mutex_lock(&chan_lock);

	    if (chan_hdr.first) {
		ast_cli(a->fd,"adr=%p - %p total=%u\n",(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter);
		tmp = chan_hdr.first;
		while (tmp) {
		    ast_cli(a->fd,"chan='%s' exten='%s' caller='%s' ast=%p update=%d\n", tmp->chan, tmp->exten, tmp->caller, tmp->ast, tmp->update);
		    temp = tmp->next;
		    tmp = temp;
		}
	    }

	ast_mutex_unlock(&chan_lock);

    }

    return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
static char *cli_salara_set_verbose(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int lg = salara_verbose;

    int nlg = lg;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara set verbose {off|on|debug|dump}";
	    e->usage = "Usage: salara set verbose {off|on|debug|dump}\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:
	    break;
	default:
	    ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
	    return CLI_FAILURE;
    }

    if (a->argc < 4) return CLI_SHOWUSAGE;

    if (!strcmp(a->argv[3],"off")) nlg = 0;
    else
    if (!strcmp(a->argv[3],"on")) nlg = 1;
    else
    if (!strcmp(a->argv[3],"debug")) nlg = 2;
    else
    if (!strcmp(a->argv[3],"dump")) nlg = 3;

    if (nlg != lg) {
	salara_verbose = nlg;
	salara_config_file_len = write_config(salara_config_file, 1);
    }

    ast_cli(a->fd, "\tSalara set events verbose to %s (%d)\n", a->argv[3], nlg);

    return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
static char *cli_salara_set_route(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int cr=0, cd=0;
char caller[AST_MAX_EXTENSION];
char called[AST_MAX_EXTENSION];

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara set route";
	    e->usage = "Usage: salara set route <caller called>\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:
	    break;
	default:
	    ast_cli(a->fd, "unknown CLI command = %d\n", cmd);
	    return CLI_FAILURE;
    }

    if (a->argc < 5) return CLI_SHOWUSAGE;


    if (strlen(a->argv[3])>0) {
	memset(caller,0,AST_MAX_EXTENSION);
	strcpy(caller, a->argv[3]);
	cr = 1;
    }
    if (strlen(a->argv[4])>0) {
	memset(called,0,AST_MAX_EXTENSION);
	strcpy(called, a->argv[4]);
	cd = 1;
    }

    if ((!cd) || (!cr)) return CLI_SHOWUSAGE;

    if (!find_record(caller)) {
	add_record(caller, called);
	salara_config_file_len = write_config(salara_config_file, 1);
	ast_cli(a->fd, "\tSalara: add '%s:%s' to route table\n", caller, called);
    } else ast_cli(a->fd, "\tSalara: '%s:%s' already exist\n", caller, called);

    return CLI_SUCCESS;
}
//------------------------------------------------------------------------------
static int msg_send(char *cmd_line)
{
    return (ast_hook_send_action(&hook, cmd_line));
}
//----------------------------------------------------------------------
static char *cli_salara_send_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int i, act, len=128;
char *buf=NULL;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara send command";
	    e->usage = "\nUsage: salara send command <core|sip|manager|http|...>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 4) return CLI_SHOWUSAGE;

	    for (i=3; i < a->argc; i++) len += strlen(a->argv[i]);
	    buf = (char *)calloc(1,len);
	    if (buf) {
		sprintf(buf,"Action: Command\nCommand: ");
		i = 3;
		while (a->argc > i) {
		    if (i!=3) sprintf(buf+strlen(buf)," ");
		    sprintf(buf+strlen(buf),"%s",a->argv[i++]);
		}

		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		sprintf(buf+strlen(buf),"\nActionID: %u\n\n",act);
		console=1;
		msg_send(buf);
		free(buf);
		console=0;

		usleep(1000);
		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);

		return CLI_SUCCESS;
	    }
	break;
    }

    return CLI_FAILURE;
}

//----------------------------------------------------------------------
static char *cli_salara_get_status_exten(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
//int act;
//char *buf=NULL;
int status;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara get status extension";
	    e->usage = "\nUsage: salara get status extension <exten>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 5) return CLI_SHOWUSAGE;

	    status = ast_extension_state(NULL, context, a->argv[4]);//(struct ast_channel *c, const char *context, const char *exten)
	    ast_cli(a->fd, "Status=%d (%s)\n", status, ast_extension_state2str(status));
/*
	    buf = (char *)calloc(1, CMD_BUF_LEN);
	    if (buf) {
		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		sprintf(buf,"Action: ExtensionState\nActionID: %u\nExten: %s\nContext: %s\n\n",
			    act,
			    a->argv[4],
			    context);
		ast_cli(a->fd, "%s", buf);
		console=1;
		msg_send(buf);
		free(buf);
		console=0;
		usleep(1000);

		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);
*/
		return CLI_SUCCESS;
	    //}
	break;
    }

    return CLI_FAILURE;
}
//----------------------------------------------------------------------
static char *cli_salara_get_status_chan(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int act;
char *buf=NULL;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara get status channel";
	    e->usage = "\nUsage: salara get status channel <chan>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 5) return CLI_SHOWUSAGE;

	    buf = (char *)calloc(1, CMD_BUF_LEN);
	    if (buf) {
		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		//sprintf(buf,"Action: Status\nChannel: %s\nActionID: %u\n\n", a->argv[4], act);// !!! <- BAD !!!
		sprintf(buf,"Action: Command\nCommand: core show channel %s\nActionID: %u\n\n",a->argv[4],act);
		ast_cli(a->fd, "%s", buf);
		console=1;
		msg_send(buf);
		console=0;

		usleep(1000);
		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);
		free(buf);

		return CLI_SUCCESS;
	    }
	break;
    }

    return CLI_FAILURE;
}
//----------------------------------------------------------------------
static char *cli_salara_get_status_peer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int act;
char *buf=NULL;
//char stx[256]={0};

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara get status peer";
	    e->usage = "\nUsage: salara get status peer <peer>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 5) return CLI_SHOWUSAGE;
	
/*	    struct sip_peer *peer = NULL;
//	    peer = realtime_peer(peer_name, NULL, NULL, TRUE, FINDPEERS);
	    //peer = sip_find_peer(peer_name, NULL, TRUE, FINDPEERS, FALSE, 0);
	    if (!peer) {
		ast_cli(a->fd, "Peer %s not found\n", a->argv[4]);
		return CLI_SUCCESS;
	    }
	    if (peer->maxms) {
		if (peer->lastms < 0) {
			sprintf(stx, "Unreachable");
		} else if (peer->lastms > peer->maxms) {
			sprintf(stx, "Lagged");
		} else if (peer->lastms) {
			sprintf(stx,"Reachable");
		} else {
			sprintf(stx,"Unknown");
		}
	    } else {
		sprintf(stx,"Unmonitored");
	    }
	    ast_cli(a->fd, "Peer %s status %s", a->argv[4], stx);
	    return CLI_SUCCESS;
*/

	    buf = (char *)calloc(1, CMD_BUF_LEN);
	    if (buf) {
		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		sprintf(buf,"Action: SIPpeerstatus\nActionID: %u\nPeer: %s\n\n", act, a->argv[4]);
		ast_cli(a->fd, "%s", buf);
		console=1;
		msg_send(buf);
		console=0;

		usleep(1000);//2000
		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);
		free(buf);

		return CLI_SUCCESS;
	    }
	break;
    }

    return CLI_FAILURE;
}
//------------------------------------------------------------------------------
static char *cli_salara_send_msg(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
char *buf=NULL;
int act, len=0;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara send msg";
	    e->usage = "\nUsage: salara send message <from to \"body\">\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 4) return CLI_SHOWUSAGE;

	    len = strlen(a->argv[3]) + strlen(a->argv[4]) + strlen(a->argv[5]) + 128;
	    buf = (char *)calloc(1,len);
	    if (buf) {
		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		if (strchr((char *)a->argv[4],':'))
		    sprintf(buf,"Action: MessageSend\nActionID: %u\nTo: %s\nFrom: %s\nBody: %s\n\n",
			    act,
			    a->argv[4],
			    a->argv[3],
			    a->argv[5]);
		else
		    sprintf(buf,"Action: MessageSend\nActionID: %u\nTo: %s:%s\nFrom: %s\nBody: %s\n\n",
			    act,
			    StrLwr(Tech),
			    a->argv[4],
			    a->argv[3],
			    a->argv[5]);
		if (salara_verbose) ast_verbose(buf);
		console=1;
		msg_send(buf);
		free(buf);
		console=0;
		usleep(1000);

		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);

		return CLI_SUCCESS;
	    }
	break;
    }

    return CLI_FAILURE;
}
//------------------------------------------------------------------------------
static char *cli_salara_send_post(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
char *buf=NULL;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara send post";
	    e->usage = "\nUsage: salara send post <type \"body\">\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 5) return CLI_SHOWUSAGE;

	    buf = (char *)calloc(1,1024);
	    if (buf) {
		CURLcode err;
		int ssl=0, js=0;

		if (strstr(dest_url_event,"https")) ssl=1;
		if (strstr((char *)a->argv[3],"json")) js=1;

		send_curl_event(dest_url_event, (char *)a->argv[4], SALARA_CURLOPT_TIMEOUT, buf, 1024, &err, ssl, js, 0);

		ast_verbose("%s\n",buf);
		free(buf);
		return CLI_SUCCESS;
	    }
	break;
    }

    return CLI_FAILURE;
}
//----------------------------------------------------------------------
static int get_good_status(char *st, int prn)
{
int len=0, loop=1, cnt=0;
char *uk=NULL, *uk2=NULL, *ukstart=NULL, *ukend=NULL;
char stx[256]={0}, sta[64];

    len = strlen(st);
    if (!len) return cnt;

    strcpy(stx, st); len = strlen(stx);
    uk = ukstart = stx;
    ukend = ukstart + len;
    if (prn>1) ast_verbose("\tGet_good_status IN:'%s'\n", stx);
    while (loop) {
	uk2 = strchr(uk,',');
	if (uk2==NULL) uk2 = strchr(uk,'\n');
	if (uk2==NULL) uk2 = strchr(uk,'\0');
	if (uk2) {
	    memset(sta,0,64);
	    memcpy(sta, uk, uk2-uk);
	    if (cnt < MAX_STATUS) {
		good_status[cnt] = atoi(sta);
		cnt++;
		uk = uk2+1;
	    } else loop=0;
	}
	if (uk>=ukend) loop=0;
    }
    if (prn>1) {
	memset(stx,0,256);
	sprintf(stx,"\tGet_good_status (%d):", cnt);
	for (loop=0; loop<MAX_STATUS; loop++) {
	    if (!loop) sprintf(stx+strlen(stx),"%d", good_status[loop]);
		  else sprintf(stx+strlen(stx),",%d", good_status[loop]);
	}
	sprintf(stx+strlen(stx),"\n");
	ast_verbose(stx);
    }

    return cnt;
}
//----------------------------------------------------------------------
static int read_config(const char *file_name, int prn)
{
FILE *fp=NULL;
char buf[PATH_MAX];
char caller[AST_MAX_EXTENSION];
char called[AST_MAX_EXTENSION];
int len = -1, dl, verb, i, w_mc=-1;
bool begin=false, end=false;
char *_begin=NULL, *uk=NULL, *uki=NULL, *uks=NULL;

    sprintf(buf, "%s/%s", ast_config_AST_CONFIG_DIR, file_name);

    if ((fp = fopen(buf, "r"))) {
	if (prn) ast_verbose("\tConfiguration file '%s' present:\n", file_name);
	len=0;
	memset(buf, 0, PATH_MAX);
	while (fgets(buf, PATH_MAX-1, fp) != NULL) {
	    len += strlen(buf);
	    if (prn) ast_verbose("%s", buf);
	    //--------------------------------------
	    //		copy data from [route] to route_table
	    dl = strlen(buf);
	    if (dl>3) {
		_begin = &buf[0];
		if ((*_begin != ';') && (*_begin != '/')) {
		    if (begin) {
			if (*_begin == '[') { end=true; begin=false; }
			else {
			    if (!end) {
				uk = strchr(buf,'\n');
				if (uk) {
				    *uk = '\0';
				    dl = strlen(buf);
				}
				uk = strchr(buf,':');
				if (uk) {
				    memset(caller,0,AST_MAX_EXTENSION);
				    memset(called,0,AST_MAX_EXTENSION);
				    strcpy(called,(uk+1));
				    *uk = '\0';
				    strcpy(caller,_begin);
				    if (!find_record(caller)) add_record(caller, called);
				}
			    }
			}
		    }

		    if (strstr(buf, "[route]")) begin = true;

		    uki = strstr(buf,"default=");
		    if (uki) {
			//ast_verbose("\tread_config: default:%s\n",buf);
			uks = strchr(buf,'\n'); if (uks) *uks = '\0';
			uki += 8;
			strcpy(dest_number,uki);
		    } else {
			uki = strstr(buf,"verbose=");
			if (uki) {
			    //ast_verbose("\tread_config: verbose:%s\n",buf);
			    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
			    uki += 8;
			    verb = atoi(uki);
			    if ((verb>=0) && (verb<=3)) salara_verbose = verb;
			} else {
			    uki = strstr(buf,"curlopt_timeout=");
			    if (uki) {
				//ast_verbose("\tread_config: curlopt_timeout:%s\n",buf);
				uks = strchr(buf,'\n'); if (uks) *uks = '\0';
				uki += 16;
				verb = atoi(uki);
				if ((verb>0) && (verb<=5)) SALARA_CURLOPT_TIMEOUT = verb;
			    } else {
				uki = strstr(buf,"dest_url=");
				if (uki) {
				    //ast_verbose("\tread_config: dest_url:%s\n",buf);
				    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
				    uki += 9;
				    sprintf(dest_url,"%s",uki);
				} else {
				    uki = strstr(buf,"default_context=");
				    if (uki) {
					//ast_verbose("\tread_config: context:%s\n",buf);
					uks = strchr(buf,'\n'); if (uks) *uks = '\0';
					uki += 16;
					sprintf(context,"%s",uki);
				    } else {
					uki = strstr(buf,"good_status=");
					if (uki) {
					    //ast_verbose("\tread_config: good_status:%s\n",buf);
					    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
					    uki += 12;
					    get_good_status(uki, salara_verbose);
					} else {
					    uki = strstr(buf,"rest_server=");
					    if (uki) {
						//ast_verbose("\tread_config: rest_server:%s\n",buf);
						uks = strchr(buf,'\n'); if (uks) *uks = '\0';
						uki += 12;
						memset(rest_server,0,PATH_MAX);
						strcpy(rest_server, uki);
					    } else {
						uki = strstr(buf,"key_word=");
						if (uki) {
						    //ast_verbose("\tread_config: key_word:%s\n",buf);
						    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
						    uki += 9;
						    memset(key_word,0,PATH_MAX);
						    strcpy(key_word, uki);
						} else {
						    uki = strstr(buf,"key_operator=");
						    if (uki) {
							//ast_verbose("\tread_config: key_operator:%s\n",buf);
							uks = strchr(buf,'\n'); if (uks) *uks = '\0';
							uki += 13;
							memset(&names_rest[0][0],0,PATH_MAX);
							strcpy(&names_rest[0][0], uki);
						    } else {
							uki = strstr(buf,"key_phone=");
							if (uki) {
							    //ast_verbose("\tread_config: key_phone:%s\n",buf);
							    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
							    uki += 10;
							    memset(&names_rest[1][0],0,PATH_MAX);
							    strcpy(&names_rest[1][0], uki);
							} else {
							    uki = strstr(buf,"key_msg=");
							    if (uki) {
								//ast_verbose("\tread_config: key_msg:%s\n",buf);
								uks = strchr(buf,'\n'); if (uks) *uks = '\0';
								uki += 8;
								memset(&names_rest[2][0],0,PATH_MAX);
								strcpy(&names_rest[2][0], uki);
							    } else {
								uki = strstr(buf,"key_extension=");
								if (uki) {
								    //ast_verbose("\tread_config: key_extension:%s\n",buf);
								    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
								    uki += 14;
								    memset(&names_rest[3][0],0,PATH_MAX);
								    strcpy(&names_rest[3][0], uki);
								} else {
								    uki = strstr(buf,"key_peer=");
								    if (uki) {
									//ast_verbose("\tread_config: key_peer:%s\n",buf);
									uks = strchr(buf,'\n'); if (uks) *uks = '\0';
									uki += 9;
									memset(&names_rest[4][0],0,PATH_MAX);
									strcpy(&names_rest[4][0], uki);
								    } else {
									uki = strstr(buf,"key_channel=");
									if (uki) {
									    //ast_verbose("\tread_config: key_channel:%s\n",buf);
									    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
									    uki += 12;
									    memset(&names_rest[5][0],0,PATH_MAX);
									    strcpy(&names_rest[5][0], uki);
									} else {
									    uki = strstr(buf,"key_context=");
									    if (uki) {
										//ast_verbose("\tread_config: key_context:%s\n",buf);
										uks = strchr(buf,'\n'); if (uks) *uks = '\0';
										uki += 12;
										memset(&names_rest[6][0],0,PATH_MAX);
										strcpy(&names_rest[6][0], uki);
									    } else{
										uki = strstr(buf,"dest_url_event=");
										if (uki) {
										    //ast_verbose("\tread_config: dest_url_event:%s\n",buf);
										    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
										    uki += 15;
										    sprintf(dest_url_event,"%s",uki);
										} else {
										    uki = strstr(buf,"newexten=");
										    if (uki) {
											//ast_verbose("\tread_config: newexten:%s\n",buf);
											uks = strchr(buf,'\n'); if (uks) *uks = '\0';
											uki += 9;
											verb = atoi(uki);
											if ((verb>=0) && (verb<=3)) newexten = verb;
										    } else {
											uki = strstr(buf,"newchannel=");
											if (uki) {
											    //ast_verbose("\tread_config: newchannel:%s\n",buf);
											    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
											    uki += 11;
											    verb = atoi(uki);
											    if ((verb>=0) && (verb<=2)) newchannel = verb;
											} else {
											    uki = strstr(buf,"hangup=");
											    if (uki) {
												//ast_verbose("\tread_config: hangup:%s\n",buf);
												uks = strchr(buf,'\n'); if (uks) *uks = '\0';
												uki += 7;
												verb = atoi(uki);
												if ((verb>=0) && (verb<=2)) hangup = verb;
											    } else {
												uki = strstr(buf,"watch_makecall=");
												if (uki) {
												    //ast_verbose("\tread_config: watch_makecall:%s\n",buf);
												    uks = strchr(buf,'\n'); if (uks) *uks = '\0';
												    uki += 15;
												    w_mc = atoi(uki);
												}
											    }
											}
										    }
										}
									    }
									}
								    }
								}
							    }
							}
						    }
						}
					    }
					}
				    }
				}
			    }
			}
		    }

		}
	    }
	    //--------------------------------------
	    memset(buf, 0, PATH_MAX);
	}
	fclose(fp);
    }
    for (i=0; i<max_param_rest; i++)
	if (!strlen(&names_rest[i][0])) strcpy(&names_rest[i][0], &def_names_rest[i][0]);

    if (!strlen(key_word)) strcpy(key_word, def_key_word);
    if (!strlen(rest_server)) strcpy(rest_server, DEFAULT_SRV_ADDR);

    if (w_mc != -1) watch_makecall = w_mc;

    return len;
}
//----------------------------------------------------------------------
static int write_config(const char *file_name, int prn)
{
FILE *fp=NULL;
char path_name[PATH_MAX];
int len = 0, i;
struct timeval curtime;
s_route_record *rt=NULL, *nx=NULL;

    sprintf(path_name, "%s/%s", ast_config_AST_CONFIG_DIR, file_name);

    if (!(fp = fopen(path_name, "w"))) {
	ast_verbose("[%s %s] Write config file '%s' error: %s\n", AST_MODULE, TimeNowPrn(), file_name, strerror(errno));
	ast_log(LOG_ERROR, "[%s %s] Write config file '%s' error: %s\n", AST_MODULE, TimeNowPrn(), file_name, strerror(errno));
	return -1;
    }

#undef FORMAT_SEPARATOR_LINE
#define FORMAT_SEPARATOR_LINE \
";-------------------------------------------------------------------------------\n"

    len += fprintf(fp, FORMAT_SEPARATOR_LINE);
    len += fprintf(fp, "; file: %s\n", file_name);
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);
    gettimeofday(&curtime, NULL);
    len += fprintf(fp, "; created at: %s", ctime(&curtime.tv_sec));
    len += fprintf(fp, "; salara version: %s\n", SALARA_VERSION);
    len += fprintf(fp, "; asterisk version: %s\n", ast_get_version());
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[general]\n");
    len += fprintf(fp, "default=%s\n", dest_number);
    len += fprintf(fp, "curlopt_timeout=%d\n", SALARA_CURLOPT_TIMEOUT);
    len += fprintf(fp, "default_context=%s\n",context);
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[route]\n");
    ast_mutex_lock(&route_lock);
	rt = route_hdr.first;
	if (rt) {
	    while (rt) {
		len += fprintf(fp, "%s:%s\n",rt->caller, rt->called);
		nx = rt->next;
		rt = nx;
	    }
	} else {
	    len += fprintf(fp, ";8000:0\n");
	    len += fprintf(fp, ";8001:00\n");
	    len += fprintf(fp, ";8002:000\n");
	    len += fprintf(fp, ";8003:0000\n");
	}
    ast_mutex_unlock(&route_lock);
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[event]\n");
    len += fprintf(fp, "verbose=%d\n", salara_verbose);
    len += fprintf(fp, "watch_makecall=%d\n", watch_makecall);
    len += fprintf(fp, "hangup=%d\n", hangup);
    len += fprintf(fp, "newchannel=%d\n", newchannel);
    len += fprintf(fp,  ";0 - not used 'Hangup' and 'Newchannel' type event\n"\
			";1 - used for transfer call only\n"\
			";2 - used for all calls\n");
    len += fprintf(fp, "newexten=%d\n", newexten);
    len += fprintf(fp,  ";0 - not used 'Newexten' type event\n"\
			";1 - used for transfer call with status UP\n"\
			";2 - used for transfer call with all status\n"\
			";3 - used for all calls with all status\n");
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[url]\n");
    if (!strlen(rest_server)) strcpy(rest_server, DEFAULT_SRV_ADDR);
    len += fprintf(fp, "rest_server=%s\n", rest_server);
    len += fprintf(fp, "dest_url=%s\n", dest_url);
    len += fprintf(fp, "dest_url_event=%s\n", dest_url_event);
    len += fprintf(fp, "good_status=0,4\n");
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    if (!strlen(key_word)) strcpy(key_word, def_key_word);
    len += fprintf(fp, "[keys]\nkey_word=%s\n", key_word);
    for (i=0; i<max_param_rest; i++) {
	//names_rest[max_param_rest][PATH_MAX] = {"","","","","","",""};
	if (!strlen(&names_rest[i][0])) strcpy(&names_rest[i][0], &def_names_rest[i][0]);
	switch (i) {
	    case 0: len += fprintf(fp, "key_operator=%s\n", &names_rest[i][0]); break;
	    case 1: len += fprintf(fp, "key_phone=%s\n", &names_rest[i][0]); break;
	    case 2: len += fprintf(fp, "key_msg=%s\n", &names_rest[i][0]); break;
	    case 3: len += fprintf(fp, "key_extension=%s\n", &names_rest[i][0]); break;
	    case 4: len += fprintf(fp, "key_peer=%s\n", &names_rest[i][0]); break;
	    case 5: len += fprintf(fp, "key_channel=%s\n", &names_rest[i][0]); break;
	    case 6: len += fprintf(fp, "key_context=%s\n", &names_rest[i][0]); break;
	}
    }
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    fflush(fp);
    fclose(fp);

    if (prn) ast_verbose("\tWriting config file '%s' (%d bytes) done\n", file_name, len);

    return len;
}
//----------------------------------------------------------------------
static int check_dest(char *from, char *to)
{
int len=0, ret=-1;
s_route_record *rt=NULL;

    ast_mutex_lock(&route_lock);
	if (route_hdr.first) {
	    rt = find_record(from);
	    if (rt) {
		len = strlen(to);
		memset(to,0,len);
		strcpy(to,rt->called);
		ret=0;
	    }
	}
    ast_mutex_unlock(&route_lock);

    return ret;
}
//----------------------------------------------------------------------
static void session_instance_destructor(void *obj)
{
struct ast_tcptls_session_instance *i = obj;

    if (i->stream_cookie) {
	ao2_t_ref(i->stream_cookie, -1, "Destroying tcptls session instance");
	i->stream_cookie = NULL;
    }
    ast_free(i->overflow_buf);
#ifdef ver13
    ao2_cleanup(i->private_data);
#endif
}
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
static int MakeAction(int type, char *from, char *to, char *mess, char *ctext)
{
char buf[512]={0};
int act=0, lg = salara_verbose;

    if ((type < 0) || (type >= MAX_EVENT_TYPE)) return 0;

    ast_mutex_lock(&act_lock);
	Act_ID++;
	act = Act_ID;
    ast_mutex_unlock(&act_lock);

    add_act(act);

    switch (type) {
	case 0://outgoing call
	    if (watch_makecall) {
		sprintf(buf,"%s/%s-XXXXXXXX",StrUpr(Tech),from);
		add_chan_record(buf, from, to, NULL);
	    }
	    sprintf(buf,"Action: Originate\nChannel: %s/%s\nContext: %s\nExten: %s\nPriority: 1\n"
			"Callerid: %s\nTimeout: 10000\nActionID: %u\n\n",
			StrUpr(Tech), from, context, to, from, act);
	break;
	case 1://message send
	    sprintf(buf,"Action: MessageSend\nActionID: %u\nTo: %s:%s\nFrom: %s\nBody: %s\n\n",
		act,
		StrLwr(Tech),
		from,//TO
		to,//FROM
		mess);
	break;
	case 2://peer status
	    sprintf(buf,"Action: SIPpeerstatus\nActionID: %u\nPeer: %s\n\n", act, from);
	break;
	case 3://channel status
	    /*
	    Action: Status
	    Channel: SIP/8003-00000001
	    ActionID: 1
	    */
	    //sprintf(buf,"Action: Status\nChannel: %s\nActionID: %u\n\n", from, act);
	    sprintf(buf,"Action: Command\nCommand: core show channel %s\nActionID: %u\n\n",from,act);
	break;
/*
	case 4://exten. status
	    sprintf(buf,"Action: ExtensionState\nActionID: %u\nExten: %s\nContext: ", act, from);
	    if (strlen(ctext)) sprintf(buf+strlen(buf),"%s\n\n", ctext);
			  else sprintf(buf+strlen(buf),"%s\n\n", context);
	break;
*/
    }

    if (lg>2) {
	ast_verbose("[%s %s] MakeAction : req_type=%d action_id=%d\n", AST_MODULE, TimeNowPrn(), type, act);
	ast_verbose(buf);
	console=1;
    }
    if (strlen(buf)) msg_send(buf);
    console=0;

    return act;
}
//----------------------------------------------------------------------
static unsigned int cli_nitka(void *data)
{
int lg = salara_verbose;
struct ast_tcptls_session_instance *ser = data;
int res=0, len, dl, body_len=0;
unsigned int ret=0, aid;
char *buf=NULL;
int uk=0, loop=1, tmp=0, done=0, stat=-1, i=0, req_type=-1, rtype;
char *uki=NULL, *uks=NULL, *uke=NULL;
//const char *answer_bad = "{\"result\":-2,\"text\":\"Error\"}";
const char *answer_bad = "Error";
const char *names[max_param] = {"operator=", "phone=", "msg="};
const char *post_len = "Content-Length:";
char operator[AST_MAX_EXTENSION]={0}, phone[AST_MAX_EXTENSION]={0}, msg[AST_MAX_EXTENSION]={0}, cont[AST_MAX_EXTENSION]={0};
char ack_status[SIZE_OF_RESP<<1]={0};
char ack_text[SIZE_OF_RESP]={0};
char tp[8]={0};
unsigned char ok=0, post_flag=0, two=0;
char *ustart=NULL, *uk_body=NULL;


    buf = (char *)calloc(1,MAX_ANSWER_LEN);
    if (buf) {
	ustart = buf;

	fcntl(ser->fd, F_SETFL, (fcntl(ser->fd, F_GETFL)|O_NONBLOCK));

	for (;;) {
	    res=0;
	    uk=tmp=0;
	    loop=1;
	    memset(buf,0,MAX_ANSWER_LEN);
	    while (loop) {
		tmp = read(ser->fd, buf+uk, MAX_ANSWER_LEN-uk-1);
		if (tmp == 0) { loop=0; done=1; }
		else
		if (tmp > 0) {
		    if ((res + tmp) < MAX_ANSWER_LEN) {
			res += tmp; uk += tmp;
			uki = strstr(buf,"\r\n\r\n");
			if (uki) {
			    if (!body_len) {
				uks = strstr(buf,post_len);
				if (uks) {
				    uk_body = uki + 4;
				    uks += strlen(post_len);//uk to body_len
				    uke = strchr(uks,'\n');
				    if (uke) {
					dl = uke - uks; if (dl>=8) dl=7;
					memcpy(tp,uks,dl);
					body_len = atoi(tp);
					post_flag=1;
					//if (salara_verbose) ast_verbose("[%s %s] Post data len:%d\n", AST_MODULE, TimeNowPrn(), body_len);
				    }
				}
			    }
			    if (!post_flag) {//GET : http://localhost:5058/operator=8003&phone=000&msg="1234567"
				if ((uki+4-ustart) < MAX_ANSWER_LEN) *(uki+4) = 0;
				loop=0; done=1; i=0;
				while (i<max_param) {
				    uks = strstr(buf, names[i]);
				    if (uks) {
					uks += strlen(names[i]);
					uke = strchr(uks,'&');
					if (uke==NULL) uke = strstr(uks," HTTP");
					if (uke) {
					    len = uke-uks; if (len >= AST_MAX_EXTENSION) len=AST_MAX_EXTENSION-1;
					    switch (i) {
						case 0: memcpy(operator, uks, len); break;
						case 1:
						    memcpy(phone, uks, len);
						    req_type = 0;//make call
						break;
						case 2:
						    memcpy(msg, uks, len);
						    req_type = 1;//send message
						break;
					    }
					}
				    }
				    i++;
				}
				if (i>1) ok=1;
			    } else {//POST
				if ((uk_body + body_len) >= (buf + res)) {
				    req_type = -1;//no command
				    loop=0; done=1;
				    json_error_t err;
				    json_t *obj = json_loads(uk_body, 0, &err);
				    if (obj) {
					json_t *tobj = json_object_get(obj, &names_rest[6][0]);//"context"
					if (json_is_string(tobj)) sprintf(cont,"%s", json_string_value(tobj));
					unsigned char er=0;
					tobj = json_object_get(obj, &names_rest[0][0]);//"operator"
					if (json_is_string(tobj)) sprintf(operator,"%s", json_string_value(tobj));
					else
					if (json_is_integer(tobj)) sprintf(operator,"%lld", json_integer_value(tobj));
					else er=1;
					if (!er) {
					    tobj = json_object_get(obj, &names_rest[1][0]);//"phone"
					    if (json_is_string(tobj)) sprintf(phone,"%s", json_string_value(tobj));
					    else
					    if (json_is_integer(tobj)) sprintf(phone,"%lld", json_integer_value(tobj));
					    else er=1;
					    if (!er) {
						req_type = 0;//make call
						tobj = json_object_get(obj, &names_rest[2][0]);//"msg"
						if (json_is_string(tobj)) sprintf(msg,"%s", json_string_value(tobj));
						else er=1;
						if (!er) {
						    req_type = 1;//send message
						}
					    }
					} else {
					    er=0;
					    json_t *tobj = json_object_get(obj, &names_rest[3][0]);//"extension"
					    if (json_is_string(tobj)) sprintf(operator,"%s", json_string_value(tobj));
					    else
					    if (json_is_integer(tobj)) sprintf(operator,"%lld", json_integer_value(tobj));
					    else er=1;
					    if (!er) {
						req_type = 4;//get status exten
					    } else {
						er=0;
						json_t *tobj = json_object_get(obj, &names_rest[4][0]);//"peer"
						if (json_is_string(tobj)) sprintf(operator,"%s", json_string_value(tobj));
						else
						if (json_is_integer(tobj)) sprintf(operator,"%lld", json_integer_value(tobj));
						else er=1;
						if (!er) {
						    req_type = 2;//get status peer
						} else {
						    er=0;
						    json_t *tobj = json_object_get(obj, &names_rest[5][0]);//"channel"
						    if (json_is_string(tobj)) {
							sprintf(operator,"%s", json_string_value(tobj));
							req_type = 3;//get status channel
						    } else er=1;
						}
					    }
					}
					json_decref(obj); obj=NULL;
					ok = ~er;
				    } else {//not json format
					i=0;
					while (i<max_param) {
					    uks = strstr(buf, names[i]);
					    if (uks) {
						uks += strlen(names[i]);
						uke = strchr(uks,'&');
						if (uke==NULL) uke = strchr(uks,'\0');
						if (uke) {
						    len = uke-uks; if (len >= AST_MAX_EXTENSION) len=AST_MAX_EXTENSION-1;
						    switch (i) {
							case 0: memcpy(operator, uks, len); break;
							case 1:
							    memcpy(phone, uks, len);
							    req_type = 0;//make call
							break;
							case 2:
							    memcpy(msg, uks, len);
							    req_type = 1;//send message
							break;
						    }
						}
					    }
					    i++;
					}
					if (i>1) ok=1;
				    }
				    //if (salara_verbose) ast_verbose("[%s %s] Post data:%s\n", AST_MODULE, TimeNowPrn(), uk_body);
				}
			    }
			}
		    } else loop=0;
		}//if (tmp >0....
	    }//while (loop)

	    if (ok) {
		if (lg) {
		    rtype = req_type;
		    if (rtype >= MAX_ACT_TYPE) rtype=MAX_ACT_TYPE-1;
		    ast_verbose("[%s %s] Action type '%s' (%d) with param : operator='%s' phone='%s' msg='%s' context='%s'\n",
				AST_MODULE, TimeNowPrn(),
				ActType[rtype],
				rtype,
				operator,
				phone,
				msg,
				cont);
		}
		//------------------------------------------------------
		two=0;
		memset(ack_text,0,SIZE_OF_RESP);
		//"Make call","Send message", "Get status peer", "Get status channel", "Get status exten", "Unknown"
		switch (req_type) {
		    case 0://"Make call"
		    case 1://"Send message"
		    case 4://"Get status exten"
			stat = ast_extension_state(NULL, cont, operator);
			strcpy(ack_text, ast_extension_state2str(stat));
			if (req_type==4) done=1;
			else {
			    if (!check_stat(stat)) two++;
			    if (lg>1) ast_verbose("[%s %s] Dest '%s' status (%d)\n", AST_MODULE, TimeNowPrn(), operator, stat);
			}
		    break;
		    case 2://"Get status peer"
		    case 3://"Get status channel"
			aid = MakeAction(req_type,operator,phone,msg,cont);//get status peer, channel
			usleep(2000);
			if (aid>0) {
			    s_act_list *abc = find_act(aid);
			    if (abc) {
				stat = abc->act->status;
				strcpy(ack_text, abc->act->resp);
				delete_act(abc,1);
			    }
			} else {
			    sprintf(ack_text,"%s", answer_bad);
			}
			done=1;
		    break;
			default : { sprintf(ack_text,"%s", answer_bad); stat=-2; }
		}//switch(req_type)
		sprintf(ack_status,"{\"result\":%d,\"text\":\"%s\"}", stat, ack_text);
		write(ser->fd, ack_status, strlen(ack_status));
		if (lg) ast_verbose("[%s %s] Send answer '%s' to rest client\n", AST_MODULE, TimeNowPrn(), ack_status);
	    } else {
		write(ser->fd, answer_bad, strlen(answer_bad));
		if (lg) ast_verbose("[%s %s] Error request parser\n", AST_MODULE, TimeNowPrn());
	    }

	    if ((res>0) && !ok && lg) ast_verbose("%s\n", buf);
	    else if (lg>2) ast_verbose("[%s %s] Data from rest client :\n%s\n", AST_MODULE, TimeNowPrn(), buf);

	    if (done) break;

	    if ((errno != EINTR) && (errno != EAGAIN) && (errno != 0)) {
		if (lg) ast_verbose("[%s %s] Socket error (%d) reading data: '%s'\n", AST_MODULE, TimeNowPrn(), errno, strerror(errno));
		done=1;
	    }

	}//for (;;)

    } else if (lg) ast_verbose("[%s %s] Error calloc in cli_nitka (sock=%d)\n", AST_MODULE, TimeNowPrn(), ser->fd);


    if (ser->fd > 0) {
	if (lg>2) ast_verbose("[%s %s] Close client socket %d\n", AST_MODULE, TimeNowPrn(), ser->fd);
	shutdown(ser->fd, SHUT_RDWR);
	close(ser->fd);
    }

    if (buf) free(buf);


    if ((ok) && (two) && (req_type < 2)) ret = MakeAction(req_type, operator, phone, msg, cont);

    return ret;
}
//----------------------------------------------------------------------
static void *cli_rest_open(void *data)
{
int lg = salara_verbose;
struct ast_tcptls_session_instance *tcptls_session = data;

    if (lg>1) ast_verbose("[%s %s] cli_rest_open : connection from client '%s' (adr=%p sock=%d)\n",
		AST_MODULE, TimeNowPrn(),
		ast_sockaddr_stringify(&tcptls_session->remote_address),
		(void *)data,
		tcptls_session->fd);

    tcptls_session->client = cli_nitka(tcptls_session);//ActionID -> to cli_para()

    return tcptls_session->parent->worker_fn(tcptls_session);


}
//----------------------------------------------------------------------
static void *srv_nitka(void *data)
{
struct ast_tcptls_session_args *desc = data;
int fd, i;
struct ast_sockaddr addr;
struct ast_tcptls_session_instance *tcptls_session;
pthread_t launched;


/*    ast_verbose("[%s %s] srv_nitka listen port %d (sock=%d)\n",
		AST_MODULE, TimeNowPrn(), ast_sockaddr_port(&desc->local_address), desc->accept_fd);*/

    for (;;) {

	if (desc->periodic_fn) desc->periodic_fn(desc);

	i = ast_wait_for_input(desc->accept_fd, desc->poll_timeout);
	if (i <= 0) continue;

	fd = ast_accept(desc->accept_fd, &addr);
	if (fd < 0) {
	    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR) && (errno != ECONNABORTED)) {
		ast_verbose("[%s %s] Accept failed: %s\n", AST_MODULE, TimeNowPrn(), strerror(errno));
		break;
	    }
	    continue;
	}// else ast_verbose("[%s %s] Accept cli_sock=%d\n", AST_MODULE, TimeNowPrn(), fd);
	tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor);
	if (!tcptls_session) {
	    ast_verbose("[%s %s] No memory for new session: %s\n", AST_MODULE, TimeNowPrn(), strerror(errno));
	    if (close(fd)) ast_verbose("[%s %s] close() failed: %s\n", AST_MODULE, TimeNowPrn(), strerror(errno));
	    continue;
	}

	tcptls_session->overflow_buf = ast_str_create(128);
	fcntl(fd, F_SETFL, (fcntl(fd, F_GETFL)) & ~O_NONBLOCK);
	tcptls_session->fd = fd;
	tcptls_session->parent = desc;
	ast_sockaddr_copy(&tcptls_session->remote_address, &addr);

	tcptls_session->client = 0;

	if (ast_pthread_create_detached_background(&launched, NULL, cli_rest_open, tcptls_session)) {
	    ast_verbose("[%s %s] Unable to launch helper thread: %s\n", AST_MODULE, TimeNowPrn(), strerror(errno));
	    ao2_ref(tcptls_session, -1);
	}// else ast_verbose("[%s %s] srv_nitka : Thread start for client '%s'\n", AST_MODULE, TimeNowPrn(), ast_sockaddr_stringify(&addr));
    }

    return NULL;
}
//----------------------------------------------------------------------
static int send_curl_event(char *url, char *body, int wait, char *str, int str_len, CURLcode *err, int crt, int js, int prn)
{
int ret=-1, dl;
CURL *curl;
CURLcode res = CURLE_OK;
struct MemoryStruct chunk;
const char *uke=NULL;

    if ((!str) || (!str_len)) return ret;

    chunk.memory = (char *)malloc(1);
    chunk.size = 0;

    curl = curl_easy_init();
    if (curl) {
	curl_easy_setopt(curl, CURLOPT_URL, url);
	struct curl_slist *headers=NULL;
	//headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	if (js) headers = curl_slist_append(headers, "Content-Type: application/json");
	   else headers = curl_slist_append(headers, "Content-Type: text/plain");
	if (body) {//POST with json
	    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
	    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body);
	    //curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	}
	if (crt) {//disable ssl certificates
	    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WrMemEventCallBackFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, global_useragent);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, wait);
	res = curl_easy_perform(curl);
	*err = res;
	if (res == CURLE_OK) {
	    dl = chunk.size;
	    if (dl >= str_len) dl = str_len-1;
	    memcpy(str, (char *)chunk.memory, dl);
	    //ret = CheckCurlEventAnswer((char *)chunk.memory, str);
	    if (prn) ast_verbose("[%s %s] Curl_event answer :%.*s\n", AST_MODULE, TimeNowPrn(), chunk.size, (char *)chunk.memory);
	    ret=0;
	} else {
	    uke = curl_easy_strerror(*err);
	    dl = strlen(uke);
	    if (dl >= str_len) dl = str_len-1;
	    if (prn) ast_verbose("[%s %s] Curl_event Error : '%s' url=%s\n", AST_MODULE, TimeNowPrn(), uke, dest_url_event);
	    memcpy(str, uke, dl);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(chunk.memory);

    }

    return ret;
}
//----------------------------------------------------------------------
static int send_to_crm(s_chan_event *evt)
{
int ret=-1, tp, ts, ssl=0, lg=salara_verbose;
CURLcode err;
json_t *js=NULL;
char *jbody=NULL;
int buf_len=MAX_ANSWER_LEN;
char buf[MAX_ANSWER_LEN]={0};

    if (!evt) return ret;

    js = json_object(); if (!js) return ret;

    tp = evt->type; if (tp >= MAX_EVENT_NAME) tp = MAX_EVENT_NAME-1;
    ts = evt->state; if (ts >= MAX_CHAN_STATE) ts = MAX_CHAN_STATE-1;
    //json_object_set_new(js,"type", json_integer((json_int_t)tp));
    json_object_set_new(js,"name", json_string(EventName[tp]));
    if (evt->chan) json_object_set_new(js,"chan", json_string(evt->chan));
    if (evt->caller) json_object_set_new(js,"caller", json_string(evt->caller));
    if (evt->exten) json_object_set_new(js,"exten", json_string(evt->exten));
    json_object_set_new(js,"state", json_string(ChanStateName[ts]));
    if (evt->app) {
	if (strlen(evt->app)) json_object_set_new(js,"app", json_string(evt->app));
    }

    jbody = json_dumps(js, JSON_COMPACT);
    if (jbody) {
	if (strstr(dest_url_event,"https:")) ssl=1;
	ret = send_curl_event(dest_url_event, jbody, SALARA_CURLOPT_TIMEOUT, buf, buf_len, &err, ssl, 1, 0);//&err, ssl, json, lg
	
    }

    if (lg) ast_verbose("[%s %s] Send_event_to '%s'\n\tpost=%s\n\tanswer=%s\n",
			AST_MODULE,
			TimeNowPrn(),
			dest_url_event, jbody, buf);

    //if (jbody) free(jbody);
    json_decref(js);

    rm_chan_event(evt);

    return 0;
}
//----------------------------------------------------------------------
static void *send_by_event(void *arg)
{
s_event_list *rec=NULL;
s_chan_event *evt=NULL;
pthread_t tid = pthread_self();
int loop=1, tp, rsa=1, cnt, ts, lg;

    ast_verbose("[%s %s] SEND_BY_EVENT Thread started (tid:%lu).\n", AST_MODULE, TimeNowPrn(), tid);

    start_http_nitka=1;

    while ((loop) && (!stop_http_nitka)) {

	lg = salara_verbose;

	usleep(1000);//10000

	//ast_mutex_lock(&event_lock);
	rsa = ast_mutex_trylock(&event_lock);
	if (rsa) {
	    //ast_verbose("[%s %s] send_by_event thread : trylock not success !!!\n", AST_MODULE, TimeNowPrn());
	    if (stop_http_nitka) break;
	    else continue;
	} //else ast_verbose("[%s %s] send_by_event thread : Lock success !!!\n", AST_MODULE, TimeNowPrn());

	if (event_hdr.counter>0) {
	    rec = event_hdr.first;
	    cnt = event_hdr.counter;
	    if (rec->event) {
		evt = make_chan_event(	rec->event->type,
					rec->event->chan,
					rec->event->caller,
					rec->event->exten,
					rec->event->state,
					rec->event->app);
		if (evt) {
		    del_event_list(rec, 0);//delete event_record from list
		    ast_mutex_unlock(&event_lock); rsa=1;
		    //ast_verbose("[%s %s] send_by_event thread : Unlock success !!!\n", AST_MODULE, TimeNowPrn());
		    if (lg>2) {//>2
			tp = evt->type; if (tp >= MAX_EVENT_NAME) tp = MAX_EVENT_NAME-1;
			ts = evt->state; if (ts >= MAX_CHAN_STATE) ts = MAX_CHAN_STATE-1;
			ast_verbose("[%s %s] SEND_BY_EVENT Thread (%d): type(%d)='%s' chan='%s' exten='%s' caller='%s' state(%d)='%s' app='%s'\n",
					AST_MODULE, TimeNowPrn(), cnt,
					tp, EventName[tp], evt->chan, evt->exten, evt->caller, ts, ChanStateName[ts], evt->app);
		    }
		    send_to_crm(evt);
		} else {
		    if (lg) ast_verbose("[%s %s] SEND_BY_EVENT Thread : record not found in del_event_list\n", AST_MODULE, TimeNowPrn());
		}
	    }
	}
	if (!rsa) {
	    ast_mutex_unlock(&event_lock); rsa=1;
	    //ast_verbose("[%s %s] send_by_event thread : Unlock success !!!\n", AST_MODULE, TimeNowPrn());
	}

	ast_mutex_unlock(&event_lock);

	if (stop_http_nitka) break;

    }//while loop

    start_http_nitka=0;

    ast_verbose("[%s %s] SEND_BY_EVENT Thread stoped (tid:%lu).\n", AST_MODULE, TimeNowPrn(), tid);

    pthread_exit(NULL);
}
//----------------------------------------------------------------------
static void *cli_rest_close(void *data)
{
int lg = salara_verbose;
struct ast_tcptls_session_instance *t_s = data;
s_act_list *abc = NULL;

    if (lg>1) ast_verbose("[%s %s] cli_rest_close : act_id=%d, release session (adr=%p sock=%d)\n",
			AST_MODULE, TimeNowPrn(), (unsigned int)t_s->client, (void *)data, t_s->fd);

    if (t_s->client>0) {
	abc = find_act(t_s->client);
	if (abc) delete_act(abc,1);
    }

    ao2_ref(t_s, -1);//release session

    return NULL;
}
//----------------------------------------------------------------------
static void periodics(void *data)
{
//struct ast_tcptls_session_args *desc = data;
int cnt, stat=100, lg = salara_verbose;
struct ast_channel *ast=NULL;
s_chan_record *tmp=NULL, *rec=NULL;
char *name="";

    if (ast_mutex_trylock(&chan_lock)) {
	if (lg) ast_verbose("[%s %s] Periodics : trylock not success !!!\n", AST_MODULE, TimeNowPrn());
	return;
    }

    cnt = chan_hdr.counter;
    if (cnt>0) {
	rec = chan_hdr.first;
	while ((rec) && (cnt>0)) {
	    ast = (struct ast_channel *)rec->ast;
	    if (ast) {
		//ast_channel_lock(ast);//!!!
		stat = ast_channel_state(ast);
		name = strdupa(ast_channel_name(ast));
		//ast_channel_unlock(ast);//!!!
		if (stat > MAX_CHAN_STATE-1) stat=MAX_CHAN_STATE-1;
		if (lg) ast_verbose("[%s %s] Periodics : total=%d channel=[%s] status=%d (%s) ast=%p\n",
					AST_MODULE, TimeNowPrn(), cnt, name, stat, ChanStateName[stat], (void *)ast);
		if ((!strlen(name)) && (dirty)) {
		    del_chan_record(rec, 0);
		}
	    }
	    tmp = rec->next;
	    rec = tmp;
	    cnt--;
	}
    }
    ast_mutex_unlock(&chan_lock);

}
//----------------------------------------------------------------------
static struct ast_tcptls_session_args sami_desc = {
    .accept_fd = -1,
    .master = AST_PTHREADT_NULL,
    .tls_cfg = NULL,
    .poll_timeout = 10000,	// wake up every 5 seconds
    .periodic_fn = NULL,//periodics,//purge_old_stuff,
    .name = "Salara server",
    .accept_fn = srv_nitka,	//tcptls_server_root,	// thread doing the accept()
    .worker_fn = cli_rest_close,//session_do,	// thread handling the session
};
//----------------------------------------------------------------------
static int salara_cleanup(void)
{

    if (!reload) stop_http_nitka=1;

    curl_global_cleanup();

    ast_mutex_lock(&salara_lock);

	delete_act_list();

	remove_records();

	if (salara_cli_registered) ast_cli_unregister_multiple(cli_salara, ARRAY_LEN(cli_salara));

	if (salara_manager_registered) ast_manager_unregister_hook(&hook);

	if (salara_app_registered) ast_unregister_application(app_salara);

	if (salara_atexit_registered) ast_unregister_atexit(salara_atexit);

	    //-------   server   --------------------------------------------
	if (!reload) {
	    ast_tcptls_server_stop(&sami_desc);
	    //----------------------------------------------------------------
	    if (http_tid != AST_PTHREADT_NULL) {
		stop_http_nitka=1;
		usleep(200000);
		pthread_cancel(http_tid);
		pthread_kill(http_tid, SIGURG);
		pthread_join(http_tid, NULL);
		pthread_attr_destroy(&threadAttr);
		start_http_nitka=0;
		remove_event_list();
		remove_chan_records();
	    }
	}

    ast_mutex_unlock(&salara_lock);

//    ast_verbose("\t[v%s] Module '%s.so' unloaded OK.\n",SALARA_VERSION, AST_MODULE);

    start_http_nitka=0;

    return 0;
}
//----------------------------------------------------------------------
static void salara_atexit(void)
{
    salara_cleanup();
}
//----------------------------------------------------------------------
static int load_module(void)
{
int lenc = -1;
struct ast_sockaddr sami_desc_local_address_tmp;

//    ast_verbose("[%s %s] Load for * version %s\n",AST_MODULE,TimeNowPrn(),ast_get_version_num());

    unload=0;

    if (!reload) start_http_nitka=0;

    curl_global_init(CURL_GLOBAL_ALL);

    gettimeofday(&salara_start_time, NULL);

    memset(dest_number,0,AST_MAX_EXTENSION);
    strcpy(dest_number, DEF_DEST_NUMBER);

    ast_mutex_lock(&salara_lock);

	ast_mutex_lock(&act_lock);
	    Act_ID = 0;
	ast_mutex_unlock(&act_lock);

	init_act_list();

	init_records();

	if (ast_register_atexit(salara_atexit) < 0) {
	    salara_atexit_registered = 0;
	    ast_verbose("[%s %s] Unable to register ATEXIT\n",AST_MODULE,TimeNowPrn());
	} else salara_atexit_registered = 1;

	memset(rest_server,0,PATH_MAX);
	lenc = read_config(salara_config_file, 0);// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	if (lenc <= 0) lenc = write_config(salara_config_file, 1);
	salara_config_file_len = lenc;
	//if (!strlen(rest_server)) strcpy(rest_server, DEFAULT_SRV_ADDR);

	if (ast_register_application(app_salara, app_salara_exec, app_salara_synopsys, app_salara_description) < 0) {
	    salara_app_registered = 0;
	    ast_verbose("[%s %s] Unable to register APP\n",AST_MODULE,TimeNowPrn());
	} else salara_app_registered = 1;


	salara_manager_registered = 0; ast_manager_unregister_hook(&hook);
	ast_manager_register_hook(&hook); salara_manager_registered = 1;

	ast_cli_register_multiple(cli_salara, ARRAY_LEN(cli_salara));
	salara_cli_registered = 1;

	if (!reload) {
	    //-------   server   --------------------------
	    ast_sockaddr_setnull(&sami_desc.local_address);
	    ast_sockaddr_parse(&sami_desc_local_address_tmp, rest_server, 0);
	    ast_sockaddr_copy(&sami_desc.local_address, &sami_desc_local_address_tmp);
	    ast_tcptls_server_start(&sami_desc);
	    //---------------------------------------------
	    if (!start_http_nitka) {
		init_chan_records();
		init_event_list();
		stop_http_nitka=0;
		//if (ast_pthread_create_detached_background(&http_tid, NULL, send_by_event, &event_hdr))
		pthread_attr_init(&threadAttr);
		pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
		if (pthread_create(&http_tid, &threadAttr, send_by_event, &event_hdr)) {
		    ast_verbose("[%s %s] Unable to launch http client thread: %s\n", AST_MODULE, TimeNowPrn(), strerror(errno));
		    start_http_nitka=0;
		}
	    }
	}

    ast_mutex_unlock(&salara_lock);

    if (reload) reload=0;

//    ast_verbose("\t[v%s %s] Module '%s.so' loaded OK.\n", SALARA_VERSION, AST_MODULE, TimeNowPrn());//, (int)salara_pid);

    return AST_MODULE_LOAD_SUCCESS;
}
//----------------------------------------------------------------------
static int unload_module(void)
{
    unload = 1;
    return (salara_cleanup());
}
//----------------------------------------------------------------------
static int reload_module(void)
{
    reload=1;

    if (unload_module() == 0) return (load_module());
    else {
#ifdef ver13
	return AST_MODULE_RELOAD_ERROR;
#else
	return AST_MODULE_LOAD_FAILURE;
#endif
    }
}
//----------------------------------------------------------------------

AST_MODULE_INFO(
    ASTERISK_GPL_KEY,
    AST_MODFLAG_LOAD_ORDER,
    AST_MODULE_DESC,
#ifdef ver13
    .support_level = AST_MODULE_SUPPORT_EXTENDED,
#endif
    .load = load_module,
    .unload = unload_module,
    .reload = reload_module,
);

