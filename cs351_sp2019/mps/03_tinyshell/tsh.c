/*
 * tsh - A tiny shell program with job control
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Misc manifest constants */
#define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

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
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */

struct job_t {           /* The job struct */
  pid_t pid;             /* job PID */
  int jid;               /* job ID [1, 2, ...] */
  int state;             /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
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
int main(int argc, char **argv) {
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  dup2(1, 2);


  // printf("Main Shell Process: pid=%d, group=%d\n", getpid(), getpgrp());

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h': /* print help message */
      usage();
      break;
    case 'v': /* emit additional diagnostic info */
      verbose = 1;
      break;
    case 'p':          /* don't print a prompt */
      emit_prompt = 0; /* handy for automatic testing */
      break;
    default:
      usage();
    }
  }

  /* Install the signal handlers */

  /* These are the ones you will need to implement */
  Signal(SIGINT, sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

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
void eval(char *cmdline) {

  int i, bg;
  char *argv[MAXARGS];

  bg = parseline(cmdline, argv);
  if (argv[0] == NULL) {
    // nothing entered, continue command prompt...
    return;
  }

  // tying "print output" to verbose state.
  if (verbose) {
    if (bg) {
      printf("background job requested\n");
    }
    for (i = 0; argv[i] != NULL; i++) {
      printf("argv[%d]=%s%s", i, argv[i], (argv[i + 1] == NULL) ? "\n" : ", ");
    }
  }

  char *command = argv[0];
  // built-in commands:
  // quit, jobs, bg <job> --> use pid, fg <job> --> use pid
  if (strcmp(command, "quit") == 0) {
    builtin_cmd(argv);
    return;
  } else if (strcmp(command, "jobs") == 0) {
    builtin_cmd(argv);
    return;
  } else if (strcmp(command, "bg") == 0) {
    builtin_cmd(argv);
    return;
  } else if (strcmp(command, "fg") == 0) {
    builtin_cmd(argv);
    return;
  }

  // Create child process for job; this has group ID for killing,
  pid_t npid;
  if ((npid = fork()) == 0) {
    // a.) set group for itself
    setpgrp();

    // b.) execute the command of the child
    if (execve(argv[0], argv, environ) < 0) {
      //failing execution.
      printf("%s: error executing command. \n", argv[0]);
      exit(0);
    }
  }
  addjob(jobs, npid, bg ? BG : FG, cmdline);

  // parent; if BG, continue; else wait on the pid from child
  if (!bg) {
    waitfg(npid);
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
int parseline(const char *cmdline, char **argv) {
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim;                /* points to first space delimiter */
  int argc;                   /* number of args */
  int bg;                     /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  } else {
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
    } else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;

  if (argc == 0) /* ignore blank line */
    return 1;

  /* should the job run in the background? */
  if ((bg = (*argv[argc - 1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv) {
  char *command = argv[0];
  if (strcmp(command, "quit") == 0) {
    
    // delete all children and jobs, then exit.
    initjobs(jobs);
    kill(-getpid(), SIGKILL);
    exit(0);
  } else if (strcmp(command, "jobs") == 0) {

    pid_t jobspid;
    if ((jobspid = fork()) == 0) {
      listjobs(jobs);
      exit(0);
    }

    // make shell wait on the jobs task.
    waitfg(jobspid);
  } else if (strcmp(command, "bg") == 0 || strcmp(command, "fg") == 0) {
    do_bgfg(argv);
  } else {
    return 0;
  }

  return 1; /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {

  char *command = argv[0];
  struct job_t *job;

  // check if the arg is existent, if not, break out show error:
  if (argv[1] == NULL) {
    fprintf(stderr, "err: %s - No process or job id given\n", command);
    return;
  } 

  pid_t argpid;
  if (argv[1][0] == '%') {
    //get by jobid
    int jid = atoi(argv[1]+1);
    job = getjobjid(jobs, jid);
  } else {
    //get by procid
    pid_t argid = atoi(argv[1]);
    job = getjobpid(jobs, argid);
  }

  //no such job, return
  if (job == NULL) {
    fprintf(stderr, "err: No such job for job/process id\n");
    return;
  }

  argpid = job->pid;

  //resume the job
  if (kill(job->pid, SIGCONT) < 0) {
    printf("error restarting job");
  }

  //set the job state of the resumed job
  if (strcmp(command, "bg") == 0) {
    job->state = BG;
  } else if (strcmp(command, "fg") == 0) {
    job->state = FG;

    waitfg(argpid);
  }
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
  int status;
  while (1) {
    //wait for a process, we will see the status
    if (waitpid(pid, &status, WUNTRACED) == -1) {
      // child gives us an error in the foreground... idk what to do.
      // probably "child error", delete it then go..
      // printf("error: pid=%d, status=%d\n", pid, status);
      // the child terminated? remove the job and go fg
      deletejob(jobs, pid);
      break;
    }

    // exit, clean break. no need to do anything
    if (WIFEXITED(status)) {
      // printf("ExitStatus: %d\n", WEXITSTATUS(status));

      deletejob(jobs, pid);
      break;
    }

    if (WIFSIGNALED(status)) {
      int sigused = WTERMSIG(status);
      if (sigused == SIGSTOP || sigused == SIGTSTP) {
        kill(pid, SIGSTOP);
        // printf("signaled stop from child\n");
        //stoppage to the child, label the job, etc.
        struct job_t *job = getjobpid(jobs, pid);
        job->state = ST;
        printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, sigused);
        break;
      } else if (sigused == SIGINT) {
        //int child and its children
        kill(-pid, SIGINT);
        kill(pid, SIGINT);
        struct job_t * job = getjobpid(jobs, pid);
        printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, sigused);
        deletejob(jobs, pid);
        break;
      } else if (sigused == SIGCONT) {
        // this isn't an important signal...
        continue;
      }
    }
    
    if (WIFSTOPPED(status)) {
      //forward signal to child, update job status, resume shell
      kill(pid, SIGTSTP);
      struct job_t *job = getjobpid(jobs, pid);
      job->state = ST;
      printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, SIGTSTP);
      break;
    }
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
void sigchld_handler(int sig) {
  // printf("SIGCHILD; mypid=%d, parentpid=%d, mygroup=%d\n", getpid(),
  // getppid(), getgid());
  // printf("Signal: %d\n", sig);


  //do we arrive here from bg? if so, figure it out...



  return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
  // send signal to children using group ids; this should be fgid..
  pid_t fgid = fgpid(jobs);
  // printf("SIGINT; mypid=%d, parentpid=%d, mygroup=%d, fgpid=%d\n", getpid(), getppid(),
  // getgid(), fgid);

  if (getpid() != fgid) {
    //I'm not the fg process, send to kill/int
    kill(fgid, SIGKILL);
    deletejob(jobs, fgid);
  } else {
    // I am the fg job, send the interrupts down
    // kill(-getpid(), SIGINT);
  }
  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) {
  // is this the parent? figure that out.
  pid_t fgid = fgpid(jobs);
  // printf("SIGSTOP; mypid=%d, parentpid=%d, mygroup=%d, fgpid=%d\n", getpid(),
  // getppid(), getgid(), fgid);

  if (getpid() != fgid) {
    //i'm not the foreground job, sent to stop and handle the info about that job.
    kill(fgid, SIGSTOP);
    struct job_t *job = getjobpid(jobs, fgid);
    job->state = ST;

  } else {
    // I'm the foreground job (shell), send them through.
    // kill(-getpid(), SIGTSTP);
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
int maxjid(struct job_t *jobs) {
  int i, max = 0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
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
      if (verbose) {
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid,
               jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs) + 1;
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
struct job_t *getjobjid(struct job_t *jobs, int jid) {
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
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
void listjobs(struct job_t *jobs) {
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
        printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
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
void usage(void) {
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
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
void sigquit_handler(int sig) {
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}
