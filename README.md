# rhubarb-geek-nz/console-logger
Console Logger for Win32

## Summary

This program captures the output from a console session.

## Syntax

The program takes a command line to execute.

Redirect stdout or stderr to a file to capture the output.

If no program is given it uses the command line interpreter from the COMSPEC environment.

```
conlog.exe [command line....] >logfile.txt
```

## Mechanics

The program creates a pseudo console and runs a child process using the console. Output is written to the true console and the log file. Either stdout or stderr can be used to redirect to the log file.
