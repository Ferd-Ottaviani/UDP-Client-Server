//Client.c
//Made by FERDINANDO OTTAVIANI & SAMUELE TAGLIENTI.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#define SIZE 1024
#define BASE 2

int PORT_NUMBER = 10000;
pid_t *pid;
int incr = 0;
int end, choice, fd, offset, div_packets, range, len, ACK, tim, sem_w_id, sem_l_id;
int *is_in, *ack_is_in;
char fileName[2000],fileNameDownload[2000];
char fileList[SIZE], clientList[SIZE];
FILE *fp;
size_t file_size;
struct packet* head = NULL;
struct sockaddr_in	 servaddr,cliaddr;
float timeout_interval, devRTT, estimated_RTT;
float beta = 0.25;
float sample_RTT = BASE;
float alpha = 0.125;
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
	struct packet* walk = head;
	while(walk->next != NULL){
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
	kill(server_pid,2);
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
		sendto(fd, resend,sizeof(struct packet),0,(struct sockaddr *)&servaddr,sizeof(struct sockaddr));
		alarm(timeout_interval);
	}
	//printf("Exit from funct_gest_timeout\n"); //Use this only for debug
}

void send_pkt_thread(struct arg* thread_arg){
	while(thread_arg->who > window.end){
		sleep(2);
	}
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
	//printf("-----Start_Window = %d     End_Window = %d-----\n",window.start,window.end);		//Use this only for debug
	if(random<range){
    	error = 1;
		printf("PACKET N: %d NOT SENT\n",new_entry->num_pkt);  
		goto retry;                  
	}
	if(random>= range && new_entry->num_pkt <= div_packets){
		//Sending data && implementing alarm
		fseek(fp,SIZE*(new_entry->num_pkt-1),SEEK_SET);
		printf("READ %ld BYTES\n",fread(new_entry->payload,1,SIZE,fp));
		sendto(fd,new_entry,sizeof(struct packet), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr)); 		//qui devo inviare la struct
		alarm(timeout_interval);
		printf("PACKET SENT N°:%d\n",new_entry->num_pkt);
		signal(SIGALRM,funct_gest_timeout);				
	}

	//Implementing ack rcv
	if (error == 0){	
                
		/*LABEL*/		wait_ack:

		//printf("START WAIT_ACK THREAD %d:\n", new_entry->num_pkt); //Use this only for debug
		//Separate my ack to other acks
		if (recvfrom(fd,&ACK,sizeof(ACK),0,(struct sockaddr *)&servaddr, &len)<0 || end == 1){					
			if(end == 1 || ack_is_in[new_entry->num_pkt] == 1){
				pthread_exit(0);
			}else{
        	   	printf("Ack number: %d not Received.\n",ACK);
			}
		}
		else{
			if(ACK == new_entry->num_pkt){
				printf(".....PACKET %d      ACK = %d .....\n",new_entry->num_pkt,ACK);
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
					printf("ACK %d UPDATED\n",ACK);
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


int main(int argc, char *argv[]) {
	signal(SIGINT,funct_exit);
	signal(SIGQUIT,funct_exit);
	if(argc != 2){
		printf("ERROR not exactly 2 args [./program <IP_ADDRESS>]\n");
		fflush(stdout);
		exit(-1);
	}
	window.start = 1;
	// Creating socket file descriptor
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if ( fd < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	memset(&servaddr, 0, sizeof(servaddr));
	len=sizeof(servaddr);
	bzero(&servaddr,sizeof(servaddr));
	// Filling server information to do request command 
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT_NUMBER);
	servaddr.sin_addr.s_addr=inet_addr(argv[1]); 
	char req_con = 'y';
	sendto(fd,&req_con,sizeof(req_con),0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
	//Recv new port number
	int old_port = PORT_NUMBER;
	recvfrom(fd,&PORT_NUMBER,sizeof(PORT_NUMBER),0,(struct sockaddr*)&servaddr,&len);
	printf("Closing port number %d\n", old_port);
	close(fd);
	//Open new socket
	printf("Opening new socket with port number %d\n", PORT_NUMBER);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if ( fd < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	memset(&servaddr, 0, sizeof(servaddr));
	len=sizeof(servaddr);
	bzero(&servaddr,sizeof(servaddr));
	// Filling server information to use
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT_NUMBER);
	servaddr.sin_addr.s_addr=inet_addr(argv[1]);
	choice = 0;
	int N = 0;
	range = 0;
	int random;
	sleep(1);
	client_pid = getpid();
	printf("Sending client pid %d.\n",client_pid);
	sendto(fd, &client_pid, sizeof(client_pid), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
	printf("Receiving server child pid ");
	recvfrom(fd,&server_pid,sizeof(server_pid),0,(struct sockaddr *)&servaddr, &len);
	printf("%d\n",server_pid);
	/*LABEL*/ resend_window:
	
	printf("Select an integer for Window transmitting\n");
	scanf("%d",&window.end);
	if(window.end < 1){
		goto resend_window;
	}
	sendto(fd, &window.end, sizeof(window.end), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));

	/*LABEL*/ resend_range:

	printf("Select an integer from 0 to 100 for the probability of loss\n");
	scanf("%d",&range);
	if(range < 0){
		goto resend_range;
	}
	if (range<0 || range > 100)
	{
		printf("Select an integer from 0 to 100 for the probability of loss\n");
		scanf("%d",&range);
	}
	sendto(fd, &range, sizeof(range), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));

	/*LABEL*/ resend_tim:

	//Choosing timer
	printf("Choose an integer for timer:\n\t[0] for static timer\n\t[1] for adaptive timer\n\n");
	scanf("%d",&tim);
	if(tim != 0 && tim != 1){
		goto resend_tim;
	}

	if(tim == 0){
		
		/*LABEL*/ resend_interval:
		
		printf("You select [0] static timer. Please insert timeout interval:\n");
		scanf("%f",&timeout_interval);
		if(timeout_interval < 1){
			goto resend_interval;
		}
		sendto(fd, &tim, sizeof(tim), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
		sendto(fd, &timeout_interval, sizeof(timeout_interval), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
	}
	else{
		sendto(fd, &tim, sizeof(tim), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
	}
	

	/*LABEL*/		new_command:

	while(choice!=4)
	{

		/*LABEL*/	resend_choice:

        printf("\n\t***|| MENU ||***\n");
		printf("\t[1] List\n\t[2] Download\n\t[3] Upload\n\t[4] Exit\n");
		scanf("%d",&choice);
		if(choice < 1 || choice > 4){
			goto resend_choice;
		}
		int num=choice;
		sendto(fd, &num, sizeof(num), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
		switch(choice)
		{

//*********************************************************************************   LIST       **********************************************************************************//
		case 1:
			memset(fileList, 0, sizeof(fileList));
			printf("Contents of the list: \n");
			recvfrom(fd,fileList,SIZE,0,(struct sockaddr *)&servaddr, &len);
			printf("%s\n",fileList);
			goto new_command;

//*********************************************************************************   DOWNLOAD   **********************************************************************************//

		case 2:
			int ACK = 0;
			int end = 0;
			int num_packet;
			size_t file_size;
			memset(fileList, 0, sizeof(fileList));
			printf("Contents of the list: \n");
			recvfrom(fd,fileList,SIZE,0,(struct sockaddr *)&servaddr, &len);
			printf("%s\n",fileList);
			memset(fileNameDownload, 0, sizeof(fileNameDownload));
			printf("Enter file name to download : \n");
			scanf("%s",fileNameDownload);
			sendto(fd, fileNameDownload, strlen(fileNameDownload), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
			printf("NAME OF TEXT FILE RECEIVED : %s\n",fileNameDownload);
			FILE *fpd;
			fpd = fopen(fileNameDownload,"wb+");
			recvfrom(fd,&file_size,sizeof(file_size),0,(struct sockaddr *)&servaddr, &len);
			recvfrom(fd,&div_packets,sizeof(div_packets),0,(struct sockaddr *)&servaddr, &len);
			is_in = (int*)calloc(div_packets+1,sizeof(int)*(div_packets + 1));
			memset(is_in,0,sizeof(is_in));
			is_in[0]=1;
			printf("Size of the file: %ld\n",file_size);
			printf("Number of packet: %d\n",div_packets);
			fseek(fpd,0,SEEK_SET);
			if(!fpd){
				printf("Cannot create to output file.\n");
				exit(-1);
			}

			//Let's start recv data and then sending acks
			while(end==0){
				printf("_________________________________________________________\n");
				struct packet new;
				recvfrom(fd,&new,sizeof(struct packet),0,(struct sockaddr *)&servaddr, &len);
				printf("  Packet %d\n",new.num_pkt);
				if(is_in[new.num_pkt] == 0)
				{
					printf("IS_IN[%d]== 0\n",new.num_pkt);

					/*LABEL*/		resend:

					fseek(fpd,(SIZE*(new.num_pkt-1)),SEEK_SET);
					if (new.num_pkt==div_packets){	
						printf("Packet received\n");
						printf("WRITE %ld BYTES\n",fwrite(new.payload,1,file_size-(SIZE*(new.num_pkt-1)),fpd));
					}else{	
						printf("WRITE %ld BYTES\n",fwrite(new.payload, 1, SIZE, fpd));
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
					if (sendto(fd, &ACK,sizeof(ACK), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr))>0)
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
					}else{
						check++;
					}
				}
				if(check == div_packets){
					end=1;
					printf("END = %d\n",end);
				}
				//printf("END = %d\n",end);		//Use this only for debug
			}  //Closing while(end==0)
			free(is_in);
			is_in = NULL;
			memset(fileNameDownload, 0, sizeof(fileNameDownload));
			fclose(fpd);
			window.end = window.end - window.start;
			window.start = 1;
			goto new_command;


//*********************************************************************************   UPLOAD   **********************************************************************************//

		case 3:

			// Init memory
			memset(clientList, 0, sizeof(clientList));
			printf("AVAIABLE FILES ON CLIENT: \n");
			DIR *d;
			struct dirent *dir;
			d = opendir(".");
			if (!d){
				printf("ERROR IN opendir().\n");
				exit(-1);
			}
			while ((dir = readdir(d)) != NULL){
				const size_t len = strlen(dir->d_name);
				if (len>2 ){
					strcat(clientList, " - ");
					strcat(clientList, dir->d_name);
					strcat(clientList, "\n");
				}
			}
			closedir(d);

			/*LABEL*/	insert_file:
			
			printf("%s\nEnter file name to Upload: \n",clientList);
    		scanf("%s",fileName);
			//check if not present
			fp=fopen(fileName,"rb+");
			if(fp){
    			sendto(fd, fileName, strlen(fileName), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr));
    	 		printf("Reading file contents.\n");
     			fseek(fp,0,SEEK_END);
       			file_size=ftell(fp);
				sendto(fd, &file_size, sizeof(file_size), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr)); //send of size
				printf("Size of the file : %ld\n",file_size);
				fseek(fp,0,SEEK_SET);
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
				sendto(fd, &div_packets, sizeof(div_packets), 0,(struct sockaddr *)&servaddr, sizeof(struct sockaddr)); 
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
					//printf("pthread_creat PACKET NUMBER %d\n",thread_arg->who); //Use this only for debug
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
					/* //Use this only for debug 
					else{
						printf("ack_is_in[%d] == 0\n",i); //Print only unacked
					}
					*/
				}
				if(counter == div_packets){
					end = 1;
				}
				counter=0;
				sleep(3);
			}
			fclose(fp);
			// Free data acks structure
			free(ack_is_in);
			ack_is_in = NULL;
			// Free semaphores
			if(semctl(sem_l_id, IPC_RMID, 0) == -1){
				printf("ERROR IN semctl() close 2.\n");
				fflush(stdout);
				exit(-1);
			}	
			window.end = window.end - window.start;
			window.start = 1;
			//break;
			goto new_command;

		case 4:
			
			close(fd);
			goto new_command;
		} //Closing Switch Case
	} //Closing while
} //Closing main
