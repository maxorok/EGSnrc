include $(EGS_CONFIG)
include $(SPEC_DIR)egspp.spec
include $(SPEC_DIR)egspp_$(my_machine).conf

CXX      = g++
CXXFLAGS = -Wall -Wextra -O2 -std=c++11

all: neighbour-test msh-parser-tests

msh-parser-tests: msh-parser-tests.cpp ../msh_parser.h
		$(CXX) -std=c++11 -Wall -Wextra -I../ msh-parser-tests.cpp $(EOUT)$@

neighbour-test: neighbour-test.cpp ../mesh_neighbours.h
	$(CXX) $(CXXFLAGS) neighbour-test.cpp -I../ -o neighbour-test

# include $(HEN_HOUSE)makefiles$(DSEP)cpp_makefile
