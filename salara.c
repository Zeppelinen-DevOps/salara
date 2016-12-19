#include <ctype.h>
#include <netdb.h>
#include <curl/curl.h>
#include <jansson.h>

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
#include "asterisk/format_cache.h"


#undef DO_SSL

#define ver13

#undef CURLs


#define TIME_STR_LEN 128
#define AST_MODULE "salara"
#define AST_MODULE_DESC "Features : transfer call; make call; get status: exten., peer, channel; send [command, message]"
#define DEF_DEST_NUMBER "1234"
#define SALARA_VERSION "2.5"//18.12.2016
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

#define max_buf_size 2048

#define MAX_CHAN_STATE 11
//------------------------------------------------------------------------

ASTERISK_FILE_VERSION(__FILE__, "$Revision: $");

//------------------------------------------------------------------------
enum bool {
    false = 0,
    true = 1
};

struct MemoryStruct {
  char *memory;
  size_t size;
};

/*
typedef struct {
    char *body;
    int len;
    int part;
    int id;
} s_resp_list;
*/

//------------------  for List of channel_name  -----------------------
typedef struct self {
    struct self * before;
    struct self * next;
    char *chan;//[AST_CHANNEL_NAME];
    char *exten;//[AST_MAX_EXTENSION];
    char *caller;//[AST_MAX_EXTENSION];
    void *ast;
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

static s_chan_hdr chan_hdr = {NULL,NULL,0};

static s_act_hdr act_hdr = {NULL,NULL,0};

static char hook_tmp_str[max_buf_size]={0};

static int reload=0;
static unsigned char console = 0;
static unsigned char dirty=1;//clear lost chan_records

static int salara_atexit_registered = 0;
static int salara_cli_registered = 0;
static int salara_manager_registered = 0;
static int salara_app_registered = 0;

static char *app_salara = "salara";
static char *app_salara_synopsys = "RouteSalara";
static char *app_salara_description = "Salara transfer call function";

static char dest_number[AST_MAX_EXTENSION] = "00";

static char dest_url[PATH_MAX] = "https://localhost:3000/call_center/incoming_call/check_is_org_only?phone=";

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

static char *ActType[MAX_ACT_TYPE] = {"Make call","Send message","Get status exten", "Get status peer", "Get status channel", "Unknown"};

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

AST_MUTEX_DEFINE_STATIC(salara_lock);//global mutex

AST_MUTEX_DEFINE_STATIC(resp_event_lock);//event_list mutex

AST_MUTEX_DEFINE_STATIC(route_lock);//route_table mutex

AST_MUTEX_DEFINE_STATIC(act_lock);//actionID mutex

AST_MUTEX_DEFINE_STATIC(status_lock);//actionID mutex

AST_MUTEX_DEFINE_STATIC(chan_lock);//chan_list mutex

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
//static void *get_chan_record(s_chan_record *rec, int withlock);
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

	if (lg) {//>2
	    if (lg>2) ast_verbose("[%s] add_chan : first=%p end=%p counter=%d (chan='%s' ext='%s' caller='%s' ast=%p)\n",
			AST_MODULE, (void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller, data);
	    if (ret) ast_verbose("[%s] add_chan : rec=%p before=%p next=%p chan='%s' ext='%s' caller='%s' ast=%p\n",
			AST_MODULE, (void *)ret, (void *)ret->before, (void *)ret->next,
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

	if (lg) {//>2
	    ast_verbose("[%s %s] del_chan : rec=%p first=%p end=%p counter=%d\n",
			AST_MODULE,
			TimeNowPrn(),
			(void *)rcd,
			(void *)chan_hdr.first,
			(void *)chan_hdr.end,
			chan_hdr.counter);
	}

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
		if ( (strcmp(tmp->chan, nchan)) && (!strcmp(tmp->exten, ext)) && (!strcmp(tmp->caller, caller)) ) {
		    if (tmp->chan) free(tmp->chan); tmp->chan=NULL;
		    nc = (char *)calloc(1, strlen(nchan) + 1);
		    if (nc) {
			strcat(nc, nchan);
			tmp->chan = nc;
		    }
		    ret = tmp;
		    break;
		} else {
		    temp = tmp->next;
		    tmp = temp;
		}
	    }
	}

	if (lg) {//>2
	    if (ret) ast_verbose("[%s] update_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s' ast=%p, record found %p\n",
				AST_MODULE,
				(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
				nchan, ext, caller, ret->ast,
				(void *)ret);
	    else
		if (lg>1) ast_verbose("[%s] update_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s', record not found\n",
				AST_MODULE,
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

	if (lg>2) {//>2
	    if (ret)
		ast_verbose("[%s] find_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s' ast=%p, record found %p\n",
			AST_MODULE,
			(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller, ret->ast,
			(void *)ret);
	    else
		ast_verbose("[%s] find_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s', record not found\n",
			AST_MODULE,
			(void *)chan_hdr.first, (void *)chan_hdr.end, chan_hdr.counter,
			nchan, ext, caller);
	}
	if (ret) {
	    if (ret->ast) {
		//stat = ast_channel_state((struct ast_channel *)ret->ast); if (stat > MAX_CHAN_STATE-1) stat=MAX_CHAN_STATE-1;
		if (lg) ast_verbose("[%s %s] find_chan : chan=[%s] exten=[%s] caller=[%s] ast=%p\n",
			AST_MODULE,
			TimeNowPrn(),
			nchan,
			//ast_channel_name((struct ast_channel *)ret->ast),
			//stat,
			//ChanStateName[stat],
			ext,
			caller,
			(void *)ret->ast);
		//if ( (ast_strlen_zero(ast_channel_name(ret->ast))) ||
		//	(!stat) ||
		//	    (stat==7) ) 
		//del_chan_record(ret, 0);
	    } else {
		if (lg) ast_verbose("[%s %s] find_chan : record found at %p, but ast=NULL -> delete record !\n",
			AST_MODULE, TimeNowPrn(), (void *)ret);
		//del_chan_record(ret, 0);
	    }
	    if (with_del) del_chan_record(ret, 0);
	}

    ast_mutex_unlock(&chan_lock);

    return ret;
}
//------------------------------------------------------------------------
/*static s_chan_record *find_chan_record(const char *nchan, const char *caller, const char *ext)
{
int lg;
s_chan_record *ret=NULL, *temp=NULL, *tmp=NULL;

    if ((!nchan) || (!ext)) return ret;

    lg = salara_verbose;

    ast_mutex_lock(&chan_lock);

	if (chan_hdr.first) {
	    tmp = chan_hdr.first;
	    while (tmp != NULL) {
		if ( (!strcmp(tmp->chan, nchan)) && (!strcmp(tmp->exten, ext)) && (!strcmp(tmp->caller, caller)) ) {
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
		ast_verbose("[%s] find_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s' %p, record found %p\n",
			AST_MODULE,
			(void *)chan_hdr.first,
			(void *)chan_hdr.end,
			chan_hdr.counter,
			ret->chan, ret->exten, ret->caller, ret->ast,
			(void *)ret);
	    else
		ast_verbose("[%s] find_chan : first=%p end=%p counter=%d chan='%s' exten='%s' caller='%s', record not found\n",
			AST_MODULE,
			(void *)chan_hdr.first,
			(void *)chan_hdr.end,
			chan_hdr.counter,
			nchan, ext, caller);
	}

    ast_mutex_unlock(&chan_lock);

    return ret;
}*/
//------------------------------------------------------------------------
/*
static struct ast_channel *get_chan_record(s_chan_record *rec, int withlock)
{
struct ast_channel *ret=NULL;

    if (!rec) return ret;

    if (withlock) ast_mutex_lock(&chan_lock);
	ret = rec->ast;
    if (withlock) ast_mutex_unlock(&chan_lock);

    return ret;
}
*/
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
	    ast_verbose("[%s] delete_act : first=%p end=%p counter=%u\n",
			AST_MODULE,
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
		if (lg>=2)//>=2
		    ast_verbose("[%s] update_act_by_index : adr=%p act_id=%u status=%d resp='%s'\n",
			AST_MODULE,
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
	    if (lg>=2)//>=2
		ast_verbose("[%s] update_act : adr=%p ind=%u status=%d resp='%s'\n",
			AST_MODULE,
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
		ast_verbose("[%s] find_act : first=%p end=%p counter=%u\n"
			    "\t-- rec=%p before=%p next=%p ind=%u status=%d resp='%s'\n",
			AST_MODULE,
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
		ast_verbose("[%s] find_act : first=%p end=%p counter=%u, record with ind=%u not found\n",
			AST_MODULE,
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
	    ast_verbose("[%s] add_act : first=%p end=%p counter=%u\n",
			AST_MODULE, 
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
	    ast_verbose("[%s] add_record : first=%p end=%p counter=%d (from='%s' to='%s')\n",
			AST_MODULE, 
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
		ast_verbose("[%s] find_record : first=%p end=%p counter=%d from='%s', record found %p\n",
			AST_MODULE,
			(void *)route_hdr.first,
			(void *)route_hdr.end,
			route_hdr.counter,
			from,
			(void *)ret);
	    else
		ast_verbose("[%s] find_record : first=%p end=%p counter=%d from='%s', record not found\n",
			AST_MODULE,
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
	    ast_verbose("[%s] del_record : first=%p end=%p counter=%d\n",
			AST_MODULE,
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
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
static const char * const global_useragent = "libcurl-agent/1.0";

#ifdef CURLs

#define CURLVERSION_ATLEAST(a,b,c) \
	((LIBCURL_VERSION_MAJOR > (a)) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR > (b))) || ((LIBCURL_VERSION_MAJOR == (a)) && (LIBCURL_VERSION_MINOR == (b)) && (LIBCURL_VERSION_PATCH >= (c))))

#define CURLOPT_SPECIAL_HASHCOMPAT ((CURLoption) -500)
//-----------------------------------------------
static void curlds_free(void *data);
//-----------------------------------------------
static const struct ast_datastore_info curl_info = {
    .type = "CURLs",
    .destroy = curlds_free,
};
//-----------------------------------------------
struct curl_settings {
    AST_LIST_ENTRY(curl_settings) list;
    CURLoption key;
    void *value;
};
//-----------------------------------------------
AST_LIST_HEAD_STATIC(global_curl_info, curl_settings);
//-----------------------------------------------
static void curlds_free(void *data)
{
AST_LIST_HEAD(global_curl_info, curl_settings) *list = data;
struct curl_settings *setting;

    if (!list) {
	return;
    }
    while ((setting = AST_LIST_REMOVE_HEAD(list, list))) {
	free(setting);
    }
    AST_LIST_HEAD_DESTROY(list);
    ast_free(list);
}
//-----------------------------------------------
enum optiontype {
	OT_BOOLEAN,
	OT_INTEGER,
	OT_INTEGER_MS,
	OT_STRING,
	OT_ENUM,
};
//-----------------------------------------------
enum hashcompat {
	HASHCOMPAT_NO = 0,
	HASHCOMPAT_YES,
	HASHCOMPAT_LEGACY,
};
//-----------------------------------------------
static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
    register int realsize = size * nmemb;
    struct ast_str **pstr = (struct ast_str **)data;

    ast_debug(3, "Called with data=%p, str=%p, realsize=%d, len=%zu, used=%zu\n", data, *pstr, realsize, ast_str_size(*pstr), ast_str_strlen(*pstr));
    //ast_verbose("CallBack: Called with data=%p, str=%p, realsize=%d, len=%zu, used=%zu\n", data, *pstr, realsize, ast_str_size(*pstr), ast_str_strlen(*pstr));
    ast_str_append_substr(pstr, 0, ptr, realsize);
    //ast_verbose("CallBack: Now, len=%zu, used=%zu\n", ast_str_size(*pstr), ast_str_strlen(*pstr));
    ast_debug(3, "Now, len=%zu, used=%zu\n", ast_str_size(*pstr), ast_str_strlen(*pstr));

    return realsize;
}
//-----------------------------------------------
static int curl_instance_init(void *data)
{
    CURL **curl = data;
    if (!(*curl = curl_easy_init())) return -1;
    curl_easy_setopt(*curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(*curl, CURLOPT_TIMEOUT, SALARA_CURLOPT_TIMEOUT);//180
    curl_easy_setopt(*curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(*curl, CURLOPT_USERAGENT, global_useragent);

    return 0;
}
//-----------------------------------------------
static void curl_instance_cleanup(void *data)
{
    CURL **curl = data;
    curl_easy_cleanup(*curl);
    ast_free(data);
}
//-----------------------------------------------
AST_THREADSTORAGE_CUSTOM(curl_instance, curl_instance_init, curl_instance_cleanup);
AST_THREADSTORAGE(thread_escapebuf);
//-----------------------------------------------
static int url_is_salara(const char *url)
{
    if (strpbrk(url, "\r\n")) return 1;

    return 0;
}
//-----------------------------------------------
static int parse_curlopt_key(const char *name, CURLoption *key, enum optiontype *ot)
{
	if (!strcasecmp(name, "header")) {
		*key = CURLOPT_HEADER;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "proxy")) {
		*key = CURLOPT_PROXY;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "proxyport")) {
		*key = CURLOPT_PROXYPORT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "proxytype")) {
		*key = CURLOPT_PROXYTYPE;
		*ot = OT_ENUM;
	} else if (!strcasecmp(name, "dnstimeout")) {
		*key = CURLOPT_DNS_CACHE_TIMEOUT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "userpwd")) {
		*key = CURLOPT_USERPWD;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "proxyuserpwd")) {
		*key = CURLOPT_PROXYUSERPWD;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "maxredirs")) {
		*key = CURLOPT_MAXREDIRS;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "referer")) {
		*key = CURLOPT_REFERER;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "useragent")) {
		*key = CURLOPT_USERAGENT;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "cookie")) {
		*key = CURLOPT_COOKIE;
		*ot = OT_STRING;
	} else if (!strcasecmp(name, "ftptimeout")) {
		*key = CURLOPT_FTP_RESPONSE_TIMEOUT;
		*ot = OT_INTEGER;
	} else if (!strcasecmp(name, "httptimeout")) {
#if CURLVERSION_ATLEAST(7,16,2)
		*key = CURLOPT_TIMEOUT_MS;
		*ot = OT_INTEGER_MS;
#else
		*key = CURLOPT_TIMEOUT;
		*ot = OT_INTEGER;
#endif
	} else if (!strcasecmp(name, "conntimeout")) {
#if CURLVERSION_ATLEAST(7,16,2)
		*key = CURLOPT_CONNECTTIMEOUT_MS;
		*ot = OT_INTEGER_MS;
#else
		*key = CURLOPT_CONNECTTIMEOUT;
		*ot = OT_INTEGER;
#endif
	} else if (!strcasecmp(name, "ftptext")) {
		*key = CURLOPT_TRANSFERTEXT;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "ssl_verifypeer")) {
		*key = CURLOPT_SSL_VERIFYPEER;
		*ot = OT_BOOLEAN;
	} else if (!strcasecmp(name, "hashcompat")) {
		*key = CURLOPT_SPECIAL_HASHCOMPAT;
		*ot = OT_ENUM;
	} else {
		return -1;
	}
	return 0;
}
//-----------------------------------------------
static int salara_curlopt_write(struct ast_channel *chan, const char *ccmd, char *name, const char *value)
{
struct ast_datastore *store;
struct global_curl_info *list;
struct curl_settings *cur, *new = NULL;
CURLoption key;
enum optiontype ot;

	if (chan) {
		if (!(store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
			// Create a new datastore
			if (!(store = ast_datastore_alloc(&curl_info, NULL))) {
				ast_log(LOG_ERROR, "Unable to allocate new datastore.  Cannot set any CURLs options\n");
				return -1;
			}

			if (!(list = ast_calloc(1, sizeof(*list)))) {
				ast_log(LOG_ERROR, "Unable to allocate list head.  Cannot set any CURLs options\n");
				ast_datastore_free(store);
				return -1;
			}

			store->data = list;
			AST_LIST_HEAD_INIT(list);
			ast_channel_datastore_add(chan, store);
		} else {
			list = store->data;
		}
	} else {
		// Populate the global structure
		list = &global_curl_info;
	}

	if (!parse_curlopt_key(name, &key, &ot)) {
		if (ot == OT_BOOLEAN) {
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)((long) ast_true(value));
			}
		} else if (ot == OT_INTEGER) {
			long tmp = atol(value);
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)tmp;
			}
		} else if (ot == OT_INTEGER_MS) {
			long tmp = atof(value) * 1000.0;
			if ((new = ast_calloc(1, sizeof(*new)))) {
				new->value = (void *)tmp;
			}
		} else if (ot == OT_STRING) {
			if ((new = ast_calloc(1, sizeof(*new) + strlen(value) + 1))) {
				new->value = (char *)new + sizeof(*new);
				strcpy(new->value, value);
			}
		} else if (ot == OT_ENUM) {
			if (key == CURLOPT_PROXYTYPE) {
				long ptype =
#if CURLVERSION_ATLEAST(7,10,0)
					CURLPROXY_HTTP;
#else
					CURLPROXY_SOCKS5;
#endif
				if (0) {
#if CURLVERSION_ATLEAST(7,15,2)
				} else if (!strcasecmp(value, "socks4")) {
					ptype = CURLPROXY_SOCKS4;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strcasecmp(value, "socks4a")) {
					ptype = CURLPROXY_SOCKS4A;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strcasecmp(value, "socks5")) {
					ptype = CURLPROXY_SOCKS5;
#endif
#if CURLVERSION_ATLEAST(7,18,0)
				} else if (!strncasecmp(value, "socks5", 6)) {
					ptype = CURLPROXY_SOCKS5_HOSTNAME;
#endif
				}

				if ((new = ast_calloc(1, sizeof(*new)))) {
					new->value = (void *)ptype;
				}
			} else if (key == CURLOPT_SPECIAL_HASHCOMPAT) {
				if ((new = ast_calloc(1, sizeof(*new)))) {
					new->value = (void *) (long) (!strcasecmp(value, "legacy") ? HASHCOMPAT_LEGACY : ast_true(value) ? HASHCOMPAT_YES : HASHCOMPAT_NO);
				}
			} else {
				goto yuck;// Highly unlikely
			}
		}

		// Memory allocation error
		if (!new) return -1;

		new->key = key;
	} else {
yuck:
		ast_log(LOG_ERROR, "Unrecognized option: %s\n", name);
		return -1;
	}

	// Remove any existing entry
	AST_LIST_LOCK(list);
	AST_LIST_TRAVERSE_SAFE_BEGIN(list, cur, list) {
		if (cur->key == new->key) {
			AST_LIST_REMOVE_CURRENT(list);
			free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END

	// Insert new entry
	ast_debug(1, "Inserting entry %p with key %d and value %p\n", new, new->key, new->value);
//	ast_verbose("[%s %s] Curls set option : Inserting entry %p with key %d and value %p\n", AST_MODULE, TimeNowPrn(), new, new->key, new->value);
	//ast_verbose("[%s %s] Curls start.\n", AST_MODULE, TimeNowPrn());
	AST_LIST_INSERT_TAIL(list, new, list);
	AST_LIST_UNLOCK(list);

	return 0;
}
//-----------------------------------------------
static int salara_curlopt_helper(struct ast_channel *chan, const char *ccmd, char *data, char *buf, struct ast_str **bufstr, ssize_t len)
{
struct ast_datastore *store;
struct global_curl_info *list[2] = { &global_curl_info, NULL };
struct curl_settings *cur = NULL;
CURLoption key;
enum optiontype ot;
int i;

	if (parse_curlopt_key(data, &key, &ot)) {
		ast_log(LOG_ERROR, "Unrecognized option: '%s'\n", data);
		return -1;
	}// else {//!!!!!!!!!!!!!!!!!!!!!!!!
	//    ast_log(LOG_ERROR, "Set option: '%s'\n", data, );
	//}

	if (chan && (store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
		list[0] = store->data;
		list[1] = &global_curl_info;
	}

	for (i = 0; i < 2; i++) {
		if (!list[i]) {
			break;
		}
		AST_LIST_LOCK(list[i]);
		AST_LIST_TRAVERSE(list[i], cur, list) {
			if (cur->key == key) {
				if (ot == OT_BOOLEAN || ot == OT_INTEGER) {
					if (buf) {
						snprintf(buf, len, "%ld", (long) cur->value);
					} else {
						ast_str_set(bufstr, len, "%ld", (long) cur->value);
					}
				} else if (ot == OT_INTEGER_MS) {
					if ((long) cur->value % 1000 == 0) {
						if (buf) {
							snprintf(buf, len, "%ld", (long)cur->value / 1000);
						} else {
							ast_str_set(bufstr, len, "%ld", (long) cur->value / 1000);
						}
					} else {
						if (buf) {
							snprintf(buf, len, "%.3f", (double) ((long) cur->value) / 1000.0);
						} else {
							ast_str_set(bufstr, len, "%.3f", (double) ((long) cur->value) / 1000.0);
						}
					}
				} else if (ot == OT_STRING) {
					ast_debug(1, "Found entry %p, with key %d and value %p\n", cur, cur->key, cur->value);
					if (buf) {
						ast_copy_string(buf, cur->value, len);
					} else {
						ast_str_set(bufstr, 0, "%s", (char *) cur->value);
					}
				} else if (key == CURLOPT_PROXYTYPE) {
					const char *strval = "unknown";
					if (0) {
#if CURLVERSION_ATLEAST(7,15,2)
					} else if ((long)cur->value == CURLPROXY_SOCKS4) {
						strval = "socks4";
#endif
#if CURLVERSION_ATLEAST(7,18,0)
					} else if ((long)cur->value == CURLPROXY_SOCKS4A) {
						strval = "socks4a";
#endif
					} else if ((long)cur->value == CURLPROXY_SOCKS5) {
						strval = "socks5";
#if CURLVERSION_ATLEAST(7,18,0)
					} else if ((long)cur->value == CURLPROXY_SOCKS5_HOSTNAME) {
						strval = "socks5hostname";
#endif
#if CURLVERSION_ATLEAST(7,10,0)
					} else if ((long)cur->value == CURLPROXY_HTTP) {
						strval = "http";
#endif
					}
					if (buf) {
						ast_copy_string(buf, strval, len);
					} else {
						ast_str_set(bufstr, 0, "%s", strval);
					}
				} else if (key == CURLOPT_SPECIAL_HASHCOMPAT) {
					const char *strval = "unknown";
					if ((long) cur->value == HASHCOMPAT_LEGACY) {
						strval = "legacy";
					} else if ((long) cur->value == HASHCOMPAT_YES) {
						strval = "yes";
					} else if ((long) cur->value == HASHCOMPAT_NO) {
						strval = "no";
					}
					if (buf) {
						ast_copy_string(buf, strval, len);
					} else {
						ast_str_set(bufstr, 0, "%s", strval);
					}
				}
				break;
			}
		}
		AST_LIST_UNLOCK(list[i]);
		if (cur) {
			break;
		}
	}

	return cur ? 0 : -1;
}
//-----------------------------------------------
static int salara_curl_helper(struct ast_channel *chan, const char *ccmd, char *info, char *buf, struct ast_str **input_str, ssize_t len)
{
struct ast_str *escapebuf = ast_str_thread_get(&thread_escapebuf, 16);
struct ast_str *str = ast_str_create(16);
int ret = -1;
AST_DECLARE_APP_ARGS(args,
    AST_APP_ARG(url);
    AST_APP_ARG(postdata);
);
CURL **curl;
struct curl_settings *cur;
struct ast_datastore *store = NULL;
int hashcompat = 0;
AST_LIST_HEAD(global_curl_info, curl_settings) *list = NULL;
char curl_errbuf[CURL_ERROR_SIZE + 1]; // add one to be safe

	if (buf) *buf = '\0';

	if (!str) return -1;

	if (!escapebuf) {
	    ast_free(str);
	    return -1;
	}

	if (ast_strlen_zero(info)) {
	    ast_log(LOG_WARNING, "CURLs requires an argument (URL)\n");
	    ast_free(str);
	    return -1;
	}

	AST_STANDARD_APP_ARGS(args, info);

	if (url_is_salara(args.url)) {
	    ast_log(LOG_ERROR, "URL '%s' is salara to HTTP/HTTPS injection attacks. Aborting CURLs() call.\n", args.url);
	    return -1;
	}

	if (chan) ast_autoservice_start(chan);

	if (!(curl = ast_threadstorage_get(&curl_instance, sizeof(*curl)))) {
	    ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
	    ast_free(str);
	    return -1;
	}

	AST_LIST_LOCK(&global_curl_info);
	AST_LIST_TRAVERSE(&global_curl_info, cur, list) {
	    if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
		hashcompat = (long) cur->value;
	    } else {
		curl_easy_setopt(*curl, cur->key, cur->value);
	    }
	}
	AST_LIST_UNLOCK(&global_curl_info);

	if (chan && (store = ast_channel_datastore_find(chan, &curl_info, NULL))) {
	    list = store->data;
	    AST_LIST_LOCK(list);
	    AST_LIST_TRAVERSE(list, cur, list) {
		if (cur->key == CURLOPT_SPECIAL_HASHCOMPAT) {
		    hashcompat = (long) cur->value;
		} else {
		    curl_easy_setopt(*curl, cur->key, cur->value);
		}
	    }
	}

	curl_easy_setopt(*curl, CURLOPT_URL, args.url);
	curl_easy_setopt(*curl, CURLOPT_FILE, (void *) &str);

	if (strstr(info,"https:")) {
	    curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYPEER, 0L);// !!!!!!!! +++++++ !!!!!!!!!!!
	    curl_easy_setopt(*curl, CURLOPT_SSL_VERIFYHOST, 0); // !!!!!!!! +++++++ !!!!!!!!!!!
	}

	if (args.postdata) {
	    curl_easy_setopt(*curl, CURLOPT_POST, 1);
	    curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, args.postdata);
	}

	// Temporarily assign a buffer for curl to write errors to.
	curl_errbuf[0] = curl_errbuf[CURL_ERROR_SIZE] = '\0';
	curl_easy_setopt(*curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(*curl) != 0) {
	    ast_log(LOG_WARNING, "%s ('%s')\n", curl_errbuf, args.url);
	}

	// Reset buffer to NULL so curl doesn't try to write to it when the
	// buffer is deallocated. Documentation is vague about allowing NULL
	// here, but the source allows it. See: "typecheck: allow NULL to unset
	// CURLOPT_ERRORBUFFER" (62bcf005f4678a93158358265ba905bace33b834).
	curl_easy_setopt(*curl, CURLOPT_ERRORBUFFER, (char*)NULL);

	if (store) AST_LIST_UNLOCK(list);

	if (args.postdata) curl_easy_setopt(*curl, CURLOPT_POST, 0);

	if (ast_str_strlen(str)) {
		ast_str_trim_blanks(str);

		ast_debug(3, "str='%s'\n", ast_str_buffer(str));
		if (hashcompat) {
			char *remainder = ast_str_buffer(str);
			char *piece;
			struct ast_str *fields = ast_str_create(ast_str_strlen(str) / 2);
			struct ast_str *values = ast_str_create(ast_str_strlen(str) / 2);
			int rowcount = 0;
			while (fields && values && (piece = strsep(&remainder, "&"))) {
			    char *name = strsep(&piece, "=");
			    struct ast_flags mode = (hashcompat == HASHCOMPAT_LEGACY ? ast_uri_http_legacy : ast_uri_http);
			    if (piece) ast_uri_decode(piece, mode);
			    ast_uri_decode(name, mode);
			    ast_str_append(&fields, 0, "%s%s", rowcount ? "," : "", ast_str_set_escapecommas(&escapebuf, 0, name, INT_MAX));
			    ast_str_append(&values, 0, "%s%s", rowcount ? "," : "", ast_str_set_escapecommas(&escapebuf, 0, S_OR(piece, ""), INT_MAX));
			    rowcount++;
			}
			pbx_builtin_setvar_helper(chan, "~ODBCFIELDS~", ast_str_buffer(fields));
			if (buf) {
			    ast_copy_string(buf, ast_str_buffer(values), len);
			} else {
			    ast_str_set(input_str, len, "%s", ast_str_buffer(values));
			}
			ast_free(fields);
			ast_free(values);
		} else {
		    if (buf) {
			ast_copy_string(buf, ast_str_buffer(str), len);
		    } else {
			ast_str_set(input_str, len, "%s", ast_str_buffer(str));
		    }
		}
		ret = 0;
	}
	ast_free(str);

	if (chan) ast_autoservice_stop(chan);

	return ret;
}
//-----------------------------------------------
static int salara_curl_exec(struct ast_channel *chan, const char *ccmd, char *info, char *buf, size_t len)
{
    return salara_curl_helper(chan, ccmd, info, buf, NULL, len);
}
//-----------------------------------------------
static int salara_curl2_exec(struct ast_channel *chan, const char *ccmd, char *info, struct ast_str **buf, ssize_t len)
{
    return salara_curl_helper(chan, ccmd, info, NULL, buf, len);
}
//-----------------------------------------------
static int salara_curlopt_read(struct ast_channel *chan, const char *ccmd, char *data, char *buf, size_t len)
{
    return salara_curlopt_helper(chan, ccmd, data, buf, NULL, len);
}
//-----------------------------------------------
static int salara_curlopt_read2(struct ast_channel *chan, const char *ccmd, char *data, struct ast_str **buf, ssize_t len)
{
    return salara_curlopt_helper(chan, ccmd, data, NULL, buf, len);
}
#endif
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------

