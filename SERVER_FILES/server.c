//Server.c

#include<sys/socket.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<string.h>
#include<stdlib.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#define SIZE 1024
#define BASE 2

int PORT_NUMBER = 10000; 
int incr = 0;
int choice, end,sd,div_packets,range,len,ACK,tim,sem_a_id,sem_l_id;
int * ack_is_in, *is_in;;
float timeout_interval, devRTT, estimated_RTT;
float beta = 0.25;
float sample_RTT = BASE;
float alpha = 0.125;
FILE * fpd;
size_t file_size;
struct packet* head = NULL;
struct sockaddr_in servaddr,cliaddr;
pid_t client_pid;
pid_t server_pid;



struct window{
	int start;
	int end;
}window;

struct packet{
	int num_pkt;
	int ack_rec;
	char payload[SIZE];
	struct packet* next;
};

struct packet *check_list(struct packet* head, int num)
{
	//Updating rcved ack
	struct packet* walk = head;
	while(walk != NULL){
		if(walk->num_pkt == num){
			walk->ack_rec = 1;
		}
		walk = walk->next;
	}
	//Check for update window
	walk = head;
	incr = 0;
	while(walk != NULL) {
		if(walk->ack_rec == 1){
			incr++;
			head = walk->next;
			walk = walk->next;
		}else{
			return head;
		}
	}
	head = NULL;
	return head;
}

struct arg{
	int who;
};


int init_list(struct packet* head,struct packet* new)
{
	struct packet* (walk) = head;
	while(walk->next != NULL)
	{
		walk = walk->next;
	}
	walk->next = new;
	return 1;
}

struct packet* check_resend(struct packet* head){
	struct packet *walk = head;
	while(walk!=NULL){
		if(walk->ack_rec == 0){
			return walk;
		}
	}
	printf("\nSomething wrong. Alarm detected but no one is == 0 \n");
	fflush(stdout);
	return NULL;
}

void funct_exit(){
	printf("\n***********************************************\n");
	printf("\n*SIGINT | SIGQUIT received. Starting close all*\n");
	printf("\n***********************************************\n");
	kill(client_pid,2);
	// Free data acks structure
	free(ack_is_in);
	ack_is_in = NULL;
	free(is_in);
	is_in = NULL;
	exit(-1);
}

void funct_gest_timeout(){
	if(tim == 1){
		estimated_RTT = (1 - alpha)*estimated_RTT + alpha*sample_RTT;
		devRTT = (1-beta)*devRTT + beta*abs(sample_RTT - estimated_RTT);
		timeout_interval = estimated_RTT + 4*devRTT;
		printf("TIMER SET TO: %f\n",timeout_interval);
	}
	struct packet *resend = check_resend(head);
	if(resend==NULL){
		printf("RESEND IS NULL\n");
		alarm(0);
	}else{
		//Resending pkt
		printf("SIGALRM DETECTED RESEND PKT NUMBER: %d\n", resend->num_pkt);
		sendto(sd,resend,sizeof(struct packet),0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr));
		alarm(timeout_interval);
	}
	//printf("Exit from funct_gest_timeout\n"); //Use this only for debug
}

