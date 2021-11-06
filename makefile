bridge: main.o
	g++ -O2 -o $@ $< -lpthread

%.o: %.cc
	g++ -O2 -c $<
