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
$ fg %fred
fred resumes in the foreground...
```