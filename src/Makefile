CXXFLAGS = -std=c++11 -Wall -Wextra -Wpedantic -g -fPIC -fstack-protector-all 
OBJS = psirver.o utils.o Tasks.o TaskFactory.o
BIN = ./psirver # The main binary

all: $(BIN)

include DEPEND

$(BIN): $(OBJS) 
	$(CXX) -o $(BIN) $(OBJS) $(LDLAGS)

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f $(BIN) $(OBJS) DEPEND

DEPEND: 
	$(CXX) -MM *.cc > DEPEND

test:
	@./test.sh
#END
