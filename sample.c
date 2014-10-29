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
// comment out below line on home PC
//#include <sys/filio.h>
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
#define MAX_CLIENTS 1024
struct client_thread threads[MAX_CLIENTS];

// the number of connections we have open now
int connections_open=0;

pthread_rwlock_t message_log_lock = PTHREAD_RWLOCK_INITIALIZER;

#define MAX_MESSAGES 10000
char *message_log[MAX_MESSAGES];
char *message_log_recipients[MAX_MESSAGES];
char *message_log_senders[MAX_MESSAGES];
int message_count=0;

int message_log_append(char *sender, char *recipient, char *message) {
  if (message_count>=MAX_MESSAGES) return -1;
  pthread_rwlock_wrlock(&message_log_lock);

  //append the message here
  message_log_recipients[message_count]=strdup(recipient);
  message_log_senders[message_count]=strdup(sender);
  message_log[message_count]=strdup(message);
  message_count++;

  pthread_rwlock_unlock(&message_log_lock);
  return 0;
}

int message_log_read(struct client_thread *t) {
  pthread_rwlock_rdlock(&message_log_lock);

  //Read and process new messages in the log
  int i;
  for(i=t->next_message;i<message_count;i++){
    if(!strcasecmp(message_log_recipients[i],t->nickname)) {
      char msg[8192];
      snprintf(msg,8192,":%s PRIVMSG %s :%s\n",message_log_senders[i],message_log_recipients[i],message_log[i]);
      write(t->fd,msg,strlen(msg));
    }
  }
  t->next_message=message_count;

  pthread_rwlock_unlock(&message_log_lock);
  return 0;
}

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

int parse_line(struct client_thread *t, char *buffer) {
  char msg[1024];
  char channel[8192];
  char nickname[8192];
  if (!strncasecmp("QUIT",buffer,4)) {
    // client has said they are going away
    // if we dont close the connection, we will get a SIGPIPE that will kill our program
    // when we try to read from the socket again in the loop.
    snprintf(msg,1024,"ERROR :Closing Link: User quit\n");
    write(t->fd,msg,strlen(msg));
    close(t->fd);
    connections_open--;
    return -1;
  }

  // if user has not registerd, returns an error
  // else, connects user to channel
  int r=sscanf((char *)buffer,"JOIN %s",channel);   
  if(r==1) {
    if(!t->user_has_registered) {
      snprintf(msg,1024,":ircserver.com 241 * : JOIN command sent before registration\n");
      write(t->fd,msg,strlen(msg));
      return 0;
    } else {
      //join channel code
    }
  }

  // if user has not registered, return error
  if (!strncasecmp("PRIVMSG",buffer,7)) {
    if(!t->user_has_registered) {
      snprintf(msg,1024,":ircserver.com 241 * : PRIVMSG command sent before registration\n");
      write(t->fd,msg,strlen(msg));
      return 0;
    } else {
      // accept and process PRIVMSG
      char recipient[1024];
      char message[1024];
      char sender[1024];
      if (sscanf(buffer, "PRIVMSG %s :%[^\n]",recipient,message)==2) {
          snprintf(sender,1024,"%s!myusername@myserver",t->nickname);
          message_log_append(sender,recipient,message);
      } else {
        // malformed PRIVMSG command returns error
        snprintf(msg,1024,":ircserver.com 461 %s : Mal-formed PRIVMSG command sent\n",t->nickname);
        write(t->fd,msg,strlen(msg));
        return 0;
      }
    }
  }

  int n=sscanf((char *)buffer,"NICK %s",nickname);
  if(n) {
    if (strlen(nickname)<=32) {
    strcpy(t->nickname,nickname);
    registration_check(t);
    } else {
      snprintf(msg,1024,":ircserver.com 432 : Nickname too long\n");
      write(t->fd,msg,strlen(msg));       
    }
  }

  int u=sscanf((char *)buffer,"USER %s",channel);
  if(u==1) {
    t->user_command_seen=1;
    registration_check(t);
  }
  return 0;
}


// makes sure that the connections open is not greater than the maximum clients
// then proceeds to connection code
void *handle_connection(void *data) {
  struct client_thread *t=data;
  if(++connections_open>MAX_CLIENTS) {
    char msg[1024];
    snprintf(msg,1024,"ERROR :Closing Link: Client count too great\n");
    write(t->fd,msg,strlen(msg));
    close(t->fd);
    connections_open--;
  }
  connection(t);
  return 0;
}

int connection(struct client_thread *t) {
  int fd=t->fd;
  t->timeout=5;
  t->next_message=message_count;
  unsigned char buffer[8192];
  int length=0;
  char msg[1024];

  snprintf(msg,1024,":ircserver.com 020 * :gday m8\n");
  write(fd,msg,strlen(msg));

  int time_of_last_data=time(0);

  // should test for t->fd>=0 instead of 1
  while(1){
    length=0;
    // checks for messages for user in log
    message_log_read(t);
  	read_from_socket(fd,buffer,&length,8192,1);
    buffer[length]=0;
    if(length>0) time_of_last_data=time(0);
    // if time since last command is greater or equal to the timeout, close connection
    if(!length && (time(0)-time_of_last_data)>=t->timeout){
  	  snprintf(msg,1024,"ERROR :Closing Link: Connection timed out length=0\n");
  	  write(fd,msg,strlen(msg));
  	  close(fd);
      connections_open--;
  	  return 0;
  	}
    // parse each character of the line
    int i;
    for(i=0;i<length;i++) {
      // checks if there is a newline or return character
      if(buffer[i]=='\n'||buffer[i]=='\r'){
        // if there is a newline or return character, parse the line
        // checks if the line closes the connection, then exit the function
        // finally resets the line and linelength variables
        if(t->line_len>0 && parse_line(t,t->line)==-1)return 0;
        t->line_len=0;
        bzero(t->line,1024);
      } else {
        if (t->line_len<1024) {
          t->line[t->line_len++]=buffer[i];
          t->line[t->line_len]=0;
        }
      }
    }
    // if there are remaining bytes, parse the line
    // if the socket is closed, exit function
    if (t->line_len>0 && parse_line(t,(char *)buffer)==-1) return 0;
  }
  close(fd);
  return 0;
}

// checks if the user has sent both USER and NICK commands
// then sends user confirmation of connection
int registration_check(struct client_thread *t) 
{
  if (t->user_has_registered) return -1;
  if (t->user_command_seen&&t->nickname[0]) {
    // User has now met the registration requirements
    t->user_has_registered=1;
    t->timeout=60;
    char msg[1024];
    snprintf(msg,1024,":ircserver.com 001 %s : Gday\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 002 %s : mate.\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 003 %s : Welcome\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 004 %s : to the server.\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 253 %s : some unknown connections\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 254 %s : some channels formed.\n",t->nickname);
    write(t->fd,msg,strlen(msg));
    snprintf(msg,1024,":ircserver.com 255 %s : I have %i clients and some servers.\n",t->nickname,connections_open);
    write(t->fd,msg,strlen(msg));
    return 0;
  }
  return -1;
}

int main(int argc,char **argv) {
  signal(SIGPIPE, SIG_IGN);

  if (argc!=2) {
  fprintf(stderr,"usage: sample <tcp port>\n");
  exit(-1);
  }
  
  int master_socket = create_listen_socket(atoi(argv[1]));
  
  fcntl(master_socket,F_SETFL,fcntl(master_socket, F_GETFL, NULL)&(~O_NONBLOCK));  
  // allocates memory for an array of structs
  // creates thread for the handle connection function
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
