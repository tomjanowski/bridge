bridge: main.o
	g++ -O2 -o $@ $<

%.o: %.cc
	g++ -O2 -c $<
