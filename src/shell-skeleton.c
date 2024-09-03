#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include "../tree/tree_plot.c"

#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

#include <math.h>
#include <float.h>

const char *sysname = "Hshell";


enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	struct command_t *next; // for piping
};

struct autocomplete_struct {
	char **matches; // matchings
	int count; // matching count
};

char *builtin_command_list[] = {"hdiff", "regression", "psvis", "textify"};

void search_and_run_command(struct command_t *command, int issudo);
int pipe_function(struct command_t *command);
void hdiff(struct command_t *command);
void regressionAndPlot(struct command_t *command);
void textify(struct command_t *command);
void save_available_commands(const char *file_name);
void combine_paths(char *result, const char *directory, const char *file);
struct autocomplete_struct *command_complete(const char *input_str);
struct autocomplete_struct *directory_complete(const char *input_str);
int check_command_or_filename(char *buf, char *filename_start);

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n",
		   command->auto_complete ? "yes" : "no");

	printf("\tArguments (%d):\n", command->arg_count);

	for (i = 0; i < command->arg_count; ++i) {
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	}

	if (command->next) {
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
	if (command->arg_count) {
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}

	if (command->next) {
		free_command(command->next);
		command->next = NULL;
	}

	free(command->name);
	free(command);
	return 0;
}

