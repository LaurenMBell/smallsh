#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <signal.h>

// GLOBAL VARIABLES!
volatile sig_atomic_t foreground_only_mode = 0; // tracks whether shell is in fg or bg mode
int last_status = 0; // exit status of the last fg process
pid_t background_pids[100]; //array of background pids 
int background_count = 0; //number of bg processes

// function for ^Z 
void SIGTSTP_handler(int sig) {
  //turn on foreground mode
  if (sig && foreground_only_mode == 0){ 
    foreground_only_mode = 1;
    char* message = "Entering foreground-only mode (& is now ignored)\n";
    write(STDOUT_FILENO, message, strlen(message));

  //turn off foreground mode
  } else if (sig && foreground_only_mode == 1) {
    foreground_only_mode = 0;
    char* message = "Exiting foreground-only mode\n";
    write(STDOUT_FILENO, message, strlen(message));
  }
}

/*
Function to replace $$ with pid in input tokens
*/
void pid_replacement(char* input) {
  char buffer[4096];
  memset(buffer, 0, sizeof(buffer));

  char* inpt = input;
  char* out = buffer;

  while (*inpt) {
    if (inpt[0] == '$' && inpt[1] == '$') {
      int pid = getpid();
      out+= sprintf(out, "%d", pid);
      inpt += 2;
    } else {
      *out++ = *inpt++;
    }
  }
  strcpy(input, buffer);
}

/*
Function to take user input string, and tokenize into an
array of commands 
*/
char** tokenize_input(char* input) {
  char** tokens = malloc(20 * sizeof(char*));
  int i = 0;

  char* tok = strtok(input, " ");
  while (tok != NULL && i < 19) {
    tokens[i++] = tok;
    tok = strtok(NULL, " ");
  }
  
  tokens[i] = NULL;
  return tokens;
}

/*
Function to run the status command, built-in 
*/
void status_command() {
  //check if child process terminated normally 
  if (WIFEXITED(last_status)) {
    printf("exit value %d\n", WEXITSTATUS(last_status));
  //check if child process terminated by a signal 
  } else if (WIFSIGNALED(last_status)) {
    printf("terminated by signal %d\n", WTERMSIG(last_status));
  }
  fflush(stdout);
}

/*
Function to run builtin exit process (kill all child processes before exiting)
*/
void exit_command() {
  //one by one kill background processes by sending SIGKILL
  for (int i = 0; i < background_count; i++) {
    kill(background_pids[i], SIGKILL);
  }
  exit(0); //return status code 0
}


/*
Function to execute non-built in commands by creating child processes
*/
int fork_process(char** tokens) {
  int background = 0;

  // check for '&' to run process in the background
  int i = 0;
  while (tokens[i] != NULL) i++;
  int last = i - 1;

  if (last >= 0 && strcmp(tokens[last], "&") == 0) {
    if (foreground_only_mode == 0) {
      //if foreground only mode is off, run normally
      background = 1;
    }
    //remove '&' if in foreground only 
    tokens[last] = NULL;
  }

  // check for redirection operators in both/either directions
  int i_redirection = 0;
  int o_redirection = 0;
  int i_index = -1;
  int o_index = -1;

  for (int j = 0; tokens[j] != NULL; j++) {
    if (strcmp(tokens[j], "<") == 0) {
      i_redirection = 1;
      i_index = j;
    }
    if (strcmp(tokens[j], ">") == 0) {
      o_redirection = 1;
      o_index = j;
    }
  }

  // fork a new shell process using fork()!
  pid_t child_pid = fork();

  //child process
  if (child_pid == 0) {

    //bg children ignore SIGINT, fg children get default SIGINT
    struct sigaction sa_int = {0};
    if (background == 0) {
      sa_int.sa_handler = SIG_DFL;
    } else {
      sa_int.sa_handler = SIG_IGN;
    }
    sigaction(SIGINT, &sa_int, NULL);

    //redirect output if < or > in tokens
    if (i_redirection) {
      int input_file = open(tokens[i_index + 1], O_RDONLY);
      if (input_file == -1) {
        perror("open <");
        exit(1);
      }
      //duplicate file descriptor
      dup2(input_file, STDIN_FILENO);
      close(input_file);
    }

    if (o_redirection) {
      int output_file = open(tokens[o_index + 1], O_WRONLY | O_TRUNC | O_CREAT, 0644);
      if (output_file == -1) {
        perror("open >");
        exit(1);
      }
      //duplicate the file descriptor
      dup2(output_file, STDOUT_FILENO);
      close(output_file);
      }

    //Remove redirection operators after forking 
    char* new_tokens[20];
    int k = 0;

    for (int j = 0; tokens[j] != NULL; j++) {
        if (strcmp(tokens[j], "<") == 0 || strcmp(tokens[j], ">") == 0) {
            j++; // skip operator + filename
            continue;
        }
        new_tokens[k++] = tokens[j];
    }
    new_tokens[k] = NULL;

    if (background ==1) {
      //no input file
      int input = 0;
      int output = 0;

      for (int k = 0; tokens[k] != NULL; k++) {
        if (strcmp(tokens[k], "<") == 0 ) {
          input = 1;
        }
        if (strcmp(tokens[k], ">") == 0) {
          output = 1;
        }
      }

      if (!input) {
        int fd = open("/dev/null", O_RDONLY);
        dup2(fd, 0);
        close(fd);
      }
      if (!output) {
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, 1);
        close(fd);
      }

    }

    //replace current process with forked process
    execvp(new_tokens[0], new_tokens);
    perror(tokens[0]);
    exit(1);
  }

  // Parent process
  if (background == 0) {
    //wait for child process to finish running
    waitpid(child_pid, &last_status, 0);

    if (WIFSIGNALED(last_status)) {
      printf("terminated by signal %d\n", WTERMSIG(last_status));
    }
  } else {
    printf("background pid is %d\n", child_pid);
    background_pids[background_count++] = child_pid;
    fflush(stdout);
  }

  return 0;
}


