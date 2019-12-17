all: sshell REPORT.html clean

sshell: sshell.o   
	gcc -Wall -Werror -o sshell sshell.o 

sshell.o: sshell.c   
	gcc -Wall -Werror -c -o sshell.o sshell.c

README.html: REPORT.md
	pandoc -o REPORT.html REPORT.md  

clean:
	rm -f sshell.o
