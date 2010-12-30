
/* 
    tinysocks - a small Unix SOCKS4 Proxy Server v0.2
    Author: Filip Jaros
    E-Mail: fjaros@asu.edu

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

typedef struct LList_sock_t
{
	int req;
	int cSock;
	int bSock;
	in_addr_t ip;
	struct LList_sock_t *prev;
	struct LList_sock_t *next;
} LList_sock;

typedef struct LList_t
{
	int lSock;
	in_addr_t ip;
	struct LList_t *next;
} LList;


char alport[128];
LList_sock *firstsock;

int isallowedport(unsigned short port)
{
	if (alport[0])
	{
		char *pch = strtok(alport, ",");
		while (pch)
		{
			if ((unsigned short)atoi(pch) == port)
				return 1;
			pch = strtok(NULL, ",");
		}
	}
	else
		return 1;
	return 0;
}

int getrandom(int from, int to)
{
	struct timeval t;
	gettimeofday(&t, NULL);
	srand(t.tv_sec + t.tv_usec);
	return rand() % ((to + 1) - from) + from;
}

int myresponse(char *buf, int good)
{
	buf[0] = 0x00;
	if (good)
		buf[1] = 0x5A;
	else
		buf[1] = 0x5B;
	return 8;  	
}

LList_sock *cancelsock(int *maxfd, fd_set *fds, LList_sock *list)
{
	if (list)
	{
		if (list->cSock)
		{
			close(list->cSock);
			FD_CLR(list->cSock, fds);
		}
		if (list->bSock)
		{
			close(list->bSock);
			FD_CLR(list->bSock, fds);
		}
		if (*maxfd == list->cSock || *maxfd == list->bSock)
		{
			while (FD_ISSET(*maxfd, fds) == 0)
				*maxfd -= 1;
		}
		if (list->prev)
			list->prev->next = list->next;
		else
			firstsock = list->next;
		if (list->next)
			list->next->prev = list->prev;
			
		LList_sock *temp = list->next;
		free(list);
		list = NULL;
		return temp;
	}
	return NULL;
}

void ts_go(char *ip_b, int range_b, int range_e, unsigned short port, int debug)
{
	int i;
	int on = 1;
	int maxfd = -1;
	int ip_blen = strlen(ip_b);

	char ip[32];
	strcpy(ip, ip_b);

	fd_set ms, fs;
	FD_ZERO(&ms);
	
	struct sockaddr_in sa;
	memset(&sa, '\0', sizeof(struct sockaddr_in));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);

	LList *first = NULL;
	firstsock = NULL;

	for (i = range_b; i <= range_e; i++)
	{
		LList *list = (LList*)malloc(sizeof(LList));
		memset(list, '\0', sizeof(LList));
		list->next = first;
		first = list;

		sprintf(ip + ip_blen, ".%d", i);
		in_addr_t ipaddr = inet_addr(ip);
		list->ip = ipaddr;
		sa.sin_addr.s_addr = ipaddr;

		list->lSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (list->lSock < 0)
		{
			if (debug) printf("socket() died on %s - %s\n", ip, strerror(errno));
			return;
		}

		if (setsockopt(list->lSock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(int)))
		{
			if (debug) printf("setsockopt() died on %s - %s\n", ip, strerror(errno));
			return;
		}

		if (ioctl(list->lSock, FIONBIO, (char*)&on) == -1)
		{
			if (debug) printf("ioctl() died on %s - %s\n", ip, strerror(errno));
			return;
		}

		if (bind(list->lSock, (struct sockaddr*)&sa, sizeof(struct sockaddr)))
		{
			if (debug) printf("bind() died on %s - %s\n", ip, strerror(errno));
			return;
		}

		if (listen(list->lSock, SOMAXCONN))
		{
			if (debug) printf("listen() died on %s - %s\n", ip, strerror(errno));
			return;
		}

		FD_SET(list->lSock, &ms);
		if (list->lSock > maxfd)
			maxfd = list->lSock;
	}

	for (;;)
	{
startloop:
		memcpy(&fs, &ms, sizeof(fd_set));
	
		int s = select(maxfd + 1, &fs, NULL, NULL, NULL);
		if (s > 0)
		{
			int isset = 0;
			LList *list = first;
			while (list)
			{
				if (FD_ISSET(list->lSock, &fs))
				{
					isset++;
					int newsock;
					do
					{
						newsock = accept(list->lSock, NULL, NULL);
						if (newsock >= 0)
						{
							if (ioctl(newsock, FIONBIO, (char*)&on) != -1)
							{
								FD_SET(newsock, &ms);
								if (newsock > maxfd)
									maxfd = newsock;
		
								LList_sock *ns = (LList_sock*)malloc(sizeof(LList_sock));
								memset(ns, '\0', sizeof(LList_sock));
								if (firstsock)
								{
									firstsock->prev = ns;
									ns->next = firstsock;
								}
								firstsock = ns;
								ns->ip = list->ip;
								ns->cSock = newsock;
								ns->req = 1;
							}
							else
							{
								if (debug) printf("csock ioctl() died on %s\n", strerror(errno));
								close(newsock);
								newsock = -1;
							}
						}
						else
						{
							if (errno != EWOULDBLOCK)
								if (debug) printf("accept() died on %s\n", strerror(errno));
						}
					} while (newsock != -1);
					if (isset == s)
						goto startloop;
				}
				list = list->next;
			}
			
			LList_sock *listsock = firstsock;
			while (listsock)
			{
				int cancel = 0;
				if (FD_ISSET(listsock->cSock, &fs))
				{
					isset++;
					char buf[1024];
					int recvlen;
					do
					{
						recvlen = recv(listsock->cSock, buf, sizeof(buf), 0);
						if (recvlen < 0)
						{
							if (errno != EWOULDBLOCK)
							{
								if (debug) printf("recv() died on cSock %s\n", strerror(errno));
								cancel = 1;
							}
						}
						else if (recvlen == 0)
						{
							if (debug) printf("cSock recvlen == 0 cancel\n");
							cancel = 1;
						}
						else
						{
							if (listsock->req == 1 && recvlen >= 8 && buf[0] == 0x04 && buf[1] == 0x01)
							{
								listsock->req = 0;
								port = ntohs(*(unsigned short*)(buf + 2));
								if (isallowedport(port))
								{
									listsock->bSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
									if (listsock->bSock >= 0)
									{
										struct sockaddr_in bs;
										memset(&bs, '\0', sizeof(struct sockaddr_in));
										bs.sin_family = AF_INET;
										bs.sin_addr.s_addr = listsock->ip;
	
										if (bind(listsock->bSock, (struct sockaddr*)&bs, sizeof(struct sockaddr)) == 0)
										{
											bs.sin_port = *(unsigned short*)(buf + 2);
											bs.sin_addr.s_addr = *(in_addr_t*)(buf + 4);
											if (connect(listsock->bSock, (struct sockaddr*)&bs, sizeof(struct sockaddr)) == 0)
											{
												if (ioctl(listsock->bSock, FIONBIO, (char*)&on) != -1)
												{
													FD_SET(listsock->bSock, &ms);
													if (listsock->bSock > maxfd)
														maxfd = listsock->bSock;
													int sendlen = myresponse(buf, 1);
													if (send(listsock->cSock, buf, sendlen, 0) < 0)
													{
														if (debug) printf("cSock socks4 req send() failed %s\n", strerror(errno));
														cancel = 1;											
													}
												}
												else
												{
													if (debug) printf("bSock ioctl() died on %s\n", strerror(errno));
													cancel = 1;
												}
											}
											else
											{
												if (debug) printf("connect() failed to %s:%u %s\n", inet_ntoa(bs.sin_addr), ntohs(port), strerror(errno));
												int sendlen = myresponse(buf, 0);
												send(listsock->cSock, buf, sendlen, 0);
												cancel = 1;
											}
										}
										else
										{
											if (debug) printf("bind() died on socks4 req %s\n", strerror(errno));
											cancel = 1;
										}	
									}
									else
									{
										if (debug) printf("bSock socket() failed\n");
										cancel = 1;
									}
								}
								else
								{
									if (debug) printf("not allowed port atttempt %u\n", port);
									int sendlen = myresponse(buf, 0);
									send(listsock->cSock, buf, sendlen, 0);
									cancel = 1;
								}
							}
							else
							{
								if (listsock->bSock)
								{
									if (send(listsock->bSock, buf, recvlen, 0) < 0)
									{
										if (debug) printf("bSock send() failed %s\n", strerror(errno));
										cancel = 1;
									}
								}
								else
								{
									if (debug) printf("cSock recvlen > 0 but listsock->bSock == 0\n");
									cancel = 1;
								}
							}	
						}
					} while (cancel == 0 && recvlen > 0);
					if (cancel)
						listsock = cancelsock(&maxfd, &ms, listsock);
					if (isset == s)
						goto startloop;
				}

				if (cancel == 0 && FD_ISSET(listsock->bSock, &fs))
				{
					isset++;
					char buf[1024];
					int recvlen;
					do
					{
						recvlen = recv(listsock->bSock, buf, sizeof(buf), 0);
						if (recvlen < 0)
						{
							if (errno != EWOULDBLOCK)
							{
								if (debug) printf("recv() died on bSock %s\n", strerror(errno));
								cancel = 1;
							}
						}
						else if (recvlen == 0)
						{
							if (debug) printf("bSock recvlen == 0 cancel\n");
							cancel = 1;
						}
						else
						{
							if (listsock->cSock)
							{
								if (send(listsock->cSock, buf, recvlen, 0) < 0)
								{
									if (debug) printf("cSock send() failed %s\n", strerror(errno));
									cancel = 1;
								}
							}
							else
							{
								if (debug) printf("bSock recvlen > 0 but listsock->cSock == 0\n");
								cancel = 1;
							}
						}
					} while (cancel == 0 && recvlen > 0);
					if (cancel)
						listsock = cancelsock(&maxfd, &ms, listsock);
					if (isset == s)
						goto startloop;
				}
				if (cancel == 0)
					listsock = listsock->next;
			}
		}
		else
			if (debug) printf("select returned <= 0 %s\n", strerror(errno));
	}
}

int main(int argc, char **argv)
{
	char ip_b[32];
	int i, range_b = 0, range_e = 0, debug = 0;
	unsigned short port = 0;

	for (i = 1; i != argc; i++)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
				case 'd':
				{
					debug = 1;
					break;
				}
				case 'r':
				{
					strcpy(ip_b, argv[i] + 3);
					break;
				}
				case 'b':
				{
					range_b = atoi(argv[i] + 3);			
					break;
				}
				case 'e':
				{
					range_e = atoi(argv[i] + 3);
					break;
				}
				case 'a':
				{
					strcpy(alport, argv[i] + 3);
					break;
				}
				case 'p':
				{
					port = (unsigned short)atoi(argv[i] + 3);
					break;
				}
			}
		}
	}

	ts_go(ip_b, range_b, range_e, port, debug);

	return 0;
}