/*
Function for running 'cd' 
*/
int cd_command(char** tokens) {
  if (tokens[1] == NULL) {
    //cd home if no path given 
    chdir(getenv("HOME"));
  } else {
    chdir(tokens[1]);
  }
}

int main() {
	//set up SIGINT for parent processes using sigaction
  struct sigaction sa_int = {0}; 
  sa_int.sa_handler = SIG_IGN; //no signal catching function because parent ignores SIGSTP
  sigfillset(&sa_int.sa_mask); //all other signlas are blocked
  sa_int.sa_flags = 0;
  sigaction(SIGINT, &sa_int, NULL);
  
  //configure SIGSTP using sigaction
  struct sigaction sa_tstp = {0};
  sa_tstp.sa_handler = SIGTSTP_handler; //signal handling function, foreground only mode
  sigfillset(&sa_tstp.sa_mask);
  sa_tstp.sa_flags = SA_RESTART; //if SIGTSTP comes while waiting for input, prompt again
  sigaction(SIGTSTP, &sa_tstp, NULL); 

  int running = 1;
  while (running) {
    pid_t pid;
    int init_status;

    //wait for the child whole process ID is equal to the value of pid
    while ((pid = waitpid(-1, &init_status, WNOHANG)) > 0) {
        printf("background pid %d is done\n", pid);
        //return true if child terminated normally (should be 0)
        if (WIFEXITED(init_status)) {
          printf("exit value %d\n", WEXITSTATUS(init_status));
        //return the exit status of the child process (should be nothing, this part shouldn't run)
        } else if (WIFSIGNALED(init_status)) {
          printf("terminated by signal %d\n", WTERMSIG(init_status));
        }
        fflush(stdout);
    }

    char buff[256];
    int n = 256;
    printf(": ");

    //read in input 
    if (fgets(buff, n, stdin) == NULL) {
      //reset the error and end-of-file indicators
      clearerr(stdin);
      continue;
    }

    // clean input, repalce $$ with pid, and tokenize input
    buff[strcspn(buff, "\n")] = '\0';
    pid_replacement(buff);
    char** tokens = tokenize_input(buff);

    // skip to the next line if it's a newline or coment
    if (buff[0] == '#' || tokens[0] == NULL) {
      printf("");
    
    // look at the tokens to execute commands
    } else {
      char* cmd = tokens[0]; 

      // exit command - exit program
      if (strcmp(cmd, "exit") == 0) {
        //printf("Bye!\n");
        free(tokens);
        exit_command();

      // cd command - change directories
      } else if (strcmp(cmd, "cd") == 0) {
        cd_command(tokens);

      // status command - get exit status value
      } else if (strcmp(cmd, "status") == 0) {
        // exit status value
        status_command();

      // non-builtin commands, fork new child process
      } else {
        fork_process(tokens);
      }

      //clean up memory 
      free(tokens);

    }
  }
  return 0;
}
