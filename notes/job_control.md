To suspend a job, type CTRL-Z while it is running. This is analogous to typing CTRL-C, except that you can resume the job after you have stopped it. When you type CTRL-Z, the shell responds with a message like this:
```
[1]+  Stopped                 command
```
Then it gives your prompt back. It also puts the suspended job at the top of the job list, as indicated by the + sign.

To resume a suspended job so that it continues to run in the foreground, just type `fg`. If for some reason, you put other jobs in the background after you have typed CTRL-Z, use `fg` with a job name or number.
```
fred is running...
CTRL-Z
[1]+  Stopped                 fred
$ bob &
[2]     bob &
run a command in the background with an ampersand (&)
$ fg %fred
fred resumes in the foreground...
```

When you run a shell session, all the process you run at the command line are child processes of that shell. If you log out or your session crashes or otherwise ends unexpectedly, SIGHUP kill signals will be sent to its child processes (including background processes) to end them too.

You can get around this by telling the process(es) that you want kept alive to ignore SIGHUP signals. There are two ways to do this: by using the `nohup` command to run the command in an environment where it will ignore termination signals.
