#undef win32

#ifdef win32
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <time.h>
#include <windows.h>
#include <fcntl.h>
#include <unistd.h>
#include <winsock.h>
#include <ws2tcpip.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/msg.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <asm/termbits.h>
#endif

#define buf_size 2048
#define sms_count 4

/*--------------------------------------------------------------------*/

int fd_log=-1;

/*--------------------------------------------------------------------*/
void print_cdr(char *st)
{
    printf(st);
}
/*--------------------------------------------------------------------*/
/*--------------------------------------------------------------------*/

int main (int argc, char *argv[]) {
  time_t ttm;
  char *abra;
//  const char *sms[sms_count] = {"\n0\n",
//			    "\n00\n",
//			    "\n1234\n",
//			    "\n8001\n"};
  int connsocket=-1;
  int client=-1;
  unsigned char from_client[buf_size], to_client[buf_size];
  fd_set Fds, read_Fds;
  char * stri, *uks=NULL, *uke=NULL, *body_uk=NULL;
  char stril[64]={0}, chap[256]={0}, server_port[64]={0}, strfrom[2048]={0};
  unsigned short tcp_port=8080, word_len=0;
  struct sockaddr_in srv_conn, cli_conn;
  socklen_t srvlen,clilen;
  struct timeval mytv,cli_tv;
  int resa, lenrecv=0, lenrecv_tmp=0, ready=0, Vixod=0, uk=0;
  int on, sms_ind=0, post_len=0, pl=0, post_flag=0;
  unsigned int total=0;

#ifdef win32
  u_long ul=1;
  WSADATA WSAData;                /* Содержит детали реализации WinSock */
#else
  int flag;
#endif
/**********************************************************************/
/*
1 : server_port
*/
    if (argc<2) {
        printf ("ERROR: you must enter port number\nfor example:srv 8080\n\n");
        return 1;
    }
// rem
    strcpy(server_port,argv[1]);
    resa=atoi(server_port);
    if (resa<=0) resa=8080; tcp_port=resa;

    ttm=time(NULL); abra=ctime(&ttm); abra[strlen(abra)-1]=0;
    sprintf(chap,"%s Start server emul. application (port=%d)\n",abra,tcp_port);
    print_cdr(chap);

#ifdef win32
    if (WSAStartup (MAKEWORD(1,1), &WSAData) != 0) {
        memset(chap,0,256);
        sprintf(chap,"WSAStartup failed. Error: %d", WSAGetLastError ());
        print_cdr(chap);
        if (fd_log>0) close(fd_log);
        return 1;
    }
#endif

while(1<2) {

    Vixod=0;
    connsocket=socket(AF_INET,SOCK_STREAM,6);

    if (connsocket < 0) {
        memset(chap,0,256);
        sprintf(chap,"ERROR: open socket (%d)\n",connsocket);
        print_cdr(chap);
        if (fd_log>0) close(fd_log);
        return 1;
    }

    on = 1;
    if (setsockopt(connsocket, SOL_SOCKET, SO_REUSEADDR,(const char *) &on, sizeof(on))) {
        memset(chap,0,256);
        sprintf(chap,"ERROR: Setsockopt - SO_REUSEADDR (%d)\n",connsocket);
        print_cdr(chap);
        if (fd_log>0) close(fd_log);
        return 1;
    }

    srvlen = sizeof(struct sockaddr_in);
    memset(&srv_conn,0,srvlen);
    srv_conn.sin_family = AF_INET;
    srv_conn.sin_addr.s_addr=htonl(INADDR_ANY);
    srv_conn.sin_port=htons(tcp_port);

    if (bind(connsocket, (struct sockaddr *) &srv_conn, srvlen) == -1) {
	memset(chap,0,128);
	sprintf(chap,"ERROR: Bind [port %d].\n", tcp_port);
	print_cdr(chap);
	goto out_of_job;
    }

    if (listen(connsocket, 3) == -1) {
	memset(chap,0,128);
	sprintf(chap,"ERROR: Listen [port %d].\n", tcp_port);
	print_cdr(chap);
	goto out_of_job;
    }
/*
#ifdef win32
    ioctlsocket(connsocket,FIONBIO,&ul);
#else
    flag = fcntl(connsocket, F_GETFL);
    fcntl(connsocket, F_SETFL, flag | O_NONBLOCK);
#endif
*/
    memset(chap,0,128);
    sprintf(chap,"Listen OK. Listening auth. client on data_port %d...\n", tcp_port);
    print_cdr(chap);

    clilen = sizeof(struct sockaddr_in);

  while (!Vixod) {
	resa=0;
	FD_ZERO(&Fds); FD_SET(connsocket,&Fds);
	mytv.tv_sec=0; mytv.tv_usec=250000;
	resa=select(connsocket+1,&Fds,NULL,NULL,&mytv);
	if (resa > 0) {
	    client = accept(connsocket, (struct sockaddr *) &cli_conn, &clilen);
      if (client>0) {
/**/
#ifdef win32
		ioctlsocket(client,FIONBIO,&ul);
#else
		flag = fcntl(client, F_GETFL);
		fcntl(client, F_SETFL, flag | O_NONBLOCK);
#endif
/**/
		memset(chap,0,128);
		sprintf(chap,"Client ");
		memset(stril,0,64);
		stri=(char *)inet_ntoa(cli_conn.sin_addr);
		sprintf(stril,"%s",stri);
		sprintf(chap+strlen(chap),"%s",stril);
		sprintf(chap+strlen(chap),":");
		sprintf(chap+strlen(chap),"%u",htons(cli_conn.sin_port));
		sprintf(chap+strlen(chap)," online\n");
		print_cdr(chap);
		/***************************************/
		memset(from_client,0,buf_size);
		lenrecv=lenrecv_tmp=0; ready=0; Vixod=0; uk=0; pl=post_len=0;
		while (!Vixod) {
		    cli_tv.tv_sec=0; cli_tv.tv_usec=500000;
		    FD_ZERO(&read_Fds); FD_SET(client,&read_Fds);
		    if (select(client+1, &read_Fds, NULL, NULL, &cli_tv)>0) {
			
                	lenrecv_tmp=recv(client, &from_client[uk], 1, 0);
                	if (lenrecv_tmp==0) {
                            Vixod=1;
                	} else if (lenrecv_tmp>0) {
                    	    lenrecv += lenrecv_tmp; uk += lenrecv_tmp;
                    	    if (!post_len) {
                    		uks = strstr((char *)from_client,"Content-Length: ");
                    		if (uks) {
                    		    uks += 16;
                    		    uke = strstr(uks,"\r\n");
                    		    if (uke) {
                    			memset(chap,0,256);
                    			memcpy(chap, uks, uke-uks);
                    			post_len = atoi(chap);
                    			//printf("post_len=%d\n",post_len);
                    		    }
                    		} else if (strstr((char *)from_client, "POST ")) post_flag=1;
                    	    } 
                    	    if (post_len) {
                    		if (!body_uk) {
                    		    body_uk = strstr((char *)from_client,"\r\n\r\n");
                    		    if (body_uk) {
                    			pl = post_len + 4;// + 4;
                    		    }
                    		}
                    	    //else if (strstr((char *)from_client,"\n\n")) ready=1;
                    	    } else {
                    		if ((!post_flag) && (strstr((char *)from_client,"\r\n\r\n"))) ready=1;
                    	    }
                    	    if (pl) {
                    		if ((char *)(from_client+uk) >= (char *)(body_uk+pl)) ready=1;
                    	    }
                	}
                	if (ready) {
                	    total++;
                	    sprintf(chap,"\n------------  total=%d  ------------\n",total); print_cdr(chap);
                	    print_cdr((char *)from_client);
                    	   //---------------------------
                    	   if (lenrecv>0) {//answer to client
//sleep(5);
				    memset(to_client,0,buf_size);
				    //strcpy((char *)to_client,sms[sms_ind]);
				    sprintf((char *)to_client,"{\"result\":0}");
				    word_len = strlen((char *)to_client);
				    send(client, &to_client[0], word_len, 0);
                    		    memset(strfrom,0,2048);
				    sprintf(strfrom,"\nSend (%d) answer : %s\n",word_len,to_client);
				    //for (tr=0; tr<word_len; tr++)
				//	sprintf(strfrom+strlen(strfrom),"%02X ",(unsigned char)to_client[tr]);
                    		//    sprintf(strfrom+strlen(strfrom),"\n");
                		    print_cdr(strfrom);
                		    sms_ind++; sms_ind &= 3;
                    	   }
                    	   //--------------------------
                    	   lenrecv=lenrecv_tmp=0; ready=0; uk=0;
                    	   memset(from_client,0,buf_size);
                    	   post_len=pl=0; post_flag=0;
                    	   body_uk=NULL;
                    	   //usleep (100000);
                    	   Vixod=1;
                    	   break;
                	}
                   }
	      }
		/***************************************/
      	   }/*if (client>0)*/
	}/*if (resa>0)*/
  }/*while (!Vixod)*/
/*
memset(chap,0,128);
sprintf(chap,"Client disconnect\n");
print_cdr(chap);
usleep(250000);
*/
out_of_job:
#ifdef win32
	if (client>0) closesocket(client);
	if (connsocket>0) closesocket(connsocket);
}/*while(1<2)*/
	WSACleanup();               /* Завершаем Windows-сессию */
#else
	if (client>0) close(client);
	if (connsocket>0) close(connsocket);
}/*while(1<2)*/
#endif
	if (fd_log>0) close(fd_log);
    return 0;
}