static int hook_callback(int category, const char *event, char *body);

static struct manager_custom_hook hook = {
    .file = __FILE__,
    .helper = hook_callback,
};

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

static struct ast_cli_entry cli_salara[] = {
    AST_CLI_DEFINE(cli_salara_info, "Show Salara module information/configuration/route"),
    AST_CLI_DEFINE(cli_salara_set_verbose, "Off/On/Debug/Dump verbose level"),
    AST_CLI_DEFINE(cli_salara_set_route, "Add caller:called to route table"),
    AST_CLI_DEFINE(cli_salara_send_cmd, "Send AMI Command"),
    AST_CLI_DEFINE(cli_salara_get_status_exten, "Get extension status"),
    AST_CLI_DEFINE(cli_salara_get_status_chan, "Get channel status"),
    AST_CLI_DEFINE(cli_salara_get_status_peer, "Get peer status"),
    AST_CLI_DEFINE(cli_salara_send_msg, "Send AMI Message"),
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
/*
static int CheckCurlAnswer(char *buk, char *exten)
{
int ret=-1;
int len=0;
char *nn = NULL;//"\"personal_manager_internal_phone\":";
char *uk=NULL, *uk2=NULL;
char dst[AST_MAX_EXTENSION]={0};

    if ((len = strlen(buk)) > 0) {
	nn = (char *)calloc(1,strlen(key_word) + 32);
	if (nn) {
	    sprintf(nn,"\"%s\":",key_word);
	    uk = strstr(buk,nn);
	    if (uk) {
		uk += strlen(nn);
		uk2 = strchr(uk,'"');
		if (uk2) {
		    uk2++;
		    uk = strchr(uk2,'"');
		    if (uk) {
			len = uk-uk2; if (len>=AST_MAX_EXTENSION) len = AST_MAX_EXTENSION-1;
			memcpy(dst, uk2, len);
			memcpy(exten, dst, len);
			ret=0;
		    }
		}
	    }
	    free(nn);
	}
    }

    return ret;
}
*/
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
#ifndef CURLs
static int send_curl(char *url, int wait, char *str, CURLcode *err, int crt)
{
int ret=-1, lg = salara_verbose;
CURL *curl;
CURLcode res = CURLE_OK;
struct MemoryStruct chunk;

    chunk.memory = (char *)malloc(1);
    chunk.size = 0;

//    curl_global_init(CURL_GLOBAL_ALL);

    curl = curl_easy_init();
    if (curl) {
	struct curl_slist *headers=NULL;
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	curl_easy_setopt(curl, CURLOPT_URL, url);
	//curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req);
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
		ast_verbose("[%s] Curl answer :%.*s\n", AST_MODULE, chunk.size, (char *)chunk.memory);
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(chunk.memory);
//	curl_global_cleanup();
    }

    return ret;
}
#endif
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------
/*
static int salara_transfer_exec(struct ast_channel *chan, const char *data, int pr)
{
int res, len;
const char *VarName = "TRANSFERSTATUS";
const char *S_Good = "SUCCESS";
const char *S_Fail = "FAILURE";
const char *S_Unsup = "UNSUPPORTED";
char *slash, *tech=NULL, *dest=NULL, *parse=NULL;
void *status=NULL;
AST_DECLARE_APP_ARGS(args,
    AST_APP_ARG(dest);
);

    if (ast_strlen_zero((char *)data)) {
	if (pr) ast_verbose("[%s] Transfer requires an argument ([Tech/]destination)\n", AST_MODULE);
	pbx_builtin_setvar_helper(chan, VarName, S_Fail);
	return -1;
    } else parse = ast_strdupa(data);

    AST_STANDARD_APP_ARGS(args, parse);

    dest = args.dest;

    if ((slash = strchr(dest, '/')) && (len = (slash - dest))) {
	tech = dest;
	dest = slash + 1;
	// Allow execution only if the Tech/destination agrees with the type of the channel
	if (strncasecmp(ast_channel_tech(chan)->type, tech, len)) {
	    pbx_builtin_setvar_helper(chan, VarName, S_Fail);
	    return -1;
	}
    }

    // Check if the channel supports transfer before we try it
    if (!ast_channel_tech(chan)->transfer) {
	pbx_builtin_setvar_helper(chan, VarName, S_Unsup);
	return -1;
    }

    res = ast_transfer(chan, dest);

    if (res < 0) status = (char *)S_Fail;
	    else status = (char *)S_Good;

    pbx_builtin_setvar_helper(chan, VarName, (char *)status);

    return res;
}
*/
//----------------------------------------------------------------------
static int app_salara_exec(struct ast_channel *ast, const char *data)
{
int lg;
int ret_curl=-1, stat=-1, res_transfer;
char *cid=NULL, *info=NULL, *buf=NULL;
unsigned int aid=0;
s_act_list *abc=NULL;
#ifdef CURLs
const char *ccmd = NULL;
#else
int ssl=0;
CURLcode er;
#endif
struct ast_channel *new_ast=NULL;


    if (!data) return -1;
    else
    if (!strlen(data)) return -1;

    buf = (char *)calloc(1,MAX_ANSWER_LEN);

    if (!buf) {
	ast_verbose("[%s] calloc memory error !\n", AST_MODULE);
	return -1;
    }

    lg = salara_verbose;

    if (lg) ast_verbose("[%s %s] application start.\n", AST_MODULE, TimeNowPrn());

    cid = ast_channel_caller(ast)->id.number.str;

    //----------------   send Curl   -------------------------------------
    info = (char *)calloc(1, strlen(dest_url) + strlen(cid) + 1);
    if (info) {
	sprintf(info,"%s%s", dest_url, cid);
#ifdef CURLs
	ret_curl = salara_curl_exec(ast, ccmd, info, buf, MAX_ANSWER_LEN);
	if ((!ret_curl) && (lg)) ast_verbose("[%s] Curl_exec OK: url=%s\n", AST_MODULE, info);
#else
	if (strstr(dest_url,"https")) ssl=1;
	ret_curl = send_curl(info, SALARA_CURLOPT_TIMEOUT, buf, &er, ssl);
	if (er != CURLE_OK) {
	    ret_curl = 1;
	    if (lg) ast_verbose("[%s] Curl_exec Error: url=%s\n\t--buf=[%s] err='%s'\n", AST_MODULE, info, buf, curl_easy_strerror(er));
	} else if (lg) ast_verbose("[%s] Curl_exec OK: url=%s\n", AST_MODULE, info);
#endif
	free(info);
    } else ast_verbose("[%s] Error: calloc memory\n", AST_MODULE);
    //--------------------------------------------------------------------

    if (ret_curl!=0) {
	memset(dest_number,0,AST_MAX_EXTENSION);
	strcpy(dest_number, DEF_DEST_NUMBER);
	check_dest(cid, dest_number);
	if (lg) ast_verbose("[%s] Curl failure, route call to default dest '%s'\n",
		    AST_MODULE,
		    dest_number);
    } else {
	memset(dest_number,0,AST_MAX_EXTENSION);
#ifdef CURLs
	CheckCurlAnswer(buf, dest_number);
#else
	strcpy(dest_number, buf);
#endif
    }
    aid = MakeAction(2, dest_number, "", "", context);

    if (aid>=0) {
	abc = find_act(aid);
	if (abc) {
	    stat = abc->act->status;
	    delete_act(abc,1);
	}
	if (!check_stat(stat)) {
	    if (lg>1) ast_verbose("[%s] Extension '%s' status (%d) OK !\n", AST_MODULE, dest_number, stat);
	} else {
	    if (lg) ast_verbose("[%s] Extension '%s' status (%d) BAD !\n", AST_MODULE, dest_number, stat);
	    memset(dest_number,0,AST_MAX_EXTENSION);
	    strcpy(dest_number, DEF_DEST_NUMBER);
	    check_dest(cid, dest_number);
	    if (lg) ast_verbose("[%s] Route call to default dest '%s'\n", AST_MODULE, dest_number);
	}
    }


    res_transfer = ast_transfer(ast, dest_number);
//    res_transfer = salara_transfer_exec(ast, dest_number, lg);

/*
    int tom=100, rco;
    struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
    if (cap) {
	ast_format_cap_append(cap, ast_format_ulaw, 0);
	ast_format_cap_append(cap, ast_format_alaw, 0);
//struct ast_channel *ast_call_forward(struct ast_channel *caller, struct ast_channel *orig, int *timeout, struct ast_format_cap *cap, struct outgoing_helper *oh, int *outstate)
	new_ast = ast_call_forward(ast, ast, &tom, cap, NULL, &rco);
	if (!new_ast) ao2_ref(cap, -1);
    }
*/

    if (res_transfer>=0) add_chan_record(ast_channel_name(ast), cid, dest_number, (void *)ast);

    if (lg) {
	stat = ast_channel_state(ast); if (stat > MAX_CHAN_STATE-1) stat=MAX_CHAN_STATE-1;
	ast_verbose("[%s] CallerID=[%s] called=[%s] transfer to '%s' res=%d status=%d (%s) | new=%p\n",
		AST_MODULE,
		ast_channel_name(ast),
		data,
		dest_number,
		res_transfer,
		stat,
		ChanStateName[stat],
		(void *)new_ast);
	ast_verbose("[%s %s] application stop.\n", AST_MODULE, TimeNowPrn());
    }

    if (buf) free(buf);

    return 0;
}
//----------------------------------------------------------------------
/*
inline static void look_chan(const char *nchan, const char *caller, const char *ext, int pr)
{
s_chan_record *rec = find_chan_by_dest(caller, ext);

    if (!rec) {
	if (pr>=2) ast_verbose("[%s %s] Look_chan : Not found channel with chan=[%s] exten=[%s] caller=[%s]\n",
			AST_MODULE,
			TimeNowPrn(),
			nchan, ext, caller);
    }
}
*/
//----------------------------------------------------------------------
static int hook_callback(int category, const char *event, char *body)
{
int lg, id=-1, sti=-1, done=0, dl, rdy=0, tp=-1;
char *uk=NULL, *uk2=NULL;
char stx[SIZE_OF_RESP]={0};

//    if ( (strstr(event,"RTCP")) || (strstr(event,"Cdr")) ) return 0;

    if (!strcmp(event,"HookResponse")) tp=0;
    else if (!strcmp(event,"Hangup")) tp=1;
    else if (!strcmp(event,"Newchannel")) tp=2;
    else if (!strcmp(event,"Newexten")) tp=3;
    else if (!strcmp(event,"AgentConnect")) tp=4;
    else return 0;

    lg = salara_verbose;

//    if (lg) ast_verbose("[%s] cat=%d event='%s' body=[\n%s]\n", AST_MODULE, category, event, body);
//    return 0;

    if ( (strlen(hook_tmp_str) + strlen(body)) > max_buf_size ) {
	if (lg) ast_verbose("<%s>\n<%s>\n",hook_tmp_str,body);
	memset(hook_tmp_str,0,max_buf_size);
    }
    strcat(hook_tmp_str, body);
    if (strstr(hook_tmp_str, "\r\n\r\n")) done=1;

    if (done) {
	switch (tp) {
	    case 0 ://HookResponse
		if (console) ast_verbose("%s",hook_tmp_str);
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
				memcpy(stx, uk, dl); sti = atoi(stx); uk = strstr(hook_tmp_str, S_StatusText);
				if (uk) { uk += strlen(S_StatusText); if (*uk == ' ') uk++; rdy=1; }
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
			    uk2 = strchr(uk, '\n'); if (uk2) { if (*(uk2-1) == '\r') uk2--; } else uk2 = strchr(uk,'\0');
			    if (uk2) {
				memset(stx,0,SIZE_OF_RESP); dl = uk2 - uk; if (dl>=SIZE_OF_RESP) dl = SIZE_OF_RESP-1; memcpy(stx, uk, dl);
				if (update_act_by_index(id, sti, stx)) if (lg>=2) ast_verbose("[hook] event='%s' action_id=%d not found in act_list\n", event, id);
			    }
			}
		    }
		} else if (lg) ast_verbose("[%s] event='%s' ActionID not found\n", AST_MODULE, event);
	    break;
	    case 1://Hangup
	    case 2://Newchannel
	    case 3://Newexten
		if (lg>1) ast_verbose("%s",hook_tmp_str);
		int cs=100;
		char chan[AST_CHANNEL_NAME]; char exten[AST_MAX_EXTENSION]; 
		char caller[AST_MAX_EXTENSION]; char chan_state[AST_MAX_EXTENSION]; char app[AST_MAX_EXTENSION];
		memset(chan,0,AST_CHANNEL_NAME); memset(exten,0,AST_MAX_EXTENSION);
		memset(caller,0,AST_MAX_EXTENSION); memset(chan_state,0,AST_MAX_EXTENSION); memset(app,0,AST_MAX_EXTENSION);
		uk = strstr(hook_tmp_str, S_Channel);
		if (uk) {
		    uk += strlen(S_Channel); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_CHANNEL_NAME) dl=AST_CHANNEL_NAME-1; memcpy(chan, uk, dl);
		    }
		}
		uk = strstr(hook_tmp_str, S_Exten);
		if (uk) {
		    uk += strlen(S_Exten); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1; memcpy(exten, uk, dl);
		    }
		}
		uk = strstr(hook_tmp_str, S_CallerIDNum);
		if (uk) {
		    uk += strlen(S_CallerIDNum); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1; memcpy(caller, uk, dl);
		    }
		}
		uk = strstr(hook_tmp_str, S_ChannelState);
		if (uk) {
		    uk += strlen(S_ChannelState); if (*uk == ' ') uk++; uk2 = strstr(uk,"\r\n");
		    if (uk2) {
			dl = uk2 - uk; if (dl >= AST_MAX_EXTENSION) dl=AST_MAX_EXTENSION-1; 
			memcpy(chan_state, uk, dl); cs = atoi(chan_state);
			if ((cs<0) || (cs>=MAX_CHAN_STATE)) cs = MAX_CHAN_STATE-1;
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
		if (lg) ast_verbose("[%s %s] '%s' event : chan='%s' exten='%s' caller='%s' state=%d(%s) app='%s'\n",
				AST_MODULE, TimeNowPrn(), event, chan, exten, caller, cs, ChanStateName[cs], app);
		if (tp==1) {//Hangup
		    if ( (strlen(chan)) && (strlen(exten)) && (strlen(caller)) ) find_chan(chan, caller, exten, 1);
		} else if (tp==2) {//Newchannel
		    if ( (strlen(chan)) && (strlen(exten)) && (strlen(caller)) ) update_chan(chan, caller, exten);
		} else {//Newexten
		    //if (lg) ast_verbose("%s",hook_tmp_str);
		}
	    break;
	    case 4://AgentConnect
		if (lg) ast_verbose("%s",hook_tmp_str);
	    break;
		default : if (lg) ast_verbose("[%s] Unknown event='%s' body=[\n%s]\n",AST_MODULE,event,hook_tmp_str);
	}//switch

	if ((lg>=2) && (!console)) ast_verbose("[%s] event='%s' body=[\n%s]\n",AST_MODULE,event,hook_tmp_str);
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
	    e->command = "salara show {info|conf|route}";
	    e->usage = "Usage: salara show {info|conf|route}\n";
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
	ast_cli(a->fd, "\t-- curl timeout %d\n", SALARA_CURLOPT_TIMEOUT);
	ast_cli(a->fd, "\t-- default context: %s\n", context);
	ast_cli(a->fd, "\t-- module version: %s\n", SALARA_VERSION);
	ast_cli(a->fd, "\t-- asterisk version: %s\n", ast_get_version());
	ast_cli(a->fd, "\t-- events verbose level: ");

	switch (lg) {
	    case 0 : ast_cli(a->fd, "off\n"); break;
	    case 1 : ast_cli(a->fd, "on\n"); break;
	    case 2 : ast_cli(a->fd, "debug\n"); break;
	    case 3 : ast_cli(a->fd, "dump\n"); break;
		default: ast_cli(a->fd, "unknown\n");
	}
	ast_cli(a->fd, "\t-- started: %s", ctime(&salara_start_time.tv_sec));
	c_t.tv_sec -= salara_start_time.tv_sec;
	ast_cli(a->fd, "\t-- uptime: %s (%lu sec)\n", seconds_to_date(buf, c_t.tv_sec), c_t.tv_sec);

	ast_mutex_lock(&route_lock);
	    ast_cli(a->fd, "\t-- routing table: records: %d",route_hdr.counter);
	    if (lg > 2) {
		if (route_hdr.first)
		    ast_cli(a->fd," (first=%p, end=%p)",
				(void *)route_hdr.first,
				(void *)route_hdr.end);
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

    //if (a->argc < 4) return CLI_SHOWUSAGE;

    return CLI_FAILURE;
}

//----------------------------------------------------------------------
static char *cli_salara_get_status_exten(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int act;
char *buf=NULL;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara get status extension";
	    e->usage = "\nUsage: salara get status extension <exten>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 4) return CLI_SHOWUSAGE;

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

		return CLI_SUCCESS;
	    }
	break;
    }

    //if (a->argc < 4) return CLI_SHOWUSAGE;

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

	    if (a->argc < 4) return CLI_SHOWUSAGE;

	    buf = (char *)calloc(1, CMD_BUF_LEN);
	    if (buf) {
		ast_mutex_lock(&act_lock);
		    Act_ID++;
		    act = Act_ID;
		ast_mutex_unlock(&act_lock);

		add_act(act);

		//sprintf(buf,"Action: Status\nChannel: %s\nActionID: %u\n\n", a->argv[4], act);
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

    //if (a->argc < 4) return CLI_SHOWUSAGE;

    return CLI_FAILURE;
}
//----------------------------------------------------------------------
static char *cli_salara_get_status_peer(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
int act;
char *buf=NULL;

    switch (cmd) {
	case CLI_INIT:
	    e->command = "salara get status peer";
	    e->usage = "\nUsage: salara get status peer <peer>\n\n";
	    return NULL;
	case CLI_GENERATE:
	    return NULL;
	case CLI_HANDLER:

	    if (a->argc < 4) return CLI_SHOWUSAGE;

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

		usleep(2000);
		s_act_list *abc = find_act(act);
		if (abc) delete_act(abc,1);
		free(buf);

		return CLI_SUCCESS;
	    }
	break;
    }

    //if (a->argc < 4) return CLI_SHOWUSAGE;

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

    //if (a->argc < 4) return CLI_SHOWUSAGE;

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
int len = -1, dl, verb, i;
enum bool begin=false, end=false;
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
	ast_verbose("[%s] Write config file '%s' error: %s\n", AST_MODULE, file_name, strerror(errno));
	ast_log(LOG_ERROR, "[%s] Write config file '%s' error: %s\n", AST_MODULE, file_name, strerror(errno));
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
	    len += fprintf(fp, ";8003:0\n");
	    len += fprintf(fp, ";8002:00\n");
	    len += fprintf(fp, ";8001:000\n");
	    len += fprintf(fp, ";8000:0000\n");
	}
    ast_mutex_unlock(&route_lock);
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[event]\n");
    len += fprintf(fp, "verbose=%d\n", salara_verbose);
    len += fprintf(fp, FORMAT_SEPARATOR_LINE);

    len += fprintf(fp, "[url]\n");
    if (!strlen(rest_server)) strcpy(rest_server, DEFAULT_SRV_ADDR);
    len += fprintf(fp, "rest_server=%s\n", rest_server);
    len += fprintf(fp, "dest_url=%s\n", dest_url);
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
#ifdef CURLs
static struct ast_custom_function salara_curls = {
    .name = "CURLs",
    .synopsis = "Retrieves the contents of a URL",
    .syntax = "CURLs(url[,post-data])",
    .desc =
    "  url       - URL to retrieve\n"
    "  post-data - Optional data to send as a POST (GET is default action)\n",
    .read = salara_curl_exec,
    .read2 = salara_curl2_exec,
};

