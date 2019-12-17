#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#define BUF_SIZE 512
#define MAX_ARGS 16

struct DataObj {
	char* outputline; // record the command and print it after 
	char* line; //command after erase marks
	char*** cmd; //parised command
	int pipeNum; //num of the pipe if any
	char* errorMessage; //error message
	int background; //if the process is a background command
	int* array; //array of exit status
	int pid; //pid of process
};

//overview of all background command
struct BackGroundCmdList{
	struct DataObj* Obj;//command object
	struct BackGroundCmdList* next;//link list of next backgroundcommand
};
//overall of the function used in the command
int scanForRedirection(struct DataObj* Obj, int index, char sign);
int afterParseError(struct DataObj* Obj);
int parse2ndVer(struct DataObj* Obj);
int Is_buildIn(struct DataObj* Obj);
int execute_buildIn(struct DataObj* Obj, int ret_status);
int execute_pipe_command(struct DataObj* Obj, char* cmdline);
void printBackGround(struct BackGroundCmdList* node, int* backgroundProcessNum);
void printCurrent(struct DataObj* cmdObj, char* cmdline);
int handler();

//return the position of desired sign
int scanForRedirection(struct DataObj* Obj, int index, char sign){
	for(int i = 0; Obj->cmd[index][i]!=NULL; i++ ){
		if(Obj->cmd[index][i][0] == sign){
			return i;
		}
	}
	return 0;
}

