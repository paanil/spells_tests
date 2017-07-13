
g++ -Wall -Werror -Wno-missing-braces -std=c++11 -O2 -I../rapidcheck/include -I../rapidcheck -c rapidcheck.cpp

g++ -Wall -std=c++11 -I../spells -I../rapidcheck/include test.cpp ../spells/json_parser.cpp rapidcheck.o -o test

test
