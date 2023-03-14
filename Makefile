main : main.o threadpool.o
	g++ -o main main.o threadpool.o -lpthread
main.o : main.cpp threadpool.h
	g++ -c main.cpp
threadpool.o : threadpool.cpp threadpool.h
	g++ -c threadpool.cpp

clean : 
	rm -f main.o threadpool.o main