//handle all the Error and give them to the Errormessage in Obj
int afterParseError(struct DataObj* Obj){
	FILE* fp;
	int SignIndex;
	//check if there is any input
	if((Obj->pipeNum == 1) && (Obj->cmd[0][0] == NULL)){
		Obj->errorMessage = "noInput";
		return 0;
	}
	//check input error
	for (int i = 0; i < Obj->pipeNum; i++){
		//input error: invalid command line
		if((Obj->cmd[i][0] == NULL ) || (Obj->cmd[i][0][0] == '>') || (Obj->cmd[i][0][0] == '<') || (Obj->cmd[i][0][0] == '&') ){
			Obj->errorMessage = "Error: invalid command line\n";
			return 0;
		}
		//input error: mislocated background sign
		SignIndex = scanForRedirection(Obj, i, '&');
		if(SignIndex){
			if((i != (Obj->pipeNum - 1)) || (Obj->cmd[i][SignIndex + 1] != NULL)){
				Obj->errorMessage = "Error: mislocated background sign\n";
				return 0;
			} else {
				Obj->cmd[i][SignIndex] = NULL;
				Obj->background = 1;
			}
		}
		//if there is no file after > or <
		SignIndex = scanForRedirection(Obj, i, '>');
		if(SignIndex){
			if(i == (Obj->pipeNum - 1)){
				if(Obj->cmd[i][SignIndex + 1] == NULL){
					Obj->errorMessage = "Error: no output file\n";
					return 0;
				} else {
					fp = fopen(Obj->cmd[i][SignIndex + 1], "a");
					if (fp == NULL){
						Obj->errorMessage = "Error: cannot open output file\n";
						return 0;
					} else {
						fclose(fp);
					}
				}
			} else {
				Obj->errorMessage = "Error: mislocated output redirection\n";
				return 0;
			}
		}
		//check if redirction can be opened
		SignIndex = scanForRedirection(Obj, i, '<');
		if(SignIndex){
			if(i == 0){
				if(Obj->cmd[i][SignIndex + 1] == NULL){
					Obj->errorMessage = "Error: no input file\n";
					return 0;
				} else {
					fp = fopen(Obj->cmd[i][SignIndex + 1], "r");
					if (fp == NULL){
						Obj->errorMessage = "Error: cannot open input file\n";
						return 0;
					} else {
						fclose(fp);
					}
				}
			} else {
				Obj->errorMessage = "Error: mislocated input redirection\n";
				return 0;
			}
		}
			
	}

	return 1;
}
//parsing function reads the command from Obj
int parse2ndVer(struct DataObj* Obj){
	char SplitSignString[BUF_SIZE];
	char FinalString[BUF_SIZE];
	int OldStringIndex = 0;
	int NewStringIndex = 0;
	int CMDIndex = 0;
	int position = 0;
	char word[BUF_SIZE];
	char*** newCmd = (char***)malloc(MAX_ARGS*sizeof(char**));
	
	for(int i = 0; i < MAX_ARGS; i++){
		newCmd[i] = (char**)malloc(MAX_ARGS*sizeof(char*));
		for (int j = 0; j < MAX_ARGS; j++){
			newCmd[i][j] = (char*)malloc(BUF_SIZE*sizeof(char));
		}
	}

	//make a copy of outputline and use the copy later
	strcpy(SplitSignString, Obj->outputline);
	//erase the >,<,&,| from string
	while(SplitSignString[OldStringIndex] != '\n'){
		if((SplitSignString[OldStringIndex] != '>') && (SplitSignString[OldStringIndex] != '<') && (SplitSignString[OldStringIndex] != '&') && (SplitSignString[OldStringIndex] != '|')){
			FinalString[NewStringIndex++] = SplitSignString[OldStringIndex++];
		} else {
			FinalString[NewStringIndex++] = ' ';
			FinalString[NewStringIndex++] = SplitSignString[OldStringIndex++];
			FinalString[NewStringIndex++] = ' ';
		}
	}
	FinalString[NewStringIndex] = '\n';

	OldStringIndex = 0;
	NewStringIndex = 0;
	//store the command between  | sign
	while(FinalString[OldStringIndex] != '\n'){
		NewStringIndex = 0;
		if(FinalString[OldStringIndex] == '|'){
			newCmd[CMDIndex][position] = NULL;
			position = 0;
			CMDIndex++;
			OldStringIndex++;
			continue;
		}
		while(FinalString[OldStringIndex+NewStringIndex] != '\n' && FinalString[OldStringIndex+NewStringIndex] != ' '){
			word[NewStringIndex] = FinalString[OldStringIndex+NewStringIndex];
			NewStringIndex++;
		}
		if(NewStringIndex != 0){
			OldStringIndex += NewStringIndex;
			word[NewStringIndex] = 0;
			strcpy(newCmd[CMDIndex][position++], word);
		} else {
		OldStringIndex++;
		}
	}
	newCmd[CMDIndex++][position] = NULL;
	newCmd[CMDIndex] = NULL;
	Obj->pipeNum = CMDIndex;
	//store final result into Obj
	for(OldStringIndex = 0; newCmd[OldStringIndex] != NULL; OldStringIndex++){
		for(NewStringIndex = 0; newCmd[OldStringIndex][NewStringIndex] != NULL; NewStringIndex++){
			strcpy(Obj->cmd[OldStringIndex][NewStringIndex],newCmd[OldStringIndex][NewStringIndex]);
		}
		Obj->cmd[OldStringIndex][NewStringIndex] = NULL;
	}
	Obj->cmd[OldStringIndex] = NULL;
	//check if there is any error in Obj
	if(!afterParseError(Obj)){
		return 0;
	}
	return 1;
}
//check if Obj command is a build in command
int Is_buildIn(struct DataObj* Obj){
	if (!strcmp(Obj->cmd[0][0], "cd")){
        return 1;
    } else if (!strcmp(Obj->cmd[0][0], "pwd")){
        return 1;
	} else {
		return 0;
	}
}
//execute build in command
int execute_buildIn(struct DataObj* Obj, int ret_status){
	char buffer[BUF_SIZE];
	if (!strcmp(Obj->cmd[0][0], "cd")){
		getcwd(buffer, BUF_SIZE);
		if(Obj->cmd[0][1] != NULL){
			char* result = strcat(buffer, "/");
			char* result2 = strcat(result, Obj->cmd[0][1]);
			if(chdir(result2) == -1){
				fprintf(stderr, "Error: no such directory\n");
				ret_status += 1;
			}
        } else {
				fprintf(stderr, "Error: no such directory\n");
				ret_status += 1;
        }
        getcwd(buffer, BUF_SIZE);
	} else if (!strcmp(Obj->cmd[0][0], "pwd")){
        getcwd(buffer, BUF_SIZE);
        fprintf(stdout, "%s\n", buffer);
    }
	return ret_status;
}
//check if the cmdline needs any pipeline and store the result into Obj
//this is where command is execute
int execute_pipe_command(struct DataObj* Obj, char* cmdline){
	int ret_status = 0;
	int fd_in = 0;
	int index = 0;
	int status, inputIndex, outputIndex, fd_out;
	int p[2];
	pid_t pid;
	//check until the end of commandline
	while(Obj->cmd[index] != NULL){
		pipe(p);
	    	pid = fork();
	    	if(pid == 0){
				//child process
				inputIndex = scanForRedirection(Obj, index, '<');
				outputIndex = scanForRedirection(Obj, index, '>');
				if(inputIndex){
						Obj->cmd[0][inputIndex] = NULL;
						fd_in = open(Obj->cmd[0][inputIndex + 1],O_RDONLY);
				}
				if(outputIndex){
						Obj->cmd[index][outputIndex] = NULL;
						fd_out = open(Obj->cmd[index][outputIndex + 1],O_RDWR);
						dup2(fd_out, STDOUT_FILENO);
				}
				dup2(fd_in, 0);
				if(index != Obj->pipeNum - 1){
					dup2(p[1], 1);
				}
				close(p[0]);
				if (Is_buildIn(Obj)){
				exit(0);
				} else { 
					if(execvp(Obj->cmd[index][0], Obj->cmd[index]) < 0){
						fprintf(stderr, "Error: command not found\n");
					}
					exit(1);
				}
	    	} else if(pid > 0) {
				//parent process
				waitpid(-1, &status, 0);
				close(p[1]);
				fd_in = p[0];
				ret_status = WEXITSTATUS(status);
				if(Is_buildIn(Obj)){
					ret_status = execute_buildIn(Obj, ret_status);
				}
	    	} else {
				perror("fork");
				exit(1);
	    	}
		Obj->array[index] = ret_status;
		index++;
	}
    return 0;
}

