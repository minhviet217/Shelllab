/* CSCI2467 Shell Lab
 * tsh - A tiny shell program with job control
 * 
 * <Viet Minh Nguyen/vmnuye2@uno.edu>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void do_redirect(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* Ignoring these signals simplifies reading from stdin/stdout */
    Signal(SIGTTIN, SIG_IGN);          /* ignore SIGTTIN */
    Signal(SIGTTOU, SIG_IGN);          /* ignore SIGTTOU */


    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}

/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
/*References of the following code lines: 1. book: "CS: APP", 2. Tips from slide: /lecture/07ecf-part2/pdf*/
void eval(char *cmdline) 
{
    int bg;//Declare variable and name it as bg(background)
    char *argv[MAXARGS];//Declare argument list as an array of type char, each pointer in this array points to an argument string. 
    pid_t pid;//Declare variable name pid as process ID, type pid_t
    struct job_t *job;
    sigset_t mask;
    sigset_t prev_mask;
    //Call parseline function that will parse the command line and build the argv array
    //If user has requested a background job, the function will return 1
    bg = parseline(cmdline, argv);

    //argv[0] is the name of the executable object file(from the book "CS: APP")
    //If argv[0] is equal to Null, that means no executable object file will be read
    if (argv[0] == NULL)
        return; 

    //Call function builtin_cmd, check if the return value is false, that means no command is built in
    //if the return value is true, at least one command is built in
    if (!builtin_cmd(argv)){
       
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTSTP);
        sigprocmask(SIG_BLOCK, &mask, &prev_mask);//Block SIGINT and save previous blacked set
        
        //Call fork() to create new processes, return twice
        //Check if pid = 0, child process is created, the tsh shell will execute whatever inside child's process
        if ((pid = fork()) == 0){
            sigprocmask(SIG_UNBLOCK, &mask, NULL);//Child must unblock signals before execve()
            setpgid(0,0);
            
            do_redirect(argv);
            
            //Call function execve() which will load and run the executable object file in argv[0] by convention
            //execve() will not return any value if the executable object file is found and execute sucessfully
            //on the contrary, it will return -1
            //if it returns -1, print out the message that "Command not found" if execve() return -1
            if (execve(argv[0], argv, environ)<0){
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        }

        //if the user has NOT asked for a BACKGROUND job, the tsh shell will call function waitfg() to wait until the foreground job to terminate
        if(!bg){
            addjob(jobs, pid, FG, cmdline); 
            sigprocmask(SIG_UNBLOCK, &mask, NULL);//Parents will unblock signals after addjob()
            //Thus, call waitfg() to wait until a child in its wait set terminates
            waitfg(pid);         
        }
        //ELSE PRINT OUT COMMAND LINE THAT THE TSH SHELL IS EXECUTING AND ITS PROCESS ID IN BACKGROUND
        else{
            addjob(jobs, pid, BG, cmdline);
            job = getjobpid(jobs, pid);//get process ID of the job
            sigprocmask(SIG_UNBLOCK, &mask, NULL);//Parents wull unblock signals after addjob()
            printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        }
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 * Book: page 791
 */
int builtin_cmd(char **argv) 
{
    if (!strcmp(argv[0], "quit")) {//Deal with quit command
        exit(0);
    }
    if (!strcmp(argv[0], "jobs")) {//Deal with jobs command
        listjobs(jobs);
        return 1;
    }
    if (!strcmp(argv[0], "bg")) {//Deal with background job command
        do_bgfg(argv);
        return 1;
    }
    if (!strcmp(argv[0], "fg")) {//Deal with fore ground job command
        do_bgfg(argv);
        return 1;
    }
    
    return 0;     /* not a builtin command */
}


/* 
 * do_redirect - scans argv for any use of < or > which indicate input or output redirection
 *
 */
void do_redirect(char **argv)
{
        int i;
        int fd;
        system("touch file.txt");//Create a file named file.txt as input file for reading
        system("touch test.out");//Create a file named test.out to write output into
        
        for(i=0; argv[i]; i++)
        {
                if (!strcmp(argv[i],"<")) {
                        /* add code for input redirection below */
                        //open existing file name file.txt for reading only
                        fd = open("file.txt", O_RDONLY, 0);
                        //Check system call for errors
                        if (fd < 0){
                            perror("fail to open the input file!");
                        }
                        //change standard input to the new file discriptor
                        dup2(fd,0);
                        //Close the open file
                        close(fd);
                        /* the line below cuts argv short. This
                           removes the < and whatever follows from argv */
                        argv[i]=NULL;
                }
                else if (!strcmp(argv[i],">")) {
                        /* add code for output redirection here */
                        //open existing file name test.out for writing only
                        //if the file already exists, then truncate it
                        fd = open("test.out", O_WRONLY|O_TRUNC, STDOUT_FILENO);
                        //Check system call for errors
                        if (fd < 0){
                            perror("fail to create output file");
                        }
                        //change standard output to the new file discriptor
                        dup2(fd,STDOUT_FILENO);
                        //Close the open file
                        close(fd); 
                        /* the line below cuts argv short. This
                           removes the > and whatever follows from argv */
                        argv[i]=NULL;
                }
        }
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
//I got advices from office hours TA (David) to write the following code lines for this do_bgfg() function
//Ref: 2467.cs.uno.edu/activities/next-steps.pdf
void do_bgfg(char **argv) 
{
    struct job_t *job;
    pid_t argvPid;
    int jid;
    
    //Arguments for fg or bg is missing, argv[0] = fg or bg --> argv[1] = Null if the arguments are missing
    if (argv[1] == NULL){
        printf("%s command requires PID or %%jobid argument\n",argv[0]);
        return;
    }

    //If the first token of an argument for fg or bg is not a "%" --> it can not be a job id, it only can be a pid
    if (argv[1][0] != '%'){
    //Continue to check if it is valid pid or not
        //if not, print out the message of invalid argument
        if (!isdigit(argv[1][0])){
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
        }
        //If argument for fg or bg is a valid pid
        //use atoi() to convert char* to int
        argvPid = atoi(argv[1]);
        //Find a job (by argvPid) on the job list
        job = getjobjid(jobs, argvPid);
        //if no such process, print out that message
        if (job == NULL){
            printf("(%s): No such process\n", argv[1]);
            return;
        }
    }
    //Check if the first token of an argument for fg or bg is "%"
    else{
     //if it is true, continue to check the second token of that argument, if it is not a digit --> invalid arguments
        if (!isdigit(argv[1][1])){
            printf("%s: argument must be a PID or %%jobid\n", argv[0]);
            return;
        }
        //If arguments for fg or bg are valid ones
        //use atoi() to convert char* to int
        //followed by "%"" will be an integer and it is aslo a job id
        jid = atoi(&argv[1][1]);
        //Find a job (by jid) on the job list
        job = getjobjid(jobs, jid);
        //if no such job, print the message out
        if (job == NULL){
            printf("%s: No such job\n", argv[1]);
            return;
        }
}

    //command line is background job
    if (!(strcmp(argv[0], "bg"))){
        //Change state of job to background
        job->state = BG;
        //Send continue signal to run again all processes that are suspended before
        kill(-(job->pid), SIGCONT);
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
    //Comand line is forefround job
    else {
        //Change state of job to foreground
        job->state = FG;
        //Send continue signal to run again all processes that are suspended before
        kill(-(job->pid), SIGCONT);
        //Call waitfg() to wait until the process is terminated
        waitfg(job->pid);
    }
    return;
}


/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    //Get pid of current foreground job, stored in variable temp
    pid_t temp = fgpid(jobs);
    struct job_t *job;
    //Find a job (by temp) on the job list
    job = getjobpid(jobs, temp);
    //if temp is not equal to given pid, do nothing and return
    if (temp != pid)
        return;
    
    //if pid is equal to pid of foreground job in the job lists, waitfg() will block until this pid is no longer the fg process
    //Blocking way here is putting this process to sleep infinitely (Ref: 2467.cs.uno.edu/activities/next-steps.pdf)
    while(job->state == FG){
        sleep(1);
        }
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
//Got advices from David (TA) to write the following code lines
void sigchld_handler(int sig) 
{
    pid_t pid;
    int child_status;

    //Call waitpid to suspends execution of the calling process until a child process in its wait set terminate (Ref: Cs: APP pg.780)
    //WNOHANG|WUNTRACED: return pid of terminated or stopped process/ other way, return 0 if no child process has stopped or terminated
    while ((pid = waitpid(-1, &child_status, WNOHANG|WUNTRACED)) > 0){
        //if the child process terminate normally, delete it based on its pid
        if(WIFEXITED(child_status)){
            deletejob(jobs, pid);
        }
        //if the child process that caused the return is currently stopped, return true
        else if(WIFSTOPPED(child_status)){
            //when it is true, get job based on process id
            struct job_t *job = getjobpid(jobs, pid);
            //Change job's state to stopped state
            job->state = ST;
        }
        //if the child process terminated because of an uncaught signal, return true
        else if(WIFSIGNALED(child_status)){ 
            //when it is true, delete that job 
            deletejob(jobs, pid);
        }
    }

    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
   pid_t pid;
   struct job_t *job;
   //Get pid of current foreground job
   pid = fgpid(jobs);
   
   //If the pid is valid
   if (pid > 0){
       //Send SIGINT signal to all processes that are running in a group
       kill(-pid, sig);
       //Find a job from jobs list based on its pid
       job = getjobpid(jobs, pid);
       //print out its property
       printf("Job [%d] (%d) terminated by signal %d\n",job->jid, job->pid, sig);
   } 
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid;
   struct job_t *job;
   //Get pid of current foreground job
   pid = fgpid(jobs);
   
   //If the pid is valid
   if (pid > 0){
       //Send SIGSTP signal to all processes that are running in a group
       kill(-pid, SIGTSTP);
       //Find a job from jobs list based on its pid
       job = getjobpid(jobs, pid);
       //Change jobs's state into stopped state
       job->state = ST;
       //Print out its property
       printf("Job [%d] (%d) stopped by signal %d\n",job->jid, job->pid, sig);
   } 
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