int free_autocomplete_struct(struct autocomplete_struct *match) {
	if (match->count){
		for (int i = 0; i < match->count; ++i){
		free(match->matches[i]);
		match->matches[i] = NULL;
        	}
	free(match->matches);
	match->count = 0;
    	}
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);

	// trim left whitespace
	while (len > 0 && strchr(splitters, buf[0]) != NULL) {
		buf++;
		len--;
	}

	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL) {
		// trim right whitespace
		buf[--len] = 0;
	}

	// auto-complete
	if (len > 0 && buf[len - 1] == '?') {
		command->auto_complete = true;
	}

	// background
	if (len > 0 && buf[len - 1] == '&') {
		command->background = true;
	}

	char *pch = strtok(buf, splitters);
	if (pch == NULL) {
		command->name = (char *)calloc(1, 1);
		command->name[0] = 0;
	} else {
		command->name = (char *)calloc(strlen(pch) + 1, 1);
		strcpy(command->name, pch);
	}

	command->args = (char **)calloc(sizeof(char *), 1);

	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1) {
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// trim left whitespace
		while (len > 0 && strchr(splitters, arg[0]) != NULL) {
			arg++;
			len--;
		}

		// trim right whitespace
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL) {
			arg[--len] = 0;
		}

		// empty arg, go for next
		if (len == 0) {
			continue;
		}

		// piping to another command
		if (strcmp(arg, "|") == 0) {
			struct command_t *c = calloc(sizeof(struct command_t), 1);
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0) {
			// handled before
			continue;
		}

		// normal arguments
		
		if (len > 2 &&
			((arg[0] == '"' && arg[len - 1] == '"') ||
			 (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}

		command->args =
			(char **)realloc(command->args, sizeof(char *) * (arg_index + 1));

		command->args[arg_index] = (char *)calloc(len + 1, 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;

	// increase args size by 2
	command->args = (char **)realloc(
		command->args, sizeof(char *) * (command->arg_count += 2));

	// shift everything forward by 1
	for (int i = command->arg_count - 2; i > 0; --i) {
		command->args[i] = command->args[i - 1];
	}

	// set args[0] as a copy of name
	command->args[0] = strdup(command->name);

	// set args[arg_count-1] (last) to NULL
	command->args[command->arg_count - 1] = NULL;

	return 0;
}

void prompt_backspace() {
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}


/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
	size_t index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];
	char *fname;

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &=
		~(ICANON |
		  ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	show_prompt();
	
	for (int i=index; i<4096; i++) {
		buf[i] = '\0';
	}

	while (1) {
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging
		// handle tab
		if (c == 9) {
			if (index == 0) continue;
			char *buf_cpy = strdup(buf);
			fname = (char *)malloc(4096 * sizeof(char));
			struct autocomplete_struct *match;
			int command_or_filename = check_command_or_filename(buf_cpy, fname); //update buffer and check
			if (command_or_filename) { // complete filename case
				match = directory_complete(fname); //find all matching files in the directory
				if (match->count == 1) {
					for (int i = strlen(fname); i < (int) strlen(match->matches[0]); i++) {
						putchar(match->matches[0][i]);
						buf[index++] = match->matches[0][i];
					}
					c = ' ';
				}else if (match->count > 1) {
					printf("\n");
					for (int i = 0; i < match->count; i++) {
						if(strstr(buf,"cd")==NULL) printf("%s ", match->matches[i]);
						else if (match->matches[i][strlen(match->matches[i])-1]=='/') printf("%s ", match->matches[i]);
					}
					printf("\n");
					show_prompt();
					printf("%s", buf);
				}
			}else { // complete command case                           
				char *command = *buf_cpy ? strdup(buf_cpy) : NULL;
				command = fname;
				match = command_complete(command); //find all matching commands
				if (match->count == 1) {
					for (int i = strlen(command); i < (int) strlen(match->matches[0]); i++) {
						putchar(match->matches[0][i]);
						buf[index++] = match->matches[0][i];
					}
		            		c = ' ';
		        	}else if (match->count > 1) {
					printf("\nAvailable commands: \n");
					for (int i = 0; i < match->count; i++) printf(" - %s \n", match->matches[i]);
					printf("\n");
					show_prompt();
					printf("%s", buf);
		        	}
			}
			free_autocomplete_struct(match); //free match struct
			free(fname); //free fname
			if (c == 9) {
				continue;
			}
		}

		// handle backspace
		if (c == 127) {
			if (index > 0) {
				prompt_backspace();
				index--;
				for (int i=index; i<4096; i++) {
					buf[i] = '\0';
				}
			}
			continue;
		}

		if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
			continue;
		}

		// up arrow
		if (c == 65) {
			while (index > 0) {
				prompt_backspace();
				index--;
			}

			char tmpbuf[4096];
			printf("%s", oldbuf);
			strcpy(tmpbuf, buf);
			strcpy(buf, oldbuf);
			strcpy(oldbuf, tmpbuf);
			index += strlen(buf);
			continue;
		}

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}

	// trim newline from the end
	if (index > 0 && buf[index - 1] == '\n') {
		index--;
	}


	// null terminate string
	buf[index++] = '\0';

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main() {
	save_available_commands("all_commands.txt");
	
	while (1) {
		struct command_t *command = calloc(sizeof(struct command_t), 1);

		// set all bytes to 0
		memset(command, 0, sizeof(struct command_t));

		int code;
		code = prompt(command);
		if (code == EXIT) {
			break;
		}

		code = process_command(command);
		if (code == EXIT) {
			break;
		}

		free_command(command);
	}

	printf("\n");
	remove("all_commands.txt");
	return 0;
}

char * trim_space(char *str) {
    char *end;
    /* skip leading whitespace */
    while (isspace(*str)) {
        str = str + 1;
    }
    /* remove trailing whitespace */
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end = end - 1;
    }
    /* write null character */
    *(end+1) = '\0';
    return str;
}

