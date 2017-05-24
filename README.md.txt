###########################################################################################
#
# 			salara - Asterisk rest api module
#
###########################################################################################

## Package files:

* salara.conf	- module configuration file

* Makefile	- make файл (module compilation scenario)

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

* salara.conf	to directory /etc/asterisk/			(asterisk confifuration directory)

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
* "operator" - получатель сообщения, абонент, которому посылается сообщение - обязательное поле
* "phone" - отправител, абонент, от имени которого посылается сообщение - обязательное поле
* "msg" - текст сообщения -обязательное поле
* "context" - контекст получателя - не обязательное поле

Алгоритм поведения модуля :
  - проверяется статус получателя, если статус удовлетворительный - отправляется сообщение

Ответ модуля на такой запрос имеет следующий вид :
```
{"result":0,"text":"Idle"}
```
где,
* "result" - содержит статус получателя
* "text" - текстовая интерпретация статуса получателя.

### 1.3 Запрос статуса внутреннего абонента (extension) :
```
{
    "exten": "8003",
    "context": "alnik"
}
```
где,
* "exten" - внутренний абонент, статус которого запрашивается - обязательное поле
* "context" - контекст внутреннего абонента - не обязательное поле

Ответ модуля на такой запрос имеет следующий вид :
```
{"result":0,"text":"Idle"} - абонент доступен
{"result":4,"text":"Unavailable"} - абонент не доступен
{"result":-1,"text":"Unknown"} - абоненте отсутствует в плане нумерации
```

### 1.4 Запрос статуса пира (peer) :
```
{
    "peer": "8003",
    "context": "alnik"
}
```
где,
* "peer" - пир, статус которого запрашивается - обязательное поле
* "context" - контекст пира - не обязательное поле

Ответ модуля на такой запрос имеет следующий вид :
```
{"result":0,"text":"Reachable"} - peer доступен
{"result":0,"text":"Unknown"} - peer не доступен
{"result":-1,"text":"Error"} - peer отсутствует в плане нумерации
```

### 1.5 Запрос статуса канала (channel) :
```
{
    "chan": "SIP/8003-00000020"
}
```
где,
* "chan" - канал, статус которого запрашивается - обязательное поле

Ответ модуля на такой запрос имеет следующий вид :
```
{"result":-1,"text":"Channel not present"} - канал отсутствует
{"result":0,"text":"Up (6)"} - канал присутствует (живой), в состоянии разговора
```

## 2. POST запросы, посылаемые модулем стороннему вэб-серверу (CRM) в формате json

Запросы формируются по следующим событиям (events) :

### 2.1 "Newchannel" - создан новый канал (для вызовов, прошедших через функцию Transfer модуля)
на сервер (CRM) посылается примерно такой запрос :
```
{"event":"Newchannel","chan":"SIP/8003-00000022","caller":"8003","exten":"0000","state":"DOWN"}
```
сервер (CRM) должен прислать ответ примерно такого вида :
```
{"result":0}
```

### 2.2 "Hangup" - канал прекратил своё существование (для вызовов, прошедших через функцию Transfer модуля)
на сервер (CRM) посылается примерно такой запрос :
```
{"event":"Hangup","chan":"SIP/8003-00000022","caller":"8003","exten":"0000","state":"UP"}
```
сервер (CRM) должен прислать ответ примерно такого вида :
```
{"result":0}
```

### 2.3 "Newexten" - статус абонента caller изменился(для вызовов, прошедших через функцию Transfer модуля или для всех вызовов)
на сервер (CRM) посылается примерно такой запрос :
```
{"event":"Newexten","chan":"SIP/8003-00000023","caller":"8003","exten":"0000","state":"RING"} - вызов на номер "0000"
```
или такой
```
{"event":"Newexten","chan":"SIP/8003-00000023","caller":"8003","exten":"0000","state":"UP","app":"Playback"} - соединение с номером "0000"
```
сервер (CRM) должен прислать ответ примерно такого вида :
```
{"result":0}
```

