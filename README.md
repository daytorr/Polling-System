# Polling System
Created a polling system in C that allows any number of clients to concurrently connect, read, and write to
a server. This server stores all the polls provided from an input file, along with each polls’ choices and each
choices’ vote count. The server also stores general information about the system (total number of logins,
threads created, and votes), along with its clients (username, active flag, bit vector to represent which polls
have been voted on already). The client can request to login with a username, vote on polls they have not
yet voted on, view poll statistics, and logout.