int process_command(struct command_t *command) {
	int r;

	if (strcmp(command->name, "") == 0) {
		return SUCCESS;
	}

	if (strcmp(command->name, "exit") == 0) {
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0) {
		if (command->arg_count > 0) {
			r = chdir(command->args[1]);
			if (r == -1) {
				printf("-%s: %s: %s\n", sysname, command->name,
					   strerror(errno));
			}

			return SUCCESS;
		}
	}
	
	if (strcmp(command->name, "psvis") == 0) {
		//Read from command:
		if(command->arg_count!=4){
			printf("Number of arguments in psvis are not correct\n");
			return SUCCESS;
		}
		 
		long root_process = strtol(command->args[1], NULL, 10);	
		char *filename = calloc(sizeof(char)*strlen(command->args[2]), 1);
		strcpy(filename, command->args[2]);
		pid_t pid_s1 = fork();
		if (pid_s1 == 0){
			char temp1[50], temp2[50];
			strcpy(temp1, "PID=");
			sprintf(temp2, "%d", (int)root_process);
			strcat(temp1, temp2);
			command->name = "sudo";
			command->args = (char *[]) {"/usr/bin/sudo", "insmod", "module/psvis.ko", temp1, NULL};
			// for the command "sudo insmod module/psvis.ko PID:xxx"
			search_and_run_command(command, 1);
		}else{
			waitpid(pid_s1, NULL, 0);
			pid_t pid_s2 = fork();
			if (pid_s2 == 0) {
				command->arg_count = 2;
				command->args = (char **)calloc(command->arg_count * sizeof(char *), 1);
				command->name = "sudo";
				command->args = (char *[]) {"/usr/bin/sudo", "rmmod", "psvis", NULL};
				// remove the module with command "sudo rmmod psvis"
				search_and_run_command(command, 1);
			}else{
				waitpid(pid_s2, NULL, 0); // wait for child process
				char buf[4096];
				remove(filename);
				sprintf(buf, "sudo dmesg -c -H | tee %s.txt", trim_space(filename));
				parse_command(buf, command);
				pipe_function(command);
				char cmd2[100];
				sprintf(cmd2, "%s.txt", trim_space(filename));
				read_tree_from_file(cmd2);
				find_children();
				char cmd3[100];
				sprintf(cmd3, "%s.png", trim_space(filename));
				plot_graph(cmd3);
				return SUCCESS;
			}
		}
	}
	    
	if(command -> next != NULL){
		return pipe_function(command); //indirect recursion inside process_command
	}	
	
	pid_t pid = fork();
	// child
	if (pid == 0) {
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// TODO: do your own exec with path resolving using execv() - done

		if(strcmp(command->name,"regression")==0){
			regressionAndPlot(command);
		}else if(strcmp(command->name, "hdiff")==0){
			hdiff(command);
		}else if(strcmp(command->name, "textify")==0){
			textify(command);
		}else{
			search_and_run_command(command,0);
		}
		exit(0);

	} else {
		// TODO: implement background processes here done
		//wait(0); // wait for child process to finish
		int status;
		if (command->background == false) {
        		waitpid(pid, &status, 0);
		} 
		return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}

void search_and_run_command(struct command_t *command, int issudo){
	//PART 1
	int exist_checker=0;
	//getting path
	char *path = getenv("PATH");
	char checkedPath[4096];
	char* pTokens = strtok(path, ":");
	
	while(pTokens!=NULL){
		//building path to command
		snprintf(checkedPath, sizeof(checkedPath), "%s/%s", pTokens, command->name);
       //check if there is an accesible file
        if (access(checkedPath, X_OK) == 0) {
            exist_checker = 1;
            break;
        }
        pTokens = strtok(NULL, ":");
	}
	if (exist_checker) {
		if(issudo == 1) execv("/usr/bin/sudo", command->args);
		else execv(checkedPath, command->args);
    } else {
        printf("-%s: %s: command not found\n", sysname, command->name);
    }
}

// Function to perform program piping for Part-2
int pipe_function(struct command_t *command){
	//Create a pipe
    int fd[2];
	if (pipe(fd) == -1) {
		fprintf(stderr,"Pipe failed");
		return EXIT;
	}
	
	//First child
	pid_t left = fork();
	if(left == 0){
    	close(1);
    	//Close read end
    	close(fd[0]);
    	// Redirect stdout to the write end of the pipe:
    	dup2(fd[1],1);
    	//Close write end
		close(fd[1]); 
		search_and_run_command(command,0); //running command
	}

	//Second child
	pid_t right = fork();
	if(right == 0){
       	close(0);
       	close(fd[1]);
       	// Redirect stdin to the read end of the pipe
       	dup2(fd[0],0);
		close(fd[0]);
		process_command(command->next); //Possible recursive
		exit(0);
    	}
    	
	//Parent process
	close(fd[0]);
	close(fd[1]);
	//Wait children to finish:
	waitpid(left,NULL,0);
	waitpid(right,NULL,0);
	return SUCCESS;
}


// Function to combine a directory path and a file name into a single path
void combine_paths(char *result, const char *directory, const char *file) {
	// Check if directory or file is NULL or empty
	if (!directory || !file || !*directory || !*file) {
		// If either directory or file is NULL or empty, copy the non-empty one to result
		strcpy(result, directory ? directory : file ? file : "");
	} else {
		// If both directory and file are not NULL or empty, concatenate them with a separator
		sprintf(result, "%s%s%s", directory, directory[strlen(directory) - 1] == '/' ? "" : "/", file);
	}
}


int check_command_or_filename(char *buf, char *filename_start) {
	const char *splitters = " \t"; 
	int len = strlen(buf);
	int flag = 0;
	while(len>0 && strchr(splitters,buf[0]) != NULL) {  
		buf++;
		len--;
	}
	while(len>0 && strchr(splitters,buf[len-1]) != NULL) {
		buf[--len] = 0; 
		flag = 1;
	}
	char *tokch = strtok(buf,splitters);
	if(tokch == NULL) return 0;

	if(flag) {
		filename_start[0] = '\0';
		return 1;
	}
	tokch = strtok(NULL,splitters);

	if(tokch == NULL) { // check for command case
		strcpy(filename_start, buf);
		return 0;
	};
	char *last_token = tokch;
	while((tokch=strtok(NULL,splitters)) != NULL){ // trim string until char
		last_token=tokch;
		if(last_token[strlen(last_token)-1] == '|') { // check for command case after pipe
			tokch=strtok(NULL,splitters);
			last_token=tokch;
			strcpy(filename_start, last_token);
			return 0;
		}
	} 
	strcpy(filename_start, last_token);
	return 1; // check for file case
}

// Function to search and save all commands under PATH together with custom commands
void save_available_commands(const char *file_name) {
	FILE *file = fopen(file_name, "w");
	if (file == NULL){
		perror("Error opening the txt file");
		return;
	}
    
	char *path = strdup(getenv("PATH"));
	int num = 0;
	char *all_commands[10000];
	char *path_tokenizer = strtok(path, ":");
	while (path_tokenizer != NULL){
		DIR *directory;
		struct dirent *directory_entry;
		directory = opendir(path_tokenizer);
        	if (directory){
			while ((directory_entry = readdir(directory)) != NULL){
				if (directory_entry->d_name[0] == '.') continue;
				char full_path[strlen(path_tokenizer) + strlen(directory_entry->d_name) + 1];
				combine_paths(full_path, path_tokenizer, directory_entry->d_name);
		            	if (access(full_path, X_OK) != 0) continue;
		            		int duplicate = 0;
					for (int i = 0; i < num; i++){
						if (strcmp(all_commands[i], directory_entry->d_name) == 0){
							duplicate = 1;
							break;
						}
					}
					if (!duplicate){
						fprintf(file, "%s\n", directory_entry->d_name);
						all_commands[num++] = strdup(directory_entry->d_name);
					}
				}
		        		closedir(directory);
        	}
        	path_tokenizer = strtok(NULL, ":");
	}
	int num_cmds = sizeof(builtin_command_list) / sizeof(builtin_command_list[0]);
	for(int i=0;i<num_cmds;i++) fprintf(file, "%s\n", builtin_command_list[i]); // add the built in commands
	fclose(file);
	free(path);
	for (int i = 0; i < num; i++) free(all_commands[i]);	
}


// Function to autocomplete commands based on input string
struct autocomplete_struct *command_complete(const char *input_str) {
	int num_matches = 0; // Number of matches found
	struct autocomplete_struct *match = calloc(1, sizeof(struct autocomplete_struct)); // Allocate memory for match struct
	FILE *file = fopen("all_commands.txt", "r"); // Open file containing all commands
	if (file == NULL) {
		perror("Error opening file");
		return match;
	}
	char command[300]; // Buffer to read each command line
	while (fgets(command, sizeof(command), file)) {
		command[strcspn(command, "\n")] = '\0';
        	if (strncmp(command, input_str, strlen(input_str)) == 0) {
			match->matches = (char **)realloc(match->matches, sizeof(char *) * (num_matches + 1)); // Check if command matches input string
			match->matches[num_matches] = (char *)calloc(strlen(command) + 1, 1); // Resize matches array to accommodate new match
			strcpy(match->matches[num_matches++], command); // Copy matched command to matches array
		}
	}
	fclose(file);
	match->count = num_matches;
	return match;
}


// Function to autocomplete directories based on input string
struct autocomplete_struct *directory_complete(const char *input_str) {
	int num_matches = 0; // Number of matches found
	struct autocomplete_struct *match = calloc(1, sizeof(struct autocomplete_struct)); // Allocate memory for match struct
	DIR *directory = opendir("."); // Open current directory
	struct dirent *dir; // Directory entry struct
	if (directory) {
		while ((dir = readdir(directory)) != NULL) { // Read each directory entry
			if (strncmp(dir->d_name, input_str, strlen(input_str)) == 0) { // Check if directory name matches input string
				match->matches = (char **) realloc(match->matches, sizeof(char *) * (num_matches + 1));
				match->matches[num_matches] = (char *)calloc(strlen(dir->d_name) + 1, 1);
				if(dir->d_type != DT_DIR) {
					char *copy = (char *)calloc(strlen(dir->d_name) + 1, 1);
					strcpy(copy, dir->d_name);
					strcpy(match->matches[num_matches++], copy);
					free(copy);
				}else if(dir->d_type == DT_DIR && strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0) {
					char *copy = (char *)calloc(strlen(dir->d_name) + 2, 1); // Allocate memory for for copy with an extra '/' char
					strcpy(copy, dir->d_name);
					copy[strlen(dir->d_name)] = '/';
					copy[strlen(dir->d_name)+1] = '\0';
					strcpy(match->matches[num_matches++], copy); // Copy the copy to the matches array
					free(copy);
				}
			}
		}
        closedir(directory);
	}
	match->count = num_matches;
	return match;
}


void regressionAndPlot(struct command_t *command){
    char* filename;
    int regressionType=1;
    int degree;
    
    //Read from command:
    if(command->arg_count==3){
   	 filename=command->args[1];
    }
    else if(command->arg_count==5){
   	 filename=command->args[1];
   	 degree=atoi(command->args[3]);
   	 if (strcmp(command->args[2], "-p")==0){
   		 regressionType=2;
   	 }
    }
    else{
   	 printf("Number of arguments are not correct\n");
   	 return;
    }
    
    FILE *fp;
    double x[100], y[100];
    int n = 0; // Number of data points
    fp = fopen(filename, "r");
    if (fp == NULL){
   	 printf("Error opening file!\n");
    }
    // Read data points from file
    printf("Data Points:\n");
    printf("x\t y\n");
    while (fscanf(fp, "%lf %lf", &x[n], &y[n]) == 2){
   	 printf("%.2f\t%.2f\n", x[n], y[n]);
   	 n++;
    }
    fclose(fp);
    if (n == 0){
    	printf("Expected data not found!\n");
    }
    
    double coefficients[100],slope, intercept;
	// Perform regression and obtain coefficients.
    
	if(regressionType == 1){ //linear regression
    	double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
   	 // Calculate required sums
   	 for(int i=0; i<n; i++){
   		 sum_y += y[i];
   	 	sum_x += x[i];
   	 	sum_xy += x[i] * y[i];
   	 	sum_xx += x[i] * x[i];
   	 }
   	 slope = (n*sum_xy - sum_x*sum_y)/(n*sum_xx - sum_x*sum_x);
   	 intercept = (sum_y-(slope)*sum_x)/n;
   	 printf("\nLinear Regression Coefficients:\n");
    	printf("Coefficient a0: %.2f\n", intercept);
   	 printf("Coefficient a1: %.2f\n", slope);
	}
    
	else if (regressionType == 2) { //polynomial regression
    	// Constructing needed X matrix and Y vector.
   	 double X[degree+1][degree+1];
   	 for(int i=0; i<=degree; i++){
   	 	for(int j=0; j<=degree; j++){
   	     	X[i][j] = 0;
   	     	for(int k=0; k<n; k++) {
   	         	X[i][j] += pow(x[k], i+j);
   	     	}
   	 	}
   	 }

   	 double Y[degree + 1];
   	 for(int i=0; i<=degree; i++){
   	 	Y[i]=0;
   	 	for(int k=0; k<n; k++){
   	     	Y[i]+=pow(x[k], i)*y[k];
   	 	}
   	 }
   	 
   	 // Solve equations
   	 for(int i=0; i<=degree; i++){
   	 	for(int j=i+1; j<=degree; j++){
   	     	double ratio=X[j][i]/X[i][i];
   	     	for(int k=0; k<=degree; k++){
   	         	X[j][k] -= ratio*X[i][k];
   	     	}
   	     	Y[j] -= ratio*Y[i];
   	 	}
   	 }
   	 for(int i=degree; i>=0; i--){
   	 	coefficients[i] = Y[i];
   	 	for(int j=i+1; j<=degree; j++){
   	     	coefficients[i] -= X[i][j]*coefficients[j];
   	 	}
   	 	coefficients[i]/=X[i][i];
   	 }
   	 
    	printf("\nPolynomial Regression Coefficients:\n");
    	for(int i = 0; i <= degree; i++){
   	 printf("Coefficient a%d: %.2lf\n", i, coefficients[i]);
   	 }
	}else{
    	printf("Invalid regression type!\n");
	}
    
	FILE *datafile = fopen("data.txt", "w");
	if(fp != NULL){
    	for (int i = 0; i < n; i++){
        	fprintf(datafile, "%lf %lf\n", x[i], y[i]);
    	}
    	fclose(datafile);
	}else{
    	fprintf(stderr, "Error: Unable to open file %s for writing\n", filename);
	}   
    
	//Using gnuplot to plot
    
	FILE *gp = popen("gnuplot", "w");
	if (gp != NULL) {
    	fprintf(gp, "set terminal pngcairo enhanced font \"arial,10\" size 800,600\n"); //for appearence
    	fprintf(gp, "set output \"plot.png\"\n");
    	fprintf(gp, "set title \"Regression Plot\"\n");
    	fprintf(gp, "set xlabel \"X\"\n");
    	fprintf(gp, "set ylabel \"Y\"\n");
    	fprintf(gp, "plot \"data.txt\" with points title \"Data Points\", ");
    	if (regressionType == 1){
        	fprintf(gp, "%f*x + %f with lines title \"Linear Regression\"\n", slope, intercept);
    	}else{
        	fprintf(gp, "%f", coefficients[0]);
        	for (int i = 1; i <= degree; i++){
            	fprintf(gp, " + %f*x**%d", coefficients[i], i);
        	}
        	fprintf(gp, " with lines title \"Polynomial Regression\"\n");
    	}
    	fprintf(gp, "quit\n");
    	pclose(gp);
	}else{
    	fprintf(stderr, "Error: Unable to open Gnuplot\n");
	}
}


void textify(struct command_t *command) {
    if (command->arg_count<3) {
        printf("You should enter: <filename> <mode(-count_letters, \
        -count_words,-count_specific_word, -change_words)> [additional arguments]\n");
        return;
    }
    const char *filename=command->args[1];
    
    //open file:
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Failed to open file\n");
        return;
    }
    
    //do what is needed according to mode
    const char *mode=command->args[2];
    
    if (strcmp(mode, "-count_letters")==0) {
        int count=0;
		int c;
		while ((c=fgetc(file)) != EOF) {
		    if (isalpha(c)||isdigit(c)) count++;
		}
        printf("Number of letters in %s: %d\n", filename, count);
        fclose(file);
    } 
    
    else if (strcmp(mode, "-count_words") == 0) {
		int count=0;
		char word[100]; //max word length given as 100
		while (fscanf(file, "%s", word)==1) {
		    count++;
		}
		fclose(file);
		printf("Number of words in %s: %d\n", filename, count);
    } 
    
    else if (strcmp(mode, "-count_specific_word") == 0) {
        if (command->arg_count < 4) {
            printf("Do not forget to enter the word to look for as the third argument!\n");
            return;
        }
        
        const char *searched_word = command->args[3];
        int count=0;
		char word[100];
		while (fscanf(file, "%s", word)==1) {
		    if (strcmp(word, searched_word)==0) count++;
		}
		fclose(file);
        printf("Number of occurrences of '%s' in %s: %d\n", searched_word, filename, count);
    } 
    
    else if (strcmp(mode, "-change_words") == 0) {
        if (command->arg_count < 5) {
            printf("Do not forget to write the word that will be changed as the 3th, word to change to 4th argument\n");
            return;
        }
        
        const char *old_word = command->args[3];
        const char *new_word = command->args[4];
        
        //To name the second file properly.
		const char *dot_position = strrchr(filename, '.');
		if (!dot_position) {
		    fprintf(stderr, "Error: Invalid filename\n");
		    fclose(file);
		    return;
		}
		size_t filename_length = dot_position - filename;
		char updated_filename[filename_length + strlen("-updated") + strlen(".txt") + 1];
		strncpy(updated_filename, filename, filename_length); // Copy the filename without extension
		updated_filename[filename_length] = '\0'; // Null-terminate the string
		strcat(updated_filename, "-updated.txt"); // Append "-updated.txt"

		//Opening new file:
		FILE *updated_file = fopen(updated_filename, "w");
		if (!updated_file) {
		    fprintf(stderr, "Error: Failed to create updated file\n");
		    fclose(file);
		    return;
		}

		char word[100];
		while (fscanf(file, "%s", word) == 1) {
		    if (strcmp(word, old_word) == 0) {
		        fprintf(updated_file, "%s ", new_word);
		    } 
		    else {
		        fprintf(updated_file, "%s ", word);
		    }

		    int next_char = fgetc(file);
		    ungetc(next_char, file); // Put back the character for further processing
		    if (next_char == '\n' || next_char == EOF) {
		        fprintf(updated_file, "\n"); // Add a newline character if it is
		    }
		}
		fclose(file);
		fclose(updated_file);
        printf("Occurrences of '%s' in %s changed to '%s' in %s\n", old_word, filename,  new_word,updated_filename);
        return;
    } 
    else {
        fprintf(stderr, "That mode does not exist!\n");
        return;
    }
}

