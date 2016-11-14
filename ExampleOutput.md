
```
-bash-4.1$ ls -l
total 64
-rwx------ 1 nay10 allusers    14 Mar 31 01:03 baa
-rwx------ 1 nay10 allusers 21436 Mar 31 15:29 index
-rwxr--r-- 1 nay10 allusers  3812 Mar 31 15:38 indexer.c
-rw------- 1 nay10 allusers  5096 Mar 30 22:17 indexer.o
-rwxr--r-- 1 nay10 allusers   279 Mar 31 00:46 Makefile
-rw------- 1 nay10 allusers   133 Mar 31 15:27 out.txt
-rwxr--r-- 1 nay10 allusers  6012 Mar 31 15:36 sorted-list.c
-rwxr--r-- 1 nay10 allusers   555 Mar 31 11:12 sorted-list.h
drwx------ 3 nay10 allusers  4096 Mar 30 20:04 test1
-bash-4.1$ more Makefile
#makefile
 
cc = gcc
root = /pathname
flags = -g -I -Wall
compile = $(cc) $(flags)
 
all: index
 
index: indexer.c sorted-list.c sorted-list.h
        $(compile) indexer.c sorted-list.c sorted-list.h -o index
 
indexer.o: indexer.c
        $(cc) -c indexer.c
 
clean:
        rm -rf *.o
-bash-4.1$ make
gcc -g -I -Wall indexer.c sorted-list.c sorted-list.h -o index
 
***************************** TEST INPUT IS A SINGLE FILE  *************************
-bash-4.1$ ./index out.txt baa
-bash-4.1$ more out.txt
{"list" : [
        {"A" : [
                {"baa" : 1}
        ]}
        {"Baa" : [
                {"baa" : 2}
        ]}
        {"cat" : [
                {"baa" : 1}
        ]}
        {"name" : [
                {"baa" : 1}
        ]}
]}
***************************** TEST INPUT IS DIRECTORY *************************
-bash-4.1$ ./index out.txt test1   
-bash-4.1$ more out.txt
{"list" : [
        {"A" : [
                {"test1/baa" : 1}
                {"test1/boo" : 1}
                {"test1/test2/baa" : 1}
                {"test1/test2/boo" : 1}
        ]}
        {"Baa" : [
                {"test1/baa" : 2}
                {"test1/test2/baa" : 2}
        ]}
        {"Boo" : [
                {"test1/boo" : 2}
                {"test1/test2/boo" : 2}
        ]}
        {"cat" : [
                {"test1/baa" : 1}
                {"test1/test2/baa" : 1}
        ]}
        {"dog" : [
                {"test1/boo" : 1}
                {"test1/test2/boo" : 1}
        ]}
        {"name" : [
                {"test1/baa" : 1}
                {"test1/boo" : 2}
                {"test1/test2/baa" : 1}
                {"test1/test2/boo" : 2}
        ]}
]}
-bash-4.1$
```
