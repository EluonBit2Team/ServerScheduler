all:
	gcc -o server_scheduler.out server_scheduler.c -I/usr/include/mysql -L/usr/lib64/mysql -lmysqlclient


clean:
	rm -f server_scheduler.out

re: clean all