void send_pkt_thread(struct arg* thread_arg){
	//Taking token list semaphore
	struct sembuf oper;
	oper.sem_num = 0;
	oper.sem_op = -1;
	if(semop(sem_l_id, &oper, 1) == -1){
		printf("ERROR IN semop().\n");
		fflush(stdout);
		exit(-1);
	}

	if(tim == 1){
		estimated_RTT = (1 - alpha)*estimated_RTT + alpha*sample_RTT;
		devRTT = (1-beta)*devRTT + beta*abs(sample_RTT - estimated_RTT);
		timeout_interval = estimated_RTT + 4*devRTT;
		//printf("TIMER SET TO: %f\n",timeout_interval);		//Use this only for debug
	}
	//Creating pkt struct
	int error, random;
	struct packet* new_entry = (struct packet *)malloc(sizeof(struct packet));
	new_entry->ack_rec = 0;
	new_entry->next = NULL;
	new_entry->num_pkt = thread_arg->who; //num_pkt_to_send;

	//printf("Create new entry PKT: %d\n", new_entry->num_pkt);		//Use this only for debug
	
	if(head == NULL){
		head = new_entry;
		printf("New entry for pkt %d insert\n", new_entry->num_pkt);
	}else {
		if(init_list(head,new_entry) == 1){
			printf("New entry for pkt %d insert\n", new_entry->num_pkt);
		}
	}

	/*LABEL*/	retry:

	error = 0;
	random = rand() % 100 + 1;
    //printf("random: %d\n",random); //Variable for loss %		//Use this only for debug
	//memset(new_entry->payload, 0, SIZE);
	//printf("-----Start_Window = %d     End_Window = %d-----\n",window.start,window.end);		//Use this only for debug
	if(random<range){
    	error = 1;
		printf("PACKET N: %d NOT SENT\n",new_entry->num_pkt);  
		goto retry;                  
	}
	else if(random>= range && new_entry->num_pkt<=div_packets){
		//Sending data && implementing alarm
		fseek(fpd,SIZE*(new_entry->num_pkt-1),SEEK_SET);
		printf("READ %ld BYTES\n",fread(new_entry->payload,1,SIZE,fpd));
		sendto(sd,new_entry,sizeof(struct packet), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr));
		alarm(timeout_interval);
		printf("PACKET SENT N°:%d\n",new_entry->num_pkt);
		signal(SIGALRM,funct_gest_timeout);				
	}

	//Implementing ack rcv
	if (error == 0){	
                
		/*LABEL*/wait_ack:

		//printf("START WAIT_ACK THREAD %d:\n", new_entry->num_pkt); //Use this only for debug
		//Separate my ack to other acks
		if (recvfrom(sd,&ACK,sizeof(ACK),0,(struct sockaddr *)&cliaddr, &len)<0 || end == 1){
			if(end == 1 || ack_is_in[new_entry->num_pkt] == 1){
				pthread_exit(0);
			}else{
        	   	printf("Ack number: %d not Received.\n",ACK);
			}
		}
		else{
			if(ACK == new_entry->num_pkt){
				printf(".....PACCHETTO %d      ACK = %d .....\n",new_entry->num_pkt,ACK);
				alarm(0);
				//Updating ack's structure using by main process
				ack_is_in[ACK] = 1;
				head = check_list(head,new_entry->num_pkt);
				if ((window.end+incr)>div_packets){
					window.end = div_packets;
				}else{
					window.start = window.start+incr;
					window.end = window.end+incr;
				}
				if((window.end == div_packets)){
					incr = 0;
				}
				incr = 0;
			}else{
				if (ack_is_in[ACK] == 0) {
					ack_is_in[ACK]=1;
					//printf("***THREAD %d UPDATE ACK %d FOR OTHERS***\n", new_entry->num_pkt,ACK); //Use this only for debug
				}
				head = check_list(head, ACK);
				goto wait_ack;
			}	
        }
			printf("---------Start_Window = %d     End_Window = %d---------\n",window.start,window.end);
    }
	printf("Child Thread for packet %d EXIT Normally.\n", new_entry->num_pkt);
	printf("__________________________________________\n");	
	// Updating token list semaphore
	oper.sem_op = 1;
	if(semop(sem_l_id, &oper, 1) == -1){
		printf("ERROR IN semop().\n");
		fflush(stdout);
		exit(-1);
	}
	free(new_entry);
	new_entry = NULL;
}