static struct ast_custom_function salara_curlopts = {
    .name = "CURLOPTs",
    .synopsis = "Set options for use with the CURLs() function",
    .syntax = "CURLOPTs(<option>)",
    .desc =
"  cookie         - Send cookie with request [none]\n"
"  conntimeout    - Number of seconds to wait for connection\n"
"  dnstimeout     - Number of seconds to wait for DNS response\n"
"  ftptext        - For FTP, force a text transfer (boolean)\n"
"  ftptimeout     - For FTP, the server response timeout\n"
"  header         - Retrieve header information (boolean)\n"
"  httptimeout    - Number of seconds to wait for HTTP response\n"
"  maxredirs      - Maximum number of redirects to follow\n"
"  proxy          - Hostname or IP to use as a proxy\n"
"  proxytype      - http, socks4, or socks5\n"
"  proxyport      - port number of the proxy\n"
"  proxyuserpwd   - A <user>:<pass> to use for authentication\n"
"  referer        - Referer URL to use for the request\n"
"  useragent      - UserAgent string to use\n"
"  userpwd        - A <user>:<pass> to use for authentication\n"
"  ssl_verifypeer - Whether to verify the peer certificate (boolean)\n"
"  hashcompat     - Result data will be compatible for use with HASH()\n"
"                 - if value is \"legacy\", will translate '+' to ' '\n"
"",
    .read = salara_curlopt_read,
    .read2 = salara_curlopt_read2,
    .write = salara_curlopt_write,
};

