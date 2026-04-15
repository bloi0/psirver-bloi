CXXFLAGS = -std=c++14 -Wall -Wextra -Wpedantic -g -fPIC -fstack-protector-all 
OBJS = psirver.o utils.o Tasks.o TaskFactory.o Jobs.o
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
