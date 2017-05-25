####################################################################
#
# 			salara - Asterisk REST API module
#
####################################################################

## Description

A simple module for Asterisk PBX that provides two main features:
1. Built-in HTTP REST API that accepts incoming connections and manages calls accordingly.
2. Built-in HTTP REST client that can connect to external REST API and manage incoming calls according to REST responses.

It is easily customizable and can serve as a base for integration of existing Asterisk installations with external data sources, like CRMs, databases or any other application with REST interface.


## Package files:

* salara.conf	- module configuration file

* Makefile	- make file (module compilation scenario)

* salara.c	- source (module source code)

Module name - salara.so

Required headers:
```
- asterisk headers files (asterisk package)
- libcurl headers files (libcurl-devel)
- libjansson headers files (libjansson-devel)
```
Required libraries:
```
- libcurl.so.4 or high (https://curl.haxx.se/download.html)
- libjansson.so.4 or high (http://www.digip.org/jansson/)
```

## Compilation and installation

make

Copy files :

* salara.so	to directory /usr/lib/asterisk/modules/	(or other asterisk module directory)

* salara.conf	to directory /etc/asterisk/		(asterisk confifuration directory)

Load module via asterisk CLI:
```
module load salara.so
```
The following message will be shown in the console:
```
Loaded salara.so
  == Registered application 'salara'
 Loaded salara.so => (Features: transfer call; make call; get status: exten.,peer,channel; send: command,message,post)
[salara 14:13:56.141] SEND_BY_EVENT Thread started (tid:2886441792).
```

Get available module commands:
```
core show help salara
```


# Module features


## 1. Direct HTTP JSON POST messages to salara module (port 5058)

### 1.1 Setup a call between two subscribers:
```
{
    "operator": "8003",
    "phone": "0000",
    "context": "alnik"
}
```
where,
* "operator" - calling party (caller) - required
* "phone" -  called party, external or internal (called) - required
* "context" - caller context - optional

Module algorithm :
*  Check the caller status, if ok caller call leg is established;
*  after caller party answers, called party leg is established.

Example of module response to this request:
```
{"result":0,"text":"Idle"}
```
where,
* "result" - caller party status code
* "text" - caller party status text.

### 1.2 Send text message to internal subscriber :
```
{
    "operator": "8003",
    "phone": "8002",
    "msg": "Message text",
    "context": "general"
}
```
where,
* "operator" - message recipient, the subscriber who receives the message - required
* "phone" - sender, the subscriber, who sends the message - required 
* "msg" - message text - required 
* "context" - recipient context - optional 

Module algorithm :
  - check the operator status, if ok - send the message

Example of module response to this request :
```
{"result":0,"text":"Idle"}
```
where,
* "result" - operator status code
* "text" - operator status text.

### 1.3 query status of extension subscriber (extension) :
```
{
    "exten": "8003",
    "context": "alnik"
}
```
where,
* "exten" - extension subscriber, which status is required- required
* "context" - exten context- optional

Example of module response to this request :
```
{"result":0,"text":"Idle"} - available
{"result":4,"text":"Unavailable"} - unavailable
{"result":-1,"text":"Unknown"} - error in numbering plan
```

### 1.4 query peer status (peer) :
```
{
    "peer": "8003",
    "context": "alnik"
}
```
where,
* "peer" - peer, which status is required - required
* "context" - peer context - optional

Example of module response to this request :
```
{"result":0,"text":"Reachable"} - available
{"result":0,"text":"Unknown"} - unavailable
{"result":-1,"text":"Error"} - error in numbering plan
```

### 1.5 query channel status (channel) :
```
{
    "chan": "SIP/8003-00000020"
}
```
where,
* "chan" - channel, which status is required - required

Example of module response to this request :
```
{"result":-1,"text":"Channel not present"} - the channel is not present
{"result":0,"text":"Up (6)"} - the channel is present (live) 
```

## 2. POST  requests, sent by the module to a third-party web server (CRM, or any other source of call management data) in JSON format

Requests are created on the following events (events) :

### 2.1 "Newchannel" - a new channel is created (for the calls, passed through the Transfer function of the module)
Example of (CRM) request : 
```
{"event":"Newchannel","chan":"SIP/8003-00000022","caller":"8003","exten":"0000","state":"DOWN"}
```
Example of HTTP response to this request :
```
{"result":0}
```

### 2.2 "Hangup" - the channel ceased functioning (for the calls, passed through the Transfer function of the module)
Example of (CRM) request :
```
{"event":"Hangup","chan":"SIP/8003-00000022","caller":"8003","exten":"0000","state":"UP"}
```
Example of HTTP response to this request  :
```
{"result":0}
```

### 2.3 "Newexten" - the caller status is changed (for the calls, passed through the Transfer function of the module)
Example of HTTP request :
```
{"event":"Newexten","chan":"SIP/8003-00000023","caller":"8003","exten":"0000","state":"RING"} - call the number "0000"
```
or
```
{"event":"Newexten","chan":"SIP/8003-00000023","caller":"8003","exten":"0000","state":"UP","app":"Playback"} - connect the number "0000" 
```
Example of HTTP response to this request  :
```
{"result":0}
```

### 2.4 "AgentConnect" - the agent answered a call from queue(for the calls, passed through the Transfer function of the module)
Example of HTTP request :
```
{"event":"AgentConnect","chan":"SIP/8003-00000025","caller":"8003","queue":"710","state":"UP","agent":"2222"} - connect the number "2222"
```
Example of HTTP response to this request  :
```
{"result":0}
```