AST_TEST_DEFINE(salara_url)
{
const char *bad_urls [] = {
    "http://example.com\r\nDELETE http://example.com/everything",
    "http://example.com\rDELETE http://example.com/everything",
    "http://example.com\nDELETE http://example.com/everything",
    "\r\nhttp://example.com",
    "\rhttp://example.com",
    "\nhttp://example.com",
    "http://example.com\r\n",
    "http://example.com\r",
    "http://example.com\n",
};
const char *good_urls [] = {
    "http://example.com",
    "http://example.com/%5C\r%5C\n",
};
int i;
enum ast_test_result_state res = AST_TEST_PASS;

    switch (cmd) {
	case TEST_INIT:
	    info->name = "salara_url";
	    info->category = "/funcs/func_curl/";
	    info->summary = "cURLs salara URL test";
	    info->description = "Ensure that any combination of '\\r' or '\\n' in a URL invalidates the URL";
	case TEST_EXECUTE:
	break;
    }

    for (i = 0; i < ARRAY_LEN(bad_urls); ++i) {
	if (!url_is_salara(bad_urls[i])) {
	    ast_test_status_update(test, "String '%s' detected as valid when it should be invalid\n", bad_urls[i]);
	    res = AST_TEST_FAIL;
	}
    }

    for (i = 0; i < ARRAY_LEN(good_urls); ++i) {
	if (url_is_salara(good_urls[i])) {
	    ast_test_status_update(test, "String '%s' detected as invalid when it should be valid\n", good_urls[i]);
	    res = AST_TEST_FAIL;
	}
    }

    return res;
}
#endif
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
static int MakeAction(int type, char *from, char *to, char *mess, char *ctext)
{
char buf[512]={0};
int act=0, lg = salara_verbose;

    if ((type < 0) || (type > 4)) return 0;

    ast_mutex_lock(&act_lock);
	Act_ID++;
	act = Act_ID;
    ast_mutex_unlock(&act_lock);

    add_act(act);

    switch (type) {
	case 0://outgoing call
	    sprintf(buf,"Action: Originate\nChannel: %s/%s\nContext: %s\nExten: %s\nPriority: 1\n"
		"Callerid: %s\nTimeout: 30000\nActionID: %u\n\n",
		StrUpr(Tech),
		from,
		context,
		to,
		from,
		act);
	break;
	case 1://message send
	    sprintf(buf,"Action: MessageSend\nActionID: %u\nTo: %s:%s\nFrom: %s\nBody: %s\n\n",
		act,
		StrLwr(Tech),
		to,
		from,
		mess);
	break;
	case 2://exten. status
	    if (strlen(ctext)) sprintf(buf,"Action: ExtensionState\nActionID: %u\nExten: %s\nContext: %s\n\n", act, from, ctext);
			  else sprintf(buf,"Action: ExtensionState\nActionID: %u\nExten: %s\nContext: %s\n\n", act, from, context);
	break;
	case 3://peer status
	    sprintf(buf,"Action: SIPpeerstatus\nActionID: %u\nPeer: %s\n\n", act, from);
	break;
	case 4://channel status
	    /*
	    Action: Status
	    Channel: SIP/8003-00000001
	    ActionID: 1
	    */
	    //sprintf(buf,"Action: Status\nChannel: %s\nActionID: %u\n\n", from, act);
	    sprintf(buf,"Action: Command\nCommand: core show channel %s\nActionID: %u\n\n",from,act);
	break;
    }

    if (lg>1) {
	ast_verbose("[%s] MakeAction : req_type=%d action_id=%d\n", AST_MODULE, type, act);
	ast_verbose(buf);
	console=1;
    }
    msg_send(buf);
    console=0;

    return act;
}
//----------------------------------------------------------------------
static unsigned int cli_nitka(void *data)
{
#define max_param 3
int lg = salara_verbose;
struct ast_tcptls_session_instance *ser = data;
int flags, res=0, len, dl, body_len=0;
unsigned int ret=0, aid;
char *buf=NULL;
int uk=0, loop=1, tmp=0, done=0, stat=-1, i=0, req_type=-1, rtype;
char *uki=NULL, *uks=NULL, *uke=NULL;
const char *answer_bad = "{\"result\":-2,\"text\":\"Error\"}";
const char *names[max_param] = {"operator=", "phone=", "msg="};
//const char *names_rest[max_param_rest] = {"operator", "phone", "msg", "exten", "peer", "chan", "context"};
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

	flags = fcntl(ser->fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(ser->fd, F_SETFL, flags);

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
					//if (salara_verbose) ast_verbose("[%s] Post data len:%d\n", AST_MODULE, body_len);
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
					unsigned char er=0;
					json_t *tobj = json_object_get(obj, &names_rest[0][0]);//"operator"
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
						req_type = 2;//get status exten
						tobj = json_object_get(obj, &names_rest[6][0]);//"context"
						if (json_is_string(tobj)) sprintf(cont,"%s", json_string_value(tobj));
						else er=1;
					    } else {
						er=0;
						json_t *tobj = json_object_get(obj, &names_rest[4][0]);//"peer"
						if (json_is_string(tobj)) sprintf(operator,"%s", json_string_value(tobj));
						else
						if (json_is_integer(tobj)) sprintf(operator,"%lld", json_integer_value(tobj));
						else er=1;
						if (!er) {
						    req_type = 3;//get status peer
						    tobj = json_object_get(obj, &names_rest[6][0]);//"context"
						    if (json_is_string(tobj)) sprintf(cont,"%s", json_string_value(tobj));
						    else er=1;
						} else {
						    er=0;
						    json_t *tobj = json_object_get(obj, &names_rest[5][0]);//"channel"
						    if (json_is_string(tobj)) {
							sprintf(operator,"%s", json_string_value(tobj));
							req_type = 4;//get status channel
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
				    //if (salara_verbose) ast_verbose("[%s] Post data:%s\n", AST_MODULE, uk_body);
				}
			    }
			}
		    } else loop=0;
		}
	    }//while (loop)

	    if (ok) {
		if (lg) {
		    rtype = req_type;
		    if (rtype >= MAX_ACT_TYPE) rtype=MAX_ACT_TYPE-1;
		    ast_verbose("[%s] Action type '%s' (%d) with param : operator='%s' phone='%s' msg='%s' context='%s'\n",
				AST_MODULE,
				ActType[rtype],
				rtype,
				operator,
				phone,
				msg,
				cont);
		}
		//------------------------------------------------------
		two=0;
		if (req_type>2) aid = MakeAction(req_type,operator,phone,msg,cont);//get status exten.,peer,channel
			   else aid = MakeAction(2,operator,phone,msg,cont);//get status exten.
		usleep(1000);
		memset(ack_text,0,SIZE_OF_RESP);
		if (aid>0) {
		    s_act_list *abc = find_act(aid);
		    if (abc) {
			stat = abc->act->status;
			strcpy(ack_text, abc->act->resp);
			delete_act(abc,1);
		    }
		    if (!check_stat(stat)) two++;
		    if (lg) ast_verbose("[%s] Dest '%s' status (%d)\n", AST_MODULE, operator, stat);
		    if (req_type >= 2) {//get status:exten peer chan
			two=0;
			done=1;
			sprintf(ack_status,"{\"result\":%d,\"text\":\"%s\"}", stat, ack_text);
			write(ser->fd, ack_status, strlen(ack_status));
			if (lg) ast_verbose("[%s] Send answer '%s' to rest client\n", AST_MODULE, ack_status);
			break;
		    } else {
			sprintf(ack_status,"{\"result\":%d,\"text\":\"%s\"}", stat, ack_text);
			write(ser->fd, ack_status, strlen(ack_status));
			if (lg) ast_verbose("[%s] Send answer '%s' to rest client\n", AST_MODULE, ack_status);
		    }
		} else {
		    write(ser->fd, answer_bad, strlen(answer_bad));
		    if (lg) ast_verbose("[%s] Send answer '%s' to rest client\n", AST_MODULE, answer_bad);
		}
	    } else {
		write(ser->fd, answer_bad, strlen(answer_bad));
		if (lg) ast_verbose("[%s] Error request parser\n", AST_MODULE);
	    }

	    if ((res>0) && !ok && lg) ast_verbose("%s\n", buf);
	    else if (lg>1) ast_verbose("[%s] Data from rest client :\n%s\n", AST_MODULE, buf);

	    if (errno != EINTR && errno != EAGAIN && errno != 0) {
		if (lg) ast_verbose("[%s] Socket error reading data: '%s'\n", AST_MODULE, strerror(errno));
		done=1;
	    }
	    if (done) break;
	}

    } else if (lg) ast_verbose("[%s] Error calloc in cli_nitka (sock=%d)\n", AST_MODULE, ser->fd);


    if (ser->fd > 0) {
	if (lg>1) ast_verbose("[%s] Close client socket %d\n", AST_MODULE, ser->fd);
	shutdown(ser->fd, SHUT_RDWR);
	close(ser->fd);
    }

    if (buf) free(buf);


    if ((ok) && (two) && (req_type < 2)) {
	ret = MakeAction(req_type, operator, phone, msg, cont);
    }

    return ret;
}
//----------------------------------------------------------------------
static void *cli_rest_open(void *data)
{
int lg = salara_verbose;
struct ast_tcptls_session_instance *tcptls_session = data;

    if (lg) ast_verbose("[%s %s] cli_rest_open : connection from client '%s' (adr=%p sock=%d)\n",
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


/*    ast_verbose("[%s] srv_nitka listen port %d (sock=%d)\n",
		AST_MODULE,
		ast_sockaddr_port(&desc->local_address),
		desc->accept_fd);*/

    for (;;) {

	if (desc->periodic_fn) desc->periodic_fn(desc);

	i = ast_wait_for_input(desc->accept_fd, desc->poll_timeout);
	if (i <= 0) continue;

	fd = ast_accept(desc->accept_fd, &addr);
	if (fd < 0) {
	    if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR) && (errno != ECONNABORTED)) {
		ast_verbose("[%s] Accept failed: %s\n", AST_MODULE, strerror(errno));
		break;
	    }
	    continue;
	}// else ast_verbose("[%s] Accept cli_sock=%d\n", AST_MODULE, fd);
	tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor);
	if (!tcptls_session) {
	    ast_verbose("[%s] No memory for new session: %s\n", AST_MODULE, strerror(errno));
	    if (close(fd)) ast_verbose("[%s] close() failed: %s\n", AST_MODULE, strerror(errno));
	    continue;
	}

	tcptls_session->overflow_buf = ast_str_create(128);
	fcntl(fd, F_SETFL, (fcntl(fd, F_GETFL)) & ~O_NONBLOCK);
	tcptls_session->fd = fd;
	tcptls_session->parent = desc;
	ast_sockaddr_copy(&tcptls_session->remote_address, &addr);

	tcptls_session->client = 0;

	if (ast_pthread_create_detached_background(&launched, NULL, cli_rest_open, tcptls_session)) {
	    ast_verbose("[%s] Unable to launch helper thread: %s\n", AST_MODULE, strerror(errno));
	    ao2_ref(tcptls_session, -1);
	}// else ast_verbose("[%s] srv_nitka : Thread start for client '%s'\n", AST_MODULE, ast_sockaddr_stringify(&addr));
    }
    return NULL;
}

