ACLOCAL_AMFLAGS = -I config

bin_PROGRAMS = zmqtest
man1_MANS = zmqtest.1

zmqtest_SOURCES = zmq-test.cpp zmq.hpp

zmqtest.1: zmqtest
	-$(HELP2MAN) --output=$@ --name='Test ZMQ pub-sub failure' --no-info ./zmqtest

run: zmqtest
	./zmqtest