void hdiff(struct command_t *command){
    char* f_name1;
    char* f_name2;
    int mode_flag=0;
    
    //Read from command:
    if(command->arg_count==4){
   	 f_name1=command->args[1];
   	 f_name2=command->args[2];
    }else if(command->arg_count==5){
   	 f_name1=command->args[2];
   	 f_name2=command->args[3];
   	 if (strcmp(command->args[1], "-b")==0){
   		 mode_flag=1;
   	 }
    }else{
   	 printf("Number of arguments are not correct\n");
   	 return;
    }
    
    //open files
    FILE *file1=fopen(f_name1, "r");
    FILE *file2=fopen(f_name2, "r");
    
    if(file1==NULL || file2==NULL){
    	printf("File not found!\n");
    }
    
    //get the extenisons  of the files:
    char *extension1=NULL;
    char *extension2=NULL;
    //First file:
    char *token1=strtok(f_name1, ".");
    while(token1!=NULL){
   	 extension1=token1;
   	 token1= strtok(NULL,".");
    }
    //Second file:
    char *token2=strtok(f_name2, ".");
    while(token2!=NULL){
   	 extension2=token2;
   	 token2=strtok(NULL, ".");
    }
    
    //Now, compare.
    if(mode_flag==0){ //mode -a, txt file compare
      	 //Necessary checks:
      	 if(extension1==NULL || extension2 ==NULL){
      		 printf("Extension could not be found for at least one of the files!\n");
      		 return;
      	 }else if(strcmp(extension1, "txt")==1|| strcmp(extension2, "txt")==1){
      		 printf("At least one of the files not txt!\n");
      		 return;
   	 }
      	 
   	 int differenceNum=0;
   	 int lineNum=1;
   	 
   	 char *line1=NULL;
   	 size_t size1=0;
   	 ssize_t line_len1=0;
   	 
   	 char *line2=NULL;
   	 size_t size2=0;
   	 ssize_t line_len2=0;
   	 //flags to check if files are finished:
   	 int f1=0;
   	 int f2=0;
      	 
   	 while(true){
   		 line_len1=getline(&line1, &size1, file1);
   		 if(line_len1==-1){f1=1;}
   		 line_len2=getline(&line2, &size2, file2);
   		 if(line_len2==-1){f2=1;}
   		 if(f1 && f2){ //both files finished
   			 break;
   		 }
   		 if(strcmp(line1, line2)){ //if linea are different
   			 differenceNum++;
   			 printf("%s: Line %d: %s",f_name1, lineNum, line1);
   			 printf("%s: Line %d: %s",f_name2, lineNum, line2);
   		 }
   		 lineNum++;
   	 }
      	 if(differenceNum==0)printf("The two text files are identical\n");
   	 else printf("%d different lines found\n", differenceNum);
    }
    
    else{//mode -b, comparing bit by bit:
   	 //getting the length of each file
   	 fseek(file1, 0, SEEK_END);
   	 long len1 = ftell(file1);
   	 rewind(file1);

   	 fseek(file2, 0, SEEK_END);
   	 long len2 = ftell(file2);    
   	 rewind(file2);

   	 int min_len=0;
   	 if(len1>len2){
   		 min_len=len2;
   	 }else{
   		 min_len=len1;  	 
   	 }
  	 
   	 //allocating memory
   	 char *buffer1 = (char*)calloc((len1),1);
   	 char *buffer2 = (char*)calloc((len2), 1);
   	 int differenceNum=0;
   	 int i=0;
  	      while(i<min_len){ //read bit by bit until end of the shorter file
  	     	 fread(buffer1+i,1,1,file1);
  			 fread(buffer2+i,1,1,file2);
  			 if(buffer1[i]!=buffer2[i]){
  				 differenceNum++;
  			 }
  			 i++;  
   	 }
   	 //free memory
   	 free(buffer1);
   	 free(buffer2);       
   	 differenceNum+=(-2*min_len)+len1+len2; //add the difference in length to difference also
  	 
   	 if(differenceNum==0){
   		 printf("The two files are identical\n");
   	 }else {
   		 printf("The two files are different in %d bytes\n", differenceNum);    
   	 }
    }		   	 
}