int main()
{
	signal(SIGINT,funct_exit);
	signal(SIGQUIT,funct_exit);
	window.start = 1;
	int connfd,random;
	char fileName[100], fileNameDownload[100],filesList[SIZE];
	for(int i=0;i<=100;i++)
	{
		fileName[i]='\0';
	}
	sd = socket(AF_INET, SOCK_DGRAM, 0);
  	if(sd==-1)
    {
      	printf(" socket not created in server\n");
		exit(0);
    }
    printf("socket created in  server\n");
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(PORT_NUMBER);
	memset(&(servaddr.sin_zero),0,8);
	len=sizeof(cliaddr);
	int already = 0;
	restart:
	if ( bind(sd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0 && already != 1)
    	printf("Not binded\n");
  	else{
		if(already == 0){
			printf("Binded port: %d\n", PORT_NUMBER);
		}
		already = 1;
		char req_con;
		recvfrom(sd,&req_con,sizeof(req_con),0,(struct sockaddr *)&cliaddr, &len);
		PORT_NUMBER++;
		sendto(sd,&PORT_NUMBER,sizeof(PORT_NUMBER),0,(struct sockaddr*)&cliaddr,sizeof(struct sockaddr));
	}

	//CREATING CHILD-SERVER TO SERVE THE REQUEST

	if(fork()==0){
		sd = socket(AF_INET, SOCK_DGRAM, 0);
  		if(sd==-1)
    	{
      		printf(" socket not created in server\n");
			exit(-1);
    	}
    	printf("socket created in  server\n");
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = INADDR_ANY;
		servaddr.sin_port = htons(PORT_NUMBER);
		memset(&(servaddr.sin_zero),0,8);
		len=sizeof(cliaddr);
		if ( bind(sd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0 ){
    		printf("Not binded\n");
			exit(-1);
  		}
		printf("Binded port: %d\n", PORT_NUMBER);
		
		server_pid = getpid();
		printf("Receiving client pid ");
		recvfrom(sd,&client_pid,sizeof(client_pid),0,(struct sockaddr *)&cliaddr, &len);
		printf("%d\n",client_pid);
		sleep(1);
		printf("Sending server child pid %d.\n",server_pid);
		sendto(sd, &server_pid,sizeof(server_pid), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr));
		
		printf("Receiving window size.\n");
		recvfrom(sd,&window.end,sizeof(window.end),0,(struct sockaddr *)&cliaddr, &len);
		int num;
		recvfrom(sd,&range,sizeof(range),0,(struct sockaddr *)&cliaddr, &len);
		recvfrom(sd,&tim,sizeof(tim),0,(struct sockaddr *)&cliaddr, &len);

		if (tim != 1){
			float timeout;
			recvfrom(sd,&timeout,sizeof(timeout),0,(struct sockaddr *)&cliaddr, &len);
			timeout_interval = timeout;
			printf("Static timeout set to: %f\n", timeout_interval);
		}
		choice = 1;
		while(1)
		{

			/*LABEL*/	new_command:

			recvfrom(sd,&num,sizeof(num),0,(struct sockaddr *)&cliaddr, &len);
			choice = num;
			switch(choice)
			{

//*********************************************************************************   LIST   **********************************************************************************//
			case 1:
				memset(filesList, 0, sizeof(filesList));
				printf("FILES ON SERVER: \n");
				int n=0;
				int i=0;
				DIR *d;
				struct dirent *dir;
				d = opendir(".");
				if (!d){
					printf("ERROR in opendir.\n");
					exit(-1);
				}
				while ((dir = readdir(d)) != NULL){
					const size_t len = strlen(dir->d_name);
					if (len>2){
						strcat(filesList, " - ");
						strcat(filesList, dir->d_name);
						strcat(filesList, "\n");
					}
				}
				closedir(d);
				sendto(sd, filesList,sizeof(filesList), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr));
				printf("%s\n",filesList);
				goto new_command;

//*********************************************************************************   DOWNLOAD   **********************************************************************************//
			case 2:
				//LIST DA MANDARE AL CLIENT
				memset(filesList, 0, sizeof(filesList));
				printf("FILES ON SERVER: \n");
				d = opendir(".");
				if (d)
				{
					while ((dir = readdir(d)) != NULL)
					{
						const size_t len = strlen(dir->d_name);
						if (len>2)
						{
						strcat(filesList, " - ");
						strcat(filesList, dir->d_name);
						strcat(filesList, "\n");
						}
					}
					closedir(d);
				}
				
				/*LABEL*/   insert_file:
				
				printf("%s\n",filesList);
				sendto(sd, filesList,SIZE, 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr));
				memset(fileNameDownload, 0, sizeof(fileNameDownload));
				recvfrom(sd,fileNameDownload,SIZE,0,(struct sockaddr *)&cliaddr, &len);
				printf("NAME OF TEXT FILE TO SEND : %s\n",fileNameDownload);
				fpd=fopen(fileNameDownload,"rb+");
				if(fpd){
    	 			printf("Reading file contents.\n");
     				fseek(fpd,0,SEEK_END);
       				file_size=ftell(fpd);
					sendto(sd, &file_size, sizeof(file_size), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr)); //send of size
					printf("Size of the file : %ld\n",file_size);
					fseek(fpd,0,SEEK_SET);
					div_packets = 0; // number of pkts created cutting file_size by SIZE
					if (file_size%SIZE==0){
						div_packets = (file_size/SIZE);
					}
					else{
						div_packets = ((file_size/SIZE)+1);
					}
					//Creating control variable to check if every pkts were acked
					ack_is_in = (int*)calloc(div_packets+1,sizeof(int)*(div_packets + 1));
					ack_is_in[0]=1;
					sendto(sd, &div_packets, sizeof(div_packets), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr)); 
					printf("Packets to send: %d\n________________________________________\n\n",div_packets);
					ACK = 0;
					int error;
					//Creating semaphore
					if((sem_l_id = semget(getpid(),1,IPC_CREAT|0666)) == -1){
						printf("ERROR IN CREATE SEMAPHORE -l.\n");
						fflush(stdout);
						exit(-1);
					}
					if(semctl(sem_l_id, 0, SETVAL, 1) == -1){
						printf("ERROR IN semctl -l().\n");
						fflush(stdout);
						exit(-1);
					}
					
					for(int count = 1 ;count<=div_packets;count++){
						struct arg* thread_arg = (struct arg*)malloc(sizeof(struct arg));
						thread_arg->who = count;
					
						/*LABEL*/			retry:	
					
						//Calling THREADS (they send pkt, rcv ack, update ack_is_in[])
						pthread_t tid;
						if(pthread_create(&tid,NULL,(void *)send_pkt_thread,thread_arg)!=0){
							printf("ERROR in pthread_creat 'send_pkt_thread' PACKET NUMBER %d\n",count);
							fflush(stdout);
							goto retry;
						}
						//printf("pthread_creat PACKET NUMBER %d\n",thread_arg->who); //use this only for debug
					}
				}
				else{
    				printf("Cannot open file.\n");
    	 			goto insert_file;
    			}
    		
    			//Waiting all threads
				end = 0;
				int counter = 0;
				while(!end){
					for(int i = 1; i<=div_packets;i++){
						if(ack_is_in[i] == 1){
							counter++;
						}
						/* Use this only for debug 
						else{
							printf("ack_is_in[%d] == 0\n",i); //Print only unacked
						}*/	
					}
					if(counter == div_packets){
						end = 1;
					}
					counter=0;
					sleep(3);
				}
				fclose(fpd);
				// Free data acks structure
				free(ack_is_in);
				ack_is_in = NULL;
				// Free semaphores
				if(semctl(sem_l_id, IPC_RMID, 0) == -1){
					printf("ERROR IN semctl() close 2.\n");
					fflush(stdout);
					exit(-1);
				}	
				goto new_command;

//*********************************************************************************   UPLOAD   **********************************************************************************//
			case 3:
				//Init structure and file 
				int ACK = 0;
				size_t file_size;
				int div_packets, num_packet;
				int end = 0;
				recvfrom(sd,fileName,SIZE,0,(struct sockaddr *)&cliaddr, &len);
				printf("NAME OF TEXT FILE RECEIVED : %s\n",fileName);
				FILE *fp;
				fp=fopen(fileName,"wb+");
				recvfrom(sd,&file_size,sizeof(file_size),0,(struct sockaddr *)&cliaddr, &len);
				recvfrom(sd,&div_packets,sizeof(div_packets),0,(struct sockaddr *)&cliaddr, &len);
				//Allocate data structure to check acks
				is_in = (int*)calloc(div_packets+1,sizeof(int)*(div_packets + 1));
				memset(is_in,0,sizeof(is_in));
				is_in[0]=1;
				printf("Size of the file: %ld\n",file_size);
				printf("Number of packet: %d\n",div_packets);
				fseek(fp,0,SEEK_SET);
				if(!fp){
					printf("Cannot create to output file.\n");
					exit(-1);
				}

				//Let's start recv data and then sending acks
				while(end==0)
				{
					printf("_________________________________________________________\n");
					struct packet new;
					recvfrom(sd,&new,sizeof(struct packet),0,(struct sockaddr *)&cliaddr, &len); //qui devo ricevere la struct
					printf("  Packet %d\n",new.num_pkt);
					if(is_in[new.num_pkt] == 0)
					{
						//printf("IS_IN[%d]== 0\n",num_packet); //Use this only for debug

						/*LABEL*/			resend:

						fseek(fp,(SIZE*(new.num_pkt-1)),SEEK_SET);
						if (new.num_pkt==div_packets){	
							printf("WRITE %ld BYTES\n",fwrite(new.payload,1,file_size-(SIZE*(new.num_pkt-1)),fp));
							printf("Packet received\n");
						}
						else{	
							printf("WRITE %ld BYTES\n",fwrite(new.payload, 1, SIZE, fp));
							printf("Packet received\n");                                       
						}
						is_in[new.num_pkt]=1;
						ACK=new.num_pkt;
					}
					else{
						printf("IS_IN[%d] == 1\n", new.num_pkt);
						ACK = new.num_pkt;
						is_in[new.num_pkt]=1;
					}
					//Calculate the loss %
					random = rand() % 100 + 1;
					//printf("rand:%d\n",random);		//Use this only for debug
					if (random>range)
					{
						//printf("Sending ACK from Server\n");		//Use this only for debug
						ACK = new.num_pkt;
						if (sendto(sd, &ACK,sizeof(ACK), 0,(struct sockaddr *)&cliaddr, sizeof(struct sockaddr))>0)
						{
							printf("ACK number: %d SENT\n",ACK);
						}
						else
						{
							printf ("ACK number: %d NOT sent\n",ACK);
							is_in[new.num_pkt]=0;
						}
					}
					else
					{
						printf("ACK LOST\n");
						fflush(stdout);
						is_in[new.num_pkt]=0;
					}	
					int  check = 0;
					for(int count=1;count<=div_packets;count++){
						if(is_in[count]==0){
							end = 0;
						}
						else{
							check++;
						}
					}
					if(check == div_packets){
						end=1;
						//printf("END = %d\n",end);  //Use this only for debug
					}
					//printf("END = %d\n",end);		//Use this only for debug
				}
				div_packets = 0;
				file_size=0;
				free(is_in);
				is_in = NULL;
				fclose(fp);
				memset(fileName, 0, sizeof(fileName));
				goto new_command;

			case 4:
				printf("Closing socket with port number %d\n",PORT_NUMBER);
				close(sd);
				printf("Child %d exit with code 0\n",getpid());
				exit(0);
			}
		}//Close While
	} // close child Fork section
	else
	{
		sleep(1);
		goto restart;
	}

  	return(0);
} //Close Main()
