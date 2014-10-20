/*
  Sample solution for NOS 2014 assignment: implement a simple multi-threaded 
  IRC-like chat service.

  (C) Paul Gardner-Stephen 2014.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_FILIO_H
	#include <sys/filio.h>
#endif
#include <sys/ioctl.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>

struct client_thread {
  pthread_t thread;
  int thread_id;
  int fd;

  char nickname[32];

  int state;
  int user_command_seen;
  int user_has_registered;
  time_t timeout;

  char line[1024];
  int line_len;

  int next_message;
};

// allocate static structure for all client connections
int MAX_CLIENTS=1000;
struct client_thread threads[1000];

// the number of connections we have open now
int connections_open=0;

pthread_rwlock_t message_log_lock;

int read_from_socket(int sock,unsigned char *buffer,int *count,int buffer_size,
		     int timeout)
{
  fcntl(sock,F_SETFL,fcntl(sock, F_GETFL, NULL)|O_NONBLOCK);


  int t=time(0)+timeout;
  if (*count>=buffer_size) return 0;
  int r=read(sock,&buffer[*count],buffer_size-*count);
  //NOSNOTE need count to be some value
  while(r!=0) {
    if (r>0) {
      (*count)+=r;
      break;
    }
    r=read(sock,&buffer[*count],buffer_size-*count);
    if (r==-1&&errno!=EAGAIN) {
      perror("read() returned error. Stopping reading from socket.");
      return -1;
    } else usleep(100000);
    // timeout after a few seconds of nothing
    if (time(0)>=t) break;
  }
  buffer[*count]=0;
  return 0;
}

int create_listen_socket(int port)
{
  int sock = socket(AF_INET,SOCK_STREAM,0);
  if (sock==-1) return -1;

  int on=1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) == -1) {
    close(sock); return -1;
  }
  if (ioctl(sock, FIONBIO, (char *)&on) == -1) {
    close(sock); return -1;
  }
  
  /* Bind it to the next port we want to try. */
  struct sockaddr_in address;
  bzero((char *) &address, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  if (bind(sock, (struct sockaddr *) &address, sizeof(address)) == -1) {
    close(sock); return -1;
  } 

  if (listen(sock, 20) != -1) return sock;

  close(sock);
  return -1;
}

int accept_incoming(int sock)
{
  struct sockaddr addr;
  unsigned int addr_len = sizeof addr;
  int asock;
  if ((asock = accept(sock, &addr, &addr_len)) != -1) {
    return asock;
  }

  return -1;
}

int clientcount;

void *handle_connection(void *data) {
  /*if(clientcount>MAX_CLIENTS) {
    char msg[1024];
    snprintf(msg,1024,"ERROR :Closing Link: Client count too great\n");
    write(fd,msg,strlen(msg));
    close(fd);
  }*/
  struct client_thread *t=data;
  connection(t->fd);
  return 0;
}

int connection(int fd) {
  char msg[1024];
  char channel[8192];
  char username[8192];
  int registered=0;
  unsigned char buffer[8192];
  int length=0;
  snprintf(msg,1024,":ircserver.com 020 * :gday m8\n");
  write(fd,msg,strlen(msg));
  while(1){
  	length=0;
  	read_from_socket(fd,buffer,&length,8192,5);
  	if(!length&&!registered){
  	  snprintf(msg,1024,"ERROR :Closing Link: Connection timed out length=0\n");
  	  write(fd,msg,strlen(msg));
  	  close(fd);
  	  return 0;
  	}

  	buffer[length]=0;
  	int r=sscanf((char *)buffer,"JOIN %s",channel); 	
  	if(r==1) {
  	  if(!registered) {
  	    snprintf(msg,1024,":ircserver.com 241 * : JOIN command sent before registration\n");
  	    write(fd,msg,strlen(msg));
      }
  	}
  	if (!strncasecmp("PRIVMSG",buffer,7)) {
        if(!registered) snprintf(msg,1024,":ircserver.com 241 * : PRIVMSG command sent before registration\n");
        else snprintf(msg,1024,":ircserver.com PRIVMSG %s PRIVMSG command sent after registration\n", username);//this shouldnt be working
        write(fd,msg,strlen(msg));
  	}
  	if (!strncasecmp("QUIT",buffer,4)) {
  		// client has said they are going away
  		// if we dont close the connection, we will get a SIGPIPE that will kill our program
  		// when we try to read from the socket again in the loop.
  	  snprintf(msg,1024,"ERROR :Closing Link: Connection timed out (bye bye)\n");
  	  write(fd,msg,strlen(msg));
  		close(fd);
  		return 0;
  	}
    int n=sscanf((char *)buffer,"NICK %s",username);
    int u=sscanf((char *)buffer,"USER %s",channel);
    if(u==1) {
      //insert proper codes
      snprintf(msg,1024,":ircserver.com 001 %s : Gday\n",username);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 002 %s : mate.\n",username);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 003 %s : Welcome\n",username);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 004 %s : to %s.\n",username,channel);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 253 %s : Enjoy\n",username);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 254 %s : your\n",username);
      write(fd,msg,strlen(msg));
      snprintf(msg,1024,":ircserver.com 255 %s : stay.\n",username);
      write(fd,msg,strlen(msg));
      registered=1;
      //handle_registered(fd,username);
    }
  }
  close(fd);
  return 0;
}


int handle_registered(int fd, char *username) {
  char msg[1024];
  unsigned char buffer[8192];
  int length=0;
  while(1) {
    read_from_socket(fd,buffer,&length,8192,60);
    if (!strncasecmp("QUIT",buffer,4)) {
      // client has said they are going away
      // if we dont close the connection, we will get a SIGPIPE that will kill our program
      // when we try to read from the socket again in the loop.
      snprintf(msg,1024,"ERROR :Closing Link: Connection timed out (bye bye)\n");
      write(fd,msg,strlen(msg));
      close(fd);
      return 0;
    }
  }
  close(fd);
  return 0;
}

int main(int argc,char **argv) {
  signal(SIGPIPE, SIG_IGN);

  if (argc!=2) {
  fprintf(stderr,"usage: sample <tcp port>\n");
  exit(-1);
  }
  
  int master_socket = create_listen_socket(atoi(argv[1]));
  
  fcntl(master_socket,F_SETFL,fcntl(master_socket, F_GETFL, NULL)&(~O_NONBLOCK));  
  
  while(1) {
    int client_sock = accept_incoming(master_socket);
    if (client_sock!=-1) {
      struct client_thread *t=calloc(sizeof(struct client_thread),1);
      if(t!=NULL){
        t->fd=client_sock;
        int err = pthread_create(&t->thread,NULL,handle_connection,(void*)t);
        if (err) close(client_sock);
      }
      else usleep(10000);
    }
  }
}
