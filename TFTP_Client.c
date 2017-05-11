#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <errno.h>
#define MAX_RETRY 5
#define TIMEOUT 2
void error(char *s)
{
	perror(s);
	exit(1);
}
int read_wr_req(int choice,char *file,char *mode,int sock,struct sockaddr_in server)
{
	unsigned char opcode[2];
	int bytesSent;
	int wo = 0;
	if(choice == 1)
	{
		opcode[0]=0x00;
		opcode[1]=0x01;
	}
	else
	{
		opcode[0]=0x00;
		opcode[1]=0x02;
	}
	char pkt[516]="";
	// Pack the header format
	pkt[wo++] = opcode[0];
	pkt[wo++] = opcode[1];
	memcpy(&pkt[wo], file, strlen(file));
	wo += strlen(file);
	pkt[wo++] = 0x00;
	memcpy(&pkt[wo], mode, strlen(mode));
	wo += strlen(mode);
	pkt[wo++] = 0x00;
	bytesSent = sendto(sock,pkt,wo,0,(struct sockaddr *)&server,sizeof(struct sockaddr));
	if(bytesSent!=wo)
	{
		return -1;
	}
	return 1;
	
}

int sendfile(char *file, int sock, struct sockaddr_in server)
{
	int retry;
	int block_no,fsize;
	struct timeval time;
	unsigned char fileBuff[512] = "", readServBuff[516] = "",pkt[516]="";
	FILE *fp = fopen(file,"rb");
	if(fp==NULL)
	{

		perror("no such file");
		return -3;
	}
	int n;
	n = read_wr_req(2,file,"octet",sock,server);
	if(n<0)
	{
		perror("Could not sent wrq");
		return -1;
	}
	for(retry=1;retry<=MAX_RETRY;retry++)
	{
		time.tv_sec = TIMEOUT;  //2 sec timeout//
		time.tv_usec = 0;  // Not init'ing this can cause strange errors
		socklen_t socklen = sizeof(struct sockaddr);
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&time, sizeof(struct timeval));
		n = recvfrom(sock, readServBuff, 516, 0, (struct sockaddr*)&server, &socklen);
		if(n==-1&& (errno == EAGAIN || errno == EWOULDBLOCK))
		{
			perror("Timeout waiting for ack, resending wrq");
			if(read_wr_req(2,file,"octet",sock,server)<0)
				return -1;
		}
		else if (readServBuff[0]==0x00 && readServBuff[1]!=0x04)
		{
			if (readServBuff[0]==0x00 && readServBuff[1]==0x05)
			{
				perror("Error message received from server,retrying...");
				printf("error code : %x%x  error message : %s", readServBuff[2], readServBuff[3], readServBuff + 4);
				if(read_wr_req(2,file,"octet",sock,server)<0)
					return -1;
			}
		}
		else 
			break;
	}
	if(retry>MAX_RETRY)
	{
		perror("Ack could not be received");
		return -1;
	}
	block_no = 1;
	int end = 0;
	while(!end)
	{
		bzero(fileBuff,512);
		fsize = fread(fileBuff,1,512,fp);
		if(fsize<512)
			end = 1;
		bzero(pkt,516);
		//filling the opcode
		pkt[0] = 0x00;
		pkt[1] = 0x03;
		char *p = &block_no;
		unsigned char block[2];
		block[1] = p[0];
		block[0] = p[1];
		pkt[2] = block[0];
		pkt[3] = block[1];
		memcpy(pkt+4,fileBuff,fsize);
		block_no++;
		n=sendto(sock, pkt, fsize+4, 0, (struct sockaddr*)&server, sizeof(struct sockaddr));
		for(retry=1;retry<=MAX_RETRY;retry++)
		{
			time.tv_sec = TIMEOUT;  //1 sec timeout//
			time.tv_usec = 0;  // Not init'ing this can cause strange errors
			socklen_t socklen = sizeof(struct sockaddr);
			setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&time, sizeof(struct timeval));
			bzero(readServBuff,516);
			n = recvfrom(sock, readServBuff, 516, 0, (struct sockaddr*)&server, &socklen);
			if(n==-1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{	
				perror("Timeout waiting for ack,resending data");
				sendto(sock, pkt, fsize+4, 0, (struct sockaddr*)&server, sizeof(struct sockaddr));
			}
			else if (readServBuff[0]==0x00 && readServBuff[1]==0x05)
			{
				perror("Error message received from server,resending data");
				printf("error code : %x%x  error message : %s", readServBuff[2], readServBuff[3], readServBuff + 4);
				sendto(sock, pkt, fsize+4, 0, (struct sockaddr*)&server, sizeof(struct sockaddr));
			}
			else if(readServBuff[0]==0x00 && readServBuff[1]==0x04)
			{
				if(readServBuff[2]==block[0]&&readServBuff[3]==block[1])
				{
					printf("Ack received for block no. %2x%2x\n",readServBuff[2],readServBuff[3]);
					break;
				}
				else
				{
					printf("Error: Ack received for wrong block,resending data");
					sendto(sock, pkt, fsize+4, 0, (struct sockaddr*)&server, sizeof(struct sockaddr));
				}
			}
		}
		if(retry>MAX_RETRY)
		{
			printf("Ack for block %d could not be received",block_no-1);
			return -1;
		}
	}
	fclose(fp);
	return 1;
}
int recvfile(char *file,int sock,struct sockaddr_in server)
{
	char *p;
	int retry;
	unsigned char fileBuff[512] = "", readServBuff[516] = "",pkt[516]="";
	FILE *fp = fopen(file,"wb");
	if(fp==NULL)
	{
		perror("No such file");
		return -1;
	}
	int n;
	n = read_wr_req(1,file,"octet",sock,server);
	if(n<0)
	{
		perror("Could not sent rrq");
		return -1;
	}
	int dataflag = 0;
	int block_no = 1;
	struct timeval time;
	int end = 0;
	unsigned char block[2];
	while(!end)
	{
		p = &block_no;
		block[0]=p[1];
		block[1]=p[0];
		time.tv_sec = TIMEOUT;  
		time.tv_usec = 0;  
		socklen_t socklen = sizeof(struct sockaddr);
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&time, sizeof(struct timeval));
		bzero(readServBuff,516);
		for(retry=1;retry<=MAX_RETRY;retry++)
		{
			n = recvfrom(sock, readServBuff, 516, 0, (struct sockaddr*)&server, &socklen);
			if(n==-1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				printf("Timeout waiting for data packet %d, ",block_no);
				if(dataflag)
				{
					printf("resending ack %d\n",block_no);
					sendto(sock,pkt,4,0,(struct sockaddr *)&server,sizeof(server));
				}
				else
				{
					printf("resending rrq\n");	
					if(read_wr_req(1,file,"octet",sock,server)<0)
						return -1;
				}
			}
			else if (readServBuff[0]==0x00 && readServBuff[1]!=0x03)
			{
				if(readServBuff[0]==0x00 && readServBuff[1]!=0x05)
				{
					perror("Error message received from server, retrying\n");
					printf("error code : %x%x  error message : %s", readServBuff[2], readServBuff[3], readServBuff + 4);
					if(!dataflag)
					{
						printf("resending rrq\n");	
						if(read_wr_req(1,file,"octet",sock,server)<0)
							return -1;
					}
					else
					{
						printf("resending ack %d\n",block_no);
						sendto(sock,pkt,4,0,(struct sockaddr *)&server,sizeof(server));		
					}

				}
			}
			else if (readServBuff[0]==0x00 && readServBuff[1]==0x03)
			{
				if(!(readServBuff[2]==block[0]&&readServBuff[3]==block[1]))
				{
					printf("Expected data block %d not received, resending ack\n",block_no );
					sendto(sock,pkt,4,0,(struct sockaddr *)&server,sizeof(server));
				}
				else
					break;
			}
		}
		if(retry>MAX_RETRY)
		{
			printf("could not receive block %d\n",block_no);
			return -1;
		}
		block_no++;	
		dataflag = 1;
		fwrite(readServBuff+4,1,n-4,fp);
		
		if(n<516)
			end=1;
		pkt[0] = 0x00;
		pkt[1] = 0x04;
		pkt[2] = block[0];
		pkt[3] = block[1];
		n = sendto(sock,pkt,4,0,(struct sockaddr *)&server,sizeof(server));
		if(n!=4)
		{
			perror("Error in sending ack");
			return -1;
		}
	}
	fclose(fp);
	return 1;
}
int main(int argc, char *argv[])
{
	if (argc < 2) 
	{
       fprintf(stderr,"usage %s hostname\n", argv[0]);
       exit(0);
    }
    char input[64] = "", filename[32] = "";
	char *destIP = argv[1];
	short port = 69;
	//int connFlag,bytesSent,buffLen,bytesRec,rem;
	struct sockaddr_in dest;
	int sockfd = socket(AF_INET,SOCK_DGRAM,0);
	if(sockfd<0)
		error("Error Opening Socket");
	dest.sin_family = AF_INET;
	dest.sin_port = htons(port);
	dest.sin_addr.s_addr = inet_addr(destIP);
	memset(&(dest.sin_zero),'\0',8);
	int index;
	while(1)
	{
		bzero(input,64);
		write(1,"myTftp>> ",strlen("myTftp>> "));
		read(0,input,64);
		index = strcspn(input," ");
		if(strncmp(input,"exit",4)==0)
		{
			break;
		}
		else if(strncmp(input,"get",3)==0)
		{
			snprintf(filename,32,"%s",&input[index+1]);
			filename[strlen(filename)-1] = '\0';
			int ret = recvfile(filename,sockfd,dest);
			if(ret>=0)
				write(1,"File received\n",strlen("File received\n"));
			else
				write(1,"File not received\n",strlen("File not received\n"));
		}
		else if(strncmp(input,"put",3)==0)
		{
			snprintf(filename,32,"%s",&input[index+1]);
			filename[strlen(filename)-1] = '\0';
			int ret = sendfile(filename,sockfd,dest);
			if(ret>=0)
				write(1,"File sent\n",strlen("File sent\n"));
			else
				write(1,"File not sent\n",strlen("File not sent\n"));
		}
		else
		{
			write(1,"Invalid Command\n",strlen("Invalid Command\n"));
		}
	}
	close(sockfd);
	return 0;
}