//----------------------------------------------------------------------
static void *cli_rest_close(void *data)
{
int lg = salara_verbose;
struct ast_tcptls_session_instance *t_s = data;
s_act_list *abc = NULL;

    if (lg) ast_verbose("[%s %s] cli_rest_close : act_id=%d, release session (adr=%p sock=%d)\n",
		AST_MODULE, TimeNowPrn(),
		(unsigned int)t_s->client,
		(void *)data,
		t_s->fd);
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
		//ast_channel_lock(ast);
		    stat = ast_channel_state(ast);
		    name = strdupa(ast_channel_name(ast));
		//ast_channel_unlock(ast);
		if (stat > MAX_CHAN_STATE-1) stat=MAX_CHAN_STATE-1;
		if (lg) ast_verbose("[%s] Periodics : total=%d channel=[%s] status=%d (%s) ast=%p\n",
			    AST_MODULE,
			    cnt,
			    name,
			    stat,
			    ChanStateName[stat],
			    (void *)ast);
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
/**/
int res=0;

#ifdef CURLs
    res = ast_custom_function_unregister(&salara_curls);
    res |= ast_custom_function_unregister(&salara_curlopts);

    AST_TEST_UNREGISTER(salara_url);
#else
    curl_global_cleanup();
#endif
    //write_config(salara_config_file, 0);

    ast_mutex_lock(&salara_lock);

	delete_act_list();

	remove_chan_records();

	remove_records();

	if (salara_cli_registered) ast_cli_unregister_multiple(cli_salara, ARRAY_LEN(cli_salara));

	if (salara_manager_registered) ast_manager_unregister_hook(&hook);

	if (salara_app_registered) ast_unregister_application(app_salara);

	if (salara_atexit_registered) ast_unregister_atexit(salara_atexit);

	//-------   server   --------------------------------------------
	if (!reload) ast_tcptls_server_stop(&sami_desc);
	//----------------------------------------------------------------

    ast_mutex_unlock(&salara_lock);

//    ast_verbose("\t[v%s] Module '%s.so' unloaded OK.\n",SALARA_VERSION, AST_MODULE);

    return res;
}
//----------------------------------------------------------------------
static void salara_atexit(void)
{
    salara_cleanup();
}
//----------------------------------------------------------------------
static int load_module(void)
{
int res = 0, lenc = -1;
struct ast_sockaddr sami_desc_local_address_tmp;

//    ast_verbose("[%s] Load for * version %s\n",AST_MODULE,ast_get_version_num());


#ifdef CURLs
    if (!ast_module_check("res_curl.so")) {
	if (ast_load_resource("res_curl.so") != AST_MODULE_LOAD_SUCCESS) {
	    ast_log(LOG_ERROR, "Cannot load res_curl, so func_curl cannot be loaded\n");
	    return AST_MODULE_LOAD_DECLINE;
	}
    }
    res = ast_custom_function_register(&salara_curls);
    res |= ast_custom_function_register(&salara_curlopts);
    AST_TEST_REGISTER(salara_url);
#else
    curl_global_init(CURL_GLOBAL_ALL);
#endif

    gettimeofday(&salara_start_time, NULL);

    memset(dest_number,0,AST_MAX_EXTENSION);
    strcpy(dest_number, DEF_DEST_NUMBER);

    ast_mutex_lock(&salara_lock);

	ast_mutex_lock(&act_lock);
	    Act_ID = 0;
	ast_mutex_unlock(&act_lock);

	init_act_list();

	init_records();
	
	init_chan_records();

	if (ast_register_atexit(salara_atexit) < 0) {
	    salara_atexit_registered = 0;
	    ast_verbose("[%s] Unable to register ATEXIT\n",AST_MODULE);
	} else salara_atexit_registered = 1;

	memset(rest_server,0,PATH_MAX);
	lenc = read_config(salara_config_file, 0);// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	if (lenc <= 0) lenc = write_config(salara_config_file, 1);
	salara_config_file_len = lenc;
	//if (!strlen(rest_server)) strcpy(rest_server, DEFAULT_SRV_ADDR);

	if (ast_register_application(app_salara, app_salara_exec, app_salara_synopsys, app_salara_description) < 0) {
	    salara_app_registered = 0;
	    ast_verbose("[%s] Unable to register APP\n",AST_MODULE);
	} else salara_app_registered = 1;


	salara_manager_registered = 0; ast_manager_unregister_hook(&hook);
	ast_manager_register_hook(&hook); salara_manager_registered = 1;

	ast_cli_register_multiple(cli_salara, ARRAY_LEN(cli_salara));
	salara_cli_registered = 1;

	//-------   server   --------------------------
	if (!reload) {
	    ast_sockaddr_setnull(&sami_desc.local_address);
	    ast_sockaddr_parse(&sami_desc_local_address_tmp, rest_server, 0);
	    ast_sockaddr_copy(&sami_desc.local_address, &sami_desc_local_address_tmp);
	    ast_tcptls_server_start(&sami_desc);
	}
	//---------------------------------------------

    ast_mutex_unlock(&salara_lock);

    if (reload) reload=0;

//    ast_verbose("\t[v%s] Module '%s.so' loaded OK.\n", SALARA_VERSION, AST_MODULE);//, (int)salara_pid);

    return res;//AST_MODULE_LOAD_SUCCESS;
}
//----------------------------------------------------------------------

//----------------------------------------------------------------------
static int unload_module(void)
{
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