//wait to catch if any exit possible and record them if any
void printBackGround(struct BackGroundCmdList* node, int* backgroundProcessNum){
	int state = 0;
	int ret_state = 0;
	while(node->next != NULL){
		if(waitpid(node->next->Obj->pid, 0, WNOHANG)){
			waitpid(node->next->Obj->pid, &state, 0);
			fprintf(stderr, "+ completed '%s' ", node->next->Obj->line);
			for(int i = 0; i < node->next->Obj->pipeNum; i++){
				fprintf(stderr, "[%d]", ret_state);
			}
			fprintf(stderr, "\n");
			*backgroundProcessNum -= 1;
			node->next = node->next->next;
		} else {
			node = node->next;
		}
	}
}
//print the command if need multiple output of exit status in pipeline
void printCurrent(struct DataObj* cmdObj, char* cmdline){
	fprintf(stderr, "+ completed '%s' ", cmdline);
	for(int i = 0; i < cmdObj->pipeNum; i++){
		fprintf(stderr, "[%d]", cmdObj->array[i]);
	}
	fprintf(stderr, "\n");
}

//the main function handle the command
int handler(){

	int backgroundProcessNum = 0;
	struct BackGroundCmdList* head = (struct BackGroundCmdList*)malloc(sizeof(struct BackGroundCmdList));
	head->Obj = NULL;
	head->next = NULL;
	//keep asking the command and handle them
	while(1){
		struct BackGroundCmdList* node = head;
		pid_t pid;
		char* cmdline = (char*)malloc(BUF_SIZE*sizeof(char));
		char outputline[BUF_SIZE];
		char*** cmd = (char***)malloc(MAX_ARGS*sizeof(char**));

		for(int i = 0; i < MAX_ARGS; i++){
			cmd[i] = (char**)malloc(MAX_ARGS*sizeof(char*));
			for (int j = 0; j < MAX_ARGS; j++){
				cmd[i][j] = (char*)malloc(BUF_SIZE*sizeof(char));
			}
		}

		char *nl;

		printf("sshell$ ");
		fflush(stdout);
		//get command
		fgets(cmdline, BUF_SIZE, stdin);

		if (!isatty(STDIN_FILENO)) {
			printf("%s", cmdline);
			fflush(stdout);
		}

		strcpy(outputline, cmdline);
		//erase newline
		nl = strchr(cmdline, '\n');
		if (nl)
			*nl = '\0';
		
		//create a command object
		struct DataObj* cmdObj = malloc(sizeof(struct DataObj));
		cmdObj->outputline = outputline;
		cmdObj->cmd = cmd;
		cmdObj->pipeNum = 0;
		cmdObj->errorMessage = NULL;
		cmdObj->background = 0;
		cmdObj->array = NULL;
		cmdObj->pid = 0;

		//parse the command and print out error message if any
		if(!parse2ndVer(cmdObj)){
			if(strcmp(cmdObj->errorMessage, "noInput")!= 0){
				fprintf(stderr, "%s", cmdObj->errorMessage);
			}
			printBackGround(head, &backgroundProcessNum);
			continue;
		} else {
		
			int array[cmdObj->pipeNum];
			cmdObj->array = array;
			cmdObj->line = cmdline;
			//catch if the command is exit
			if(strcmp(cmdObj->cmd[0][0],"exit") == 0){
				if(!backgroundProcessNum){
					fprintf(stderr, "Bye...\n");
					break;
				} else {
					//error:message if there are background command
					fprintf(stderr, "Error: active jobs still running\n");
					printBackGround(head, &backgroundProcessNum);
					fprintf(stderr, "+ completed 'exit' [1]\n");
					continue;
				}
			}

			//background process check
			if(!cmdObj->background){
				execute_pipe_command(cmdObj, cmdline);
				printBackGround(head, &backgroundProcessNum);
				printCurrent(cmdObj, cmdline);
			} else {
				pid = fork();
				if(pid == 0){
					execute_pipe_command(cmdObj, cmdline);
					exit(0);
				} else {
					printBackGround(head, &backgroundProcessNum);
					cmdObj->pid = pid;
					struct BackGroundCmdList* process = (struct BackGroundCmdList*)malloc(sizeof(struct BackGroundCmdList));
					process->Obj = cmdObj;
					process->next = NULL;
					while(node->next != NULL){
						node = node->next;
					}
					node->next = process;
					backgroundProcessNum++;
				}
			}
		}
	}
	return 0;
}
int main(int argc, char *argv[]){
	handler();
	return EXIT_SUCCESS;
}